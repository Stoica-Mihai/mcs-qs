#include "handler.hpp"

#include <qcoreapplication.h>
#include <qdbusconnection.h>
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
	this->enumerateMethods();
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

void DBusIpcHandler::enumerateMethods() {
	this->methods.clear();

	const auto* mo = this->metaObject();
	const auto baseOffset = DBusIpcHandler::staticMetaObject.methodCount();

	for (auto i = baseOffset; i < mo->methodCount(); ++i) {
		auto method = mo->method(i);

		// Only Q_INVOKABLE / public-slot user-declared methods. Skip signals
		// (handled separately in v2) and constructor-like helpers.
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

		// Map return type. Empty == void on the bus.
		auto returnType = method.returnType();
		QByteArray outSig;
		if (returnType != QMetaType::Void) {
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

	qCDebug(logDBusIpc) << "DBusIpcHandler" << this << "enumerated"
	                    << this->methods.size() << "methods";
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
