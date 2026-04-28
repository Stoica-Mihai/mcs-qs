#pragma once

#include <qdbusabstractadaptor.h>
#include <qdbusconnection.h>
#include <qdbusmessage.h>
#include <qdbusvirtualobject.h>
#include <qhash.h>
#include <qmetaobject.h>
#include <qobject.h>
#include <qqmlintegration.h>
#include <qqmlparserstatus.h>
#include <qstring.h>
#include <qtmetamacros.h>
#include <qvariant.h>

#include "../../core/reload.hpp"

namespace qs::dbus::server {

class DBusIpcHandler;

///! Internal — virtual object that backs a DBusIpcHandler.
class DBusIpcVirtualObject: public QDBusVirtualObject {
	Q_OBJECT;

public:
	explicit DBusIpcVirtualObject(DBusIpcHandler* handler): handler(handler) {}

	[[nodiscard]] QString introspect(const QString& path) const override;
	bool handleMessage(const QDBusMessage& message, const QDBusConnection& connection) override;

private:
	DBusIpcHandler* handler;
};

///! Publish QML-declared functions over D-Bus.
///
/// Mirrors the ergonomics of @@QtIo.IpcHandler but routes calls through the
/// session bus instead of Quickshell's CLI socket. Declare `function` members
/// with explicit argument types and the handler will register them as D-Bus
/// methods on the configured `service` + `path` + `iface`. Any process on
/// the session bus can then invoke them (e.g. `busctl --user call`).
///
/// Currently supported argument / return types: `string`, `int`, `bool`,
/// `real`, `void`. Signals and properties are not yet exposed (planned).
///
/// ### Example
/// ```qml
/// DBusIpcHandler {
///   service: "com.mcshell.Shell"
///   path: "/Shell"
///   iface: "com.mcshell.Shell"
///
///   function toggleVolume(): void { sharedDropdown.togglePanel("volume") }
///   function setBrightness(percent: int): void { Brightness.setPercent(percent) }
/// }
/// ```
///
/// Then from any process on the user session:
/// ```sh
/// busctl --user call com.mcshell.Shell /Shell com.mcshell.Shell ToggleVolume
/// busctl --user call com.mcshell.Shell /Shell com.mcshell.Shell SetBrightness i 50
/// ```
class DBusIpcHandler: public PostReloadHook {
	Q_OBJECT;
	/// Whether the handler is currently registered on the bus. Toggle to
	/// temporarily disable without losing configuration.
	Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged);
	/// D-Bus service (well-known name) to register on the session bus.
	/// Required and must be unique across the bus.
	Q_PROPERTY(QString service READ service WRITE setService NOTIFY serviceChanged);
	/// Object path to expose. Defaults to `/`.
	Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged);
	/// D-Bus interface name. Required.
	Q_PROPERTY(QString iface READ iface WRITE setIface NOTIFY ifaceChanged);
	/// True once the handler successfully claimed the service name.
	Q_PROPERTY(bool registered READ registered NOTIFY registeredChanged);
	QML_ELEMENT;

public:
	explicit DBusIpcHandler(QObject* parent = nullptr): PostReloadHook(parent) {}
	~DBusIpcHandler() override;
	Q_DISABLE_COPY_MOVE(DBusIpcHandler);

	void onPostReload() override;

	[[nodiscard]] bool enabled() const { return this->mEnabled; }
	void setEnabled(bool enabled);

	[[nodiscard]] QString service() const { return this->mService; }
	void setService(const QString& service);

	[[nodiscard]] QString path() const { return this->mPath; }
	void setPath(const QString& path);

	[[nodiscard]] QString iface() const { return this->mIface; }
	void setIface(const QString& iface);

	[[nodiscard]] bool registered() const { return this->mRegistered; }

	// Used by DBusIpcVirtualObject.
	[[nodiscard]] QString generateIntrospect() const;
	bool dispatchCall(const QDBusMessage& message, const QDBusConnection& connection);

signals:
	void enabledChanged();
	void serviceChanged();
	void pathChanged();
	void ifaceChanged();
	void registeredChanged();

private:
	struct MethodEntry {
		QMetaMethod method;
		QString dbusName;        // PascalCase D-Bus name
		QByteArray inSignature;  // e.g. "si"
		QByteArray outSignature; // "" for void, "s"/"i"/etc for single return
	};

	void enumerateMethods();
	void tryRegister();
	void unregister();

	bool mEnabled = true;
	bool mRegistered = false;
	QString mService;
	QString mPath = "/";
	QString mIface;

	// Resolved method registry, keyed by D-Bus name.
	QHash<QString, MethodEntry> methods;

	DBusIpcVirtualObject* virtualObject = nullptr;
};

} // namespace qs::dbus::server
