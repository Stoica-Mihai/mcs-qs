#include "handler.hpp"

#include <qcoreapplication.h>
#include <qdbusconnection.h>
#include <qdbusextratypes.h>
#include <qdbusmessage.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qmetaobject.h>
#include <qmetatype.h>
#include <qobject.h>
#include <qstring.h>
#include <qstringbuilder.h>
#include <qtypes.h>
#include <qvariant.h>

#include "../../core/logcat.hpp"

namespace qs::dbus::server {

namespace {
QS_LOGGING_CATEGORY(logDBusIpc, "quickshell.dbus.ipc", QtWarningMsg);

// Map a Qt meta-type to a D-Bus single-value signature byte. Returns an empty
// QByteArray if the type can't be expressed on the bus.
QByteArray dbusSignatureFor(int typeId) {
	switch (typeId) {
	case QMetaType::Void: return {};
	case QMetaType::QString: return "s";
	case QMetaType::Int: return "i";
	case QMetaType::UInt: return "u";
	case QMetaType::LongLong: return "x";
	case QMetaType::ULongLong: return "t";
	case QMetaType::Bool: return "b";
	case QMetaType::Double: return "d";
	case QMetaType::Float: return "d"; // float widens to double on the bus
	default: return {};
	}
}

// Pascal-case the QML method name so D-Bus consumers see a conventional name.
// `toggleVolume` → `ToggleVolume`. Names already starting with an uppercase
// letter are returned as-is.
QString toDBusName(const QString& qmlName) {
	if (qmlName.isEmpty()) return qmlName;
	if (qmlName.at(0).isUpper()) return qmlName;
	return qmlName.at(0).toUpper() % qmlName.mid(1);
}

QVariant variantFromDBusArg(const QVariant& arg, int targetTypeId) {
	auto v = arg;
	if (v.userType() == targetTypeId) return v;
	if (!v.convert(QMetaType(targetTypeId))) {
		qCWarning(logDBusIpc) << "Could not convert" << arg << "to type id" << targetTypeId;
		return {};
	}
	return v;
}

} // namespace

// ── DBusIpcVirtualObject ──────────────────────────────────────────

QString DBusIpcVirtualObject::introspect(const QString& /*path*/) const {
	return this->handler->generateIntrospect();
}

bool DBusIpcVirtualObject::handleMessage(
    const QDBusMessage& message,
    const QDBusConnection& connection
) {
	return this->handler->dispatchCall(message, connection);
}

// ── DBusIpcHandler ────────────────────────────────────────────────

DBusIpcHandler::~DBusIpcHandler() { this->unregister(); }

void DBusIpcHandler::onPostReload() {
	this->enumerateMembers();
	if (this->mEnabled) this->tryRegister();
}

void DBusIpcHandler::setEnabled(bool enabled) {
	if (this->mEnabled == enabled) return;
	this->mEnabled = enabled;
	emit this->enabledChanged();

	if (enabled) {
		if (!this->mRegistered) this->tryRegister();
	} else {
		this->unregister();
	}
}

void DBusIpcHandler::setService(const QString& service) {
	if (this->mService == service) return;
	this->unregister();
	this->mService = service;
	emit this->serviceChanged();
	if (this->mEnabled) this->tryRegister();
}

void DBusIpcHandler::setPath(const QString& path) {
	if (this->mPath == path) return;
	this->unregister();
	this->mPath = path;
	emit this->pathChanged();
	if (this->mEnabled) this->tryRegister();
}

void DBusIpcHandler::setIface(const QString& iface) {
	if (this->mIface == iface) return;
	this->unregister();
	this->mIface = iface;
	emit this->ifaceChanged();
	if (this->mEnabled) this->tryRegister();
}

// ── SignalRelay ────────────────────────────────────────────────────
// One relay per registered signal OR property-notify. Mirrors the
// per-type-slot pattern from IpcSignalListener — Qt's signal/slot
// system requires a slot whose signature matches the source signal,
// so we provide one slot per supported arg type and pick the right
// one at connect time.
class DBusIpcHandler::SignalRelay: public QObject {
	Q_OBJECT;

public:
	SignalRelay(DBusIpcHandler* handler, const SignalEntry& entry)
	    : QObject(handler), handler(handler), signalEntry(entry) {
		const char* slotSig = nullptr;
		const auto& sig = entry.signature;
		if (sig.isEmpty())   slotSig = "onVoid()";
		else if (sig == "s") slotSig = "onString(QString)";
		else if (sig == "i") slotSig = "onInt(int)";
		else if (sig == "b") slotSig = "onBool(bool)";
		else if (sig == "d") slotSig = "onDouble(double)";
		else { qCWarning(logDBusIpc) << "SignalRelay: unsupported signature" << sig; return; }

		const auto& mo = SignalRelay::staticMetaObject;
		auto slot = mo.method(mo.indexOfSlot(slotSig));
		QObject::connect(handler, entry.signal, this, slot);
	}

	SignalRelay(DBusIpcHandler* handler, const PropertyEntry& entry)
	    : QObject(handler), handler(handler), propertyEntry(entry), isProperty(true) {
		const auto& mo = SignalRelay::staticMetaObject;
		auto slot = mo.method(mo.indexOfSlot("onPropertyNotify()"));
		QObject::connect(handler, entry.property.notifySignal(), this, slot);
	}

private slots:
	void onVoid()                   { handler->emitSignalRaw(signalEntry, QVariant()); }
	void onString(const QString& v) { handler->emitSignalRaw(signalEntry, QVariant(v)); }
	void onInt(int v)               { handler->emitSignalRaw(signalEntry, QVariant(v)); }
	void onBool(bool v)             { handler->emitSignalRaw(signalEntry, QVariant(v)); }
	void onDouble(double v)         { handler->emitSignalRaw(signalEntry, QVariant(v)); }
	void onPropertyNotify() {
		handler->emitPropertyChanged(propertyEntry, propertyEntry.property.read(handler));
	}

private:
	DBusIpcHandler* handler;
	SignalEntry signalEntry;
	PropertyEntry propertyEntry;
	bool isProperty = false;
};

void DBusIpcHandler::emitSignalRaw(const SignalEntry& entry, const QVariant& arg) {
	if (!this->mRegistered) return;
	auto bus = QDBusConnection::sessionBus();
	auto msg = QDBusMessage::createSignal(this->mPath, this->mIface, entry.dbusName);
	if (!entry.signature.isEmpty() && arg.isValid()) msg << arg;
	bus.send(msg);
}

void DBusIpcHandler::emitPropertyChanged(
    const PropertyEntry& entry,
    const QVariant& value
) {
	if (!this->mRegistered) return;
	auto bus = QDBusConnection::sessionBus();
	auto msg = QDBusMessage::createSignal(
	    this->mPath,
	    "org.freedesktop.DBus.Properties",
	    "PropertiesChanged"
	);
	QVariantMap changed;
	changed.insert(entry.dbusName, value);
	msg << this->mIface << changed << QStringList();
	bus.send(msg);
}

void DBusIpcHandler::enumerateMembers() {
	this->methods.clear();
	this->properties.clear();
	this->signals_.clear();
	for (auto* relay: this->signalRelays) relay->deleteLater();
	this->signalRelays.clear();

	const auto* mo = this->metaObject();
	const auto methodOffset = DBusIpcHandler::staticMetaObject.methodCount();
	const auto propertyOffset = DBusIpcHandler::staticMetaObject.propertyCount();

	for (auto i = methodOffset; i < mo->methodCount(); ++i) {
		auto method = mo->method(i);

		// Signals — capture a per-signal entry and connect via SignalRelay.
		if (method.methodType() == QMetaMethod::Signal) {
			QByteArray sig;
			if (method.parameterCount() == 0) {
				// no args
			} else if (method.parameterCount() == 1) {
				sig = dbusSignatureFor(method.parameterType(0));
				if (sig.isEmpty()) {
					qCWarning(logDBusIpc) << "DBusIpcHandler" << this << ": skipping signal"
					                      << method.name() << "— unsupported argument type:"
					                      << method.parameterTypeName(0);
					continue;
				}
			} else {
				qCWarning(logDBusIpc) << "DBusIpcHandler" << this << ": skipping signal"
				                      << method.name()
				                      << "— only zero- or one-arg signals are supported";
				continue;
			}

			SignalEntry entry;
			entry.signal = method;
			entry.dbusName = toDBusName(QString::fromUtf8(method.name()));
			entry.signature = sig;
			this->signals_.insert(entry.dbusName, entry);
			continue;
		}

		// Methods (Q_INVOKABLE / public-slot user-declared).
		if (method.methodType() != QMetaMethod::Method
		    && method.methodType() != QMetaMethod::Slot)
		{
			continue;
		}

		// Map argument types.
		QByteArray inSig;
		bool typesOk = true;
		for (auto a = 0; a < method.parameterCount(); ++a) {
			auto sig = dbusSignatureFor(method.parameterType(a));
			if (sig.isEmpty()) {
				qCWarning(logDBusIpc) << "DBusIpcHandler" << this << ": skipping method"
				                      << method.name() << "— unsupported argument type at"
				                      << a << ":" << method.parameterTypeName(a);
				typesOk = false;
				break;
			}
			inSig += sig;
		}
		if (!typesOk) continue;

		// Map return type. Empty == void on the bus. QML wraps void-returning
		// functions as QVariant at the metaobject level, so treat that as void.
		auto returnType = method.returnType();
		QByteArray outSig;
		if (returnType != QMetaType::Void && returnType != QMetaType::QVariant) {
			outSig = dbusSignatureFor(returnType);
			if (outSig.isEmpty()) {
				qCWarning(logDBusIpc) << "DBusIpcHandler" << this << ": skipping method"
				                      << method.name() << "— unsupported return type:"
				                      << method.typeName();
				continue;
			}
		}

		auto dbusName = toDBusName(QString::fromUtf8(method.name()));

		MethodEntry entry;
		entry.method = method;
		entry.dbusName = dbusName;
		entry.inSignature = inSig;
		entry.outSignature = outSig;

		this->methods.insert(dbusName, entry);
	}

	// Properties — only those declared after our base metaobject offset.
	for (auto i = propertyOffset; i < mo->propertyCount(); ++i) {
		auto prop = mo->property(i);
		auto sig = dbusSignatureFor(prop.metaType().id());
		if (sig.isEmpty()) {
			// Containers we treat specially.
			if (prop.metaType().id() == QMetaType::QStringList) sig = "as";
			else if (prop.metaType().id() == QMetaType::QVariantMap) sig = "a{sv}";
		}
		if (sig.isEmpty()) {
			qCWarning(logDBusIpc) << "DBusIpcHandler" << this << ": skipping property"
			                      << prop.name() << "— unsupported type:" << prop.typeName();
			continue;
		}

		PropertyEntry entry;
		entry.property = prop;
		entry.dbusName = toDBusName(QString::fromUtf8(prop.name()));
		entry.signature = sig;
		this->properties.insert(entry.dbusName, entry);
	}

	// Wire signal relays after both methods + signals are known.
	for (auto& entry: this->signals_) {
		auto* relay = new SignalRelay(this, entry);
		this->signalRelays.append(relay);
	}

	// Connect property-notify signals to PropertiesChanged emission via
	// a per-property SignalRelay configured in property mode (no D-Bus
	// signal name; the relay just calls back into emitPropertyChanged).
	for (auto& entry: this->properties) {
		if (!entry.property.hasNotifySignal()) continue;
		auto* relay = new SignalRelay(this, entry);
		this->signalRelays.append(relay);
	}

	qCDebug(logDBusIpc) << "DBusIpcHandler" << this << "enumerated"
	                    << this->methods.size() << "methods,"
	                    << this->signals_.size() << "signals,"
	                    << this->properties.size() << "properties";
}

void DBusIpcHandler::tryRegister() {
	if (this->mRegistered) return;
	if (this->mService.isEmpty() || this->mIface.isEmpty()) return;

	auto bus = QDBusConnection::sessionBus();

	if (this->virtualObject == nullptr) {
		this->virtualObject = new DBusIpcVirtualObject(this);
	}

	if (!bus.registerVirtualObject(this->mPath, this->virtualObject,
	                               QDBusConnection::SingleNode))
	{
		qCWarning(logDBusIpc) << "DBusIpcHandler" << this << ": could not register virtual"
		                      << "object at path" << this->mPath
		                      << "(another object may already be registered)";
		return;
	}

	if (!bus.registerService(this->mService)) {
		qCWarning(logDBusIpc) << "DBusIpcHandler" << this << ": could not claim service"
		                      << this->mService
		                      << "(another process may already own it)";
		bus.unregisterObject(this->mPath);
		return;
	}

	this->mRegistered = true;
	emit this->registeredChanged();
	qCInfo(logDBusIpc) << "DBusIpcHandler" << this << "registered as" << this->mService
	                   << "at" << this->mPath << "with iface" << this->mIface;
}

void DBusIpcHandler::unregister() {
	if (!this->mRegistered) return;
	auto bus = QDBusConnection::sessionBus();
	bus.unregisterService(this->mService);
	bus.unregisterObject(this->mPath);
	this->mRegistered = false;
	emit this->registeredChanged();
}

QString DBusIpcHandler::generateIntrospect() const {
	QString xml;
	xml += "<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\"";
	xml += " \"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n";
	xml += "<node>\n";

	// Standard introspectable interface (QtDBus also handles this on its side).
	xml += "  <interface name=\"org.freedesktop.DBus.Introspectable\">\n";
	xml += "    <method name=\"Introspect\">\n";
	xml += "      <arg name=\"xml\" type=\"s\" direction=\"out\"/>\n";
	xml += "    </method>\n";
	xml += "  </interface>\n";

	// Standard Properties interface (only emit if we have any properties).
	if (!this->properties.isEmpty()) {
		xml += "  <interface name=\"org.freedesktop.DBus.Properties\">\n";
		xml += "    <method name=\"Get\">\n";
		xml += "      <arg name=\"interface\" type=\"s\" direction=\"in\"/>\n";
		xml += "      <arg name=\"name\" type=\"s\" direction=\"in\"/>\n";
		xml += "      <arg name=\"value\" type=\"v\" direction=\"out\"/>\n";
		xml += "    </method>\n";
		xml += "    <method name=\"GetAll\">\n";
		xml += "      <arg name=\"interface\" type=\"s\" direction=\"in\"/>\n";
		xml += "      <arg name=\"props\" type=\"a{sv}\" direction=\"out\"/>\n";
		xml += "    </method>\n";
		xml += "    <signal name=\"PropertiesChanged\">\n";
		xml += "      <arg name=\"interface\" type=\"s\"/>\n";
		xml += "      <arg name=\"changed\" type=\"a{sv}\"/>\n";
		xml += "      <arg name=\"invalidated\" type=\"as\"/>\n";
		xml += "    </signal>\n";
		xml += "  </interface>\n";
	}

	// User interface.
	xml += "  <interface name=\"" % this->mIface % "\">\n";
	for (auto it = this->methods.constBegin(); it != this->methods.constEnd(); ++it) {
		const auto& m = it.value();
		xml += "    <method name=\"" % m.dbusName % "\">\n";

		// Args, named after the method's parameter names where available.
		auto names = m.method.parameterNames();
		for (auto i = 0; i < m.inSignature.size(); ++i) {
			auto argName = names.value(i);
			if (argName.isEmpty()) argName = QByteArray("arg") + QByteArray::number(i);
			xml += "      <arg name=\"" % QString::fromUtf8(argName)
			    % "\" type=\"" % QString::fromUtf8(m.inSignature.mid(i, 1))
			    % "\" direction=\"in\"/>\n";
		}
		if (!m.outSignature.isEmpty()) {
			xml += "      <arg name=\"result\" type=\"" % QString::fromUtf8(m.outSignature)
			    % "\" direction=\"out\"/>\n";
		}

		xml += "    </method>\n";
	}

	for (auto it = this->signals_.constBegin(); it != this->signals_.constEnd(); ++it) {
		const auto& s = it.value();
		xml += "    <signal name=\"" % s.dbusName % "\">\n";
		if (!s.signature.isEmpty()) {
			auto names = s.signal.parameterNames();
			auto argName = QString::fromUtf8(names.value(0));
			if (argName.isEmpty()) argName = "value";
			xml += "      <arg name=\"" % argName % "\" type=\""
			    % QString::fromUtf8(s.signature) % "\"/>\n";
		}
		xml += "    </signal>\n";
	}

	for (auto it = this->properties.constBegin(); it != this->properties.constEnd(); ++it) {
		const auto& p = it.value();
		xml += "    <property name=\"" % p.dbusName % "\" type=\""
		    % QString::fromUtf8(p.signature) % "\" access=\"read\"/>\n";
	}

	xml += "  </interface>\n";

	xml += "</node>\n";
	return xml;
}

bool DBusIpcHandler::dispatchCall(
    const QDBusMessage& message,
    const QDBusConnection& connection
) {
	const auto iface = message.interface();
	const auto member = message.member();

	// Handle Introspect ourselves so static config doesn't need it.
	if (iface == "org.freedesktop.DBus.Introspectable" && member == "Introspect") {
		auto reply = message.createReply(QVariant(this->generateIntrospect()));
		connection.send(reply);
		return true;
	}

	// org.freedesktop.DBus.Properties — Get / GetAll. Set is omitted in v2.
	if (iface == "org.freedesktop.DBus.Properties") {
		const auto args = message.arguments();
		if (member == "Get" && args.size() == 2) {
			auto requestedIface = args.at(0).toString();
			auto propName = args.at(1).toString();
			if (requestedIface != this->mIface) {
				connection.send(message.createErrorReply(
				    QDBusError::UnknownInterface,
				    QString("No such interface: %1").arg(requestedIface)
				));
				return true;
			}
			auto it = this->properties.constFind(propName);
			if (it == this->properties.constEnd()) {
				connection.send(message.createErrorReply(
				    QDBusError::UnknownProperty,
				    QString("No such property: %1").arg(propName)
				));
				return true;
			}
			QDBusVariant wrapped(it->property.read(this));
			connection.send(message.createReply(QVariant::fromValue(wrapped)));
			return true;
		}
		if (member == "GetAll" && args.size() == 1) {
			auto requestedIface = args.at(0).toString();
			QVariantMap out;
			if (requestedIface == this->mIface) {
				for (auto it = this->properties.constBegin();
				     it != this->properties.constEnd(); ++it)
				{
					out.insert(it->dbusName, it->property.read(this));
				}
			}
			connection.send(message.createReply(QVariant::fromValue(out)));
			return true;
		}
		// Set, etc — fall through to default handling.
		return false;
	}

	if (iface != this->mIface) return false; // QtDBus will return UnknownInterface

	auto entryIt = this->methods.constFind(member);
	if (entryIt == this->methods.constEnd()) return false;
	const auto& entry = entryIt.value();

	const auto args = message.arguments();
	if (args.size() != entry.method.parameterCount()) {
		auto err = message.createErrorReply(
		    QDBusError::InvalidArgs,
		    QString("Method %1 expects %2 args, got %3")
		        .arg(member)
		        .arg(entry.method.parameterCount())
		        .arg(args.size())
		);
		connection.send(err);
		return true;
	}

	// Build generic arguments by converting QVariants to the method's parameter types.
	QVariantList converted;
	converted.reserve(args.size());
	for (auto i = 0; i < args.size(); ++i) {
		auto v = variantFromDBusArg(args.at(i), entry.method.parameterType(i));
		if (!v.isValid() && entry.method.parameterType(i) != QMetaType::Void) {
			auto err = message.createErrorReply(
			    QDBusError::InvalidArgs,
			    QString("Could not coerce argument %1 to %2")
			        .arg(i)
			        .arg(QString::fromUtf8(entry.method.parameterTypeName(i)))
			);
			connection.send(err);
			return true;
		}
		converted.append(v);
	}

	auto getArg = [&](int i) {
		return i < converted.size()
		    ? QGenericArgument(converted.at(i).typeName(), const_cast<void*>(converted.at(i).constData()))
		    : QGenericArgument();
	};

	// Invoke. Up to 10 args (QMetaMethod::invoke limit).
	if (entry.method.parameterCount() > 10) {
		auto err = message.createErrorReply(
		    QDBusError::NotSupported,
		    QString("Method %1 has more than 10 arguments").arg(member)
		);
		connection.send(err);
		return true;
	}

	if (entry.outSignature.isEmpty()) {
		// Void return.
		auto ok = entry.method.invoke(
		    this,
		    Qt::DirectConnection,
		    getArg(0), getArg(1), getArg(2), getArg(3), getArg(4),
		    getArg(5), getArg(6), getArg(7), getArg(8), getArg(9)
		);
		if (!ok) {
			auto err = message.createErrorReply(
			    QDBusError::Failed,
			    QString("Invocation of %1 failed").arg(member)
			);
			connection.send(err);
			return true;
		}
		auto reply = message.createReply();
		connection.send(reply);
		return true;
	}

	// Non-void return: capture into a QVariant.
	QVariant returnSlot(QMetaType(entry.method.returnType()), nullptr);
	auto ok = entry.method.invoke(
	    this,
	    Qt::DirectConnection,
	    QGenericReturnArgument(returnSlot.typeName(), returnSlot.data()),
	    getArg(0), getArg(1), getArg(2), getArg(3), getArg(4),
	    getArg(5), getArg(6), getArg(7), getArg(8), getArg(9)
	);
	if (!ok) {
		auto err = message.createErrorReply(
		    QDBusError::Failed,
		    QString("Invocation of %1 failed").arg(member)
		);
		connection.send(err);
		return true;
	}

	auto reply = message.createReply(returnSlot);
	connection.send(reply);
	return true;
}

} // namespace qs::dbus::server

#include "handler.moc"
