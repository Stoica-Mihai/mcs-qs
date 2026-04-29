#pragma once

#include <qdbusextratypes.h>
#include <qobject.h>
#include <qproperty.h>
#include <qqmlintegration.h>
#include <qstring.h>
#include <qtmetamacros.h>

#include "../../core/doc.hpp"

class DBusLogindManager;
class DBusLogindSession;

namespace qs::service::logind {

class Logind: public QObject {
	Q_OBJECT;

public:
	[[nodiscard]] static Logind* instance();

	void lock();
	void unlock();
	void suspend(bool interactive);
	void hibernate(bool interactive);
	void powerOff(bool interactive);
	void reboot(bool interactive);
	void setBrightness(const QString& subsystem, const QString& name, quint32 value);

	[[nodiscard]] QString sessionPath() const { return this->mSessionPath; }
	[[nodiscard]] QBindable<bool> bindablePrepareForSleep() { return &this->bPrepareForSleep; }
	[[nodiscard]] QBindable<bool> bindablePrepareForShutdown() { return &this->bPrepareForShutdown; }

signals:
	void sessionResolved();
	void prepareForSleepChanged();
	void prepareForShutdownChanged();

private slots:
	void onSessionResolved(const QDBusObjectPath& path);
	void onPrepareForSleep(bool start);
	void onPrepareForShutdown(bool start);

private:
	explicit Logind();

	void resolveSession();
	[[nodiscard]] DBusLogindSession* sessionProxy();

	QString mSessionPath;
	DBusLogindManager* manager = nullptr;
	DBusLogindSession* session = nullptr;

	Q_OBJECT_BINDABLE_PROPERTY(Logind, bool, bPrepareForSleep, &Logind::prepareForSleepChanged);
	Q_OBJECT_BINDABLE_PROPERTY(Logind, bool, bPrepareForShutdown, &Logind::prepareForShutdownChanged);
};

///! systemd-logind interface.
/// Native binding for the system-bus `org.freedesktop.login1` service.
/// Replaces shelling out to `loginctl` / `systemctl` for session lock and
/// power actions, and exposes the broadcast `PrepareForSleep` /
/// `PrepareForShutdown` signals so the shell can react before sleep/poweroff
/// (e.g. flush state, dim the display) without polling.
///
/// Brightness can also be adjusted via @@setBrightness, which writes
/// `Session.SetBrightness(subsystem, name, value)` — useful so the shell
/// doesn't need to ship a `brightnessctl` dependency for simple cases.
class LogindQml: public QObject {
	Q_OBJECT;
	QML_NAMED_ELEMENT(Logind);
	QML_SINGLETON;
	// clang-format off
	/// Object path of the user's active session, or empty until logind has
	/// been queried. The C++ resolution happens once on construction.
	Q_PROPERTY(QString sessionPath READ sessionPath NOTIFY sessionResolved);
	/// True while logind is preparing to put the system to sleep — the
	/// signal it's based on (`PrepareForSleep`) is emitted with `true`
	/// just before suspend and with `false` once the system has resumed.
	Q_PROPERTY(bool preparingForSleep READ default NOTIFY prepareForSleepChanged BINDABLE bindablePrepareForSleep);
	/// True while logind is preparing to shut the system down (or reboot).
	Q_PROPERTY(bool preparingForShutdown READ default NOTIFY prepareForShutdownChanged BINDABLE bindablePrepareForShutdown);
	// clang-format on

public:
	explicit LogindQml(QObject* parent = nullptr);

	/// Lock the user's current session.
	Q_INVOKABLE void lock() { Logind::instance()->lock(); }
	/// Unlock the user's current session.
	Q_INVOKABLE void unlock() { Logind::instance()->unlock(); }
	/// Suspend the system. `interactive=true` lets logind prompt for
	/// authentication if required; `false` returns an error instead.
	Q_INVOKABLE void suspend(bool interactive = false) {
		Logind::instance()->suspend(interactive);
	}
	/// Hibernate the system.
	Q_INVOKABLE void hibernate(bool interactive = false) {
		Logind::instance()->hibernate(interactive);
	}
	/// Power off the system.
	Q_INVOKABLE void powerOff(bool interactive = false) {
		Logind::instance()->powerOff(interactive);
	}
	/// Reboot the system.
	Q_INVOKABLE void reboot(bool interactive = false) {
		Logind::instance()->reboot(interactive);
	}
	/// Set brightness on a kernel subsystem. Typical call:
	/// `Logind.setBrightness("backlight", "intel_backlight", 80000)`.
	Q_INVOKABLE void
	setBrightness(const QString& subsystem, const QString& name, quint32 value) {
		Logind::instance()->setBrightness(subsystem, name, value);
	}

	[[nodiscard]] QString sessionPath() const { return Logind::instance()->sessionPath(); }
	[[nodiscard]] static QBindable<bool> bindablePrepareForSleep() {
		return Logind::instance()->bindablePrepareForSleep();
	}
	[[nodiscard]] static QBindable<bool> bindablePrepareForShutdown() {
		return Logind::instance()->bindablePrepareForShutdown();
	}

signals:
	void sessionResolved();
	void prepareForSleepChanged();
	void prepareForShutdownChanged();
};

} // namespace qs::service::logind
