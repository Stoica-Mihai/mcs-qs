#include "qml.hpp"

#include <unistd.h>

#include <qcoreapplication.h>
#include <qdbusconnection.h>
#include <qdbuserror.h>
#include <qdbusextratypes.h>
#include <qdbuspendingcall.h>
#include <qdbuspendingreply.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qobject.h>
#include <qstring.h>
#include <qtypes.h>

#include "../../core/logcat.hpp"
#include "dbus_manager.h"
#include "dbus_session.h"

namespace qs::service::logind {

namespace {
QS_LOGGING_CATEGORY(logLogind, "quickshell.service.logind", QtWarningMsg);
}

Logind::Logind(): QObject(nullptr) {
	this->manager = new DBusLogindManager(
	    "org.freedesktop.login1",
	    "/org/freedesktop/login1",
	    QDBusConnection::systemBus(),
	    this
	);

	if (!this->manager->isValid()) {
		qCWarning(logLogind) << "Cannot reach logind on the system bus";
		return;
	}

	QObject::connect(this->manager, &DBusLogindManager::PrepareForSleep,
	                 this, &Logind::onPrepareForSleep);
	QObject::connect(this->manager, &DBusLogindManager::PrepareForShutdown,
	                 this, &Logind::onPrepareForShutdown);

	this->resolveSession();
}

Logind* Logind::instance() {
	static Logind* g = new Logind();
	return g;
}

void Logind::resolveSession() {
	auto pid = static_cast<quint32>(getpid());
	auto pending = this->manager->GetSessionByPID(pid);
	auto* watcher = new QDBusPendingCallWatcher(pending, this);
	QObject::connect(
	    watcher,
	    &QDBusPendingCallWatcher::finished,
	    this,
	    [this](QDBusPendingCallWatcher* w) {
		    QDBusPendingReply<QDBusObjectPath> reply = *w;
		    if (reply.isError()) {
			    qCWarning(logLogind) << "GetSessionByPID failed:" << reply.error().message();
		    } else {
			    this->onSessionResolved(reply.value());
		    }
		    w->deleteLater();
	    }
	);
}

void Logind::onSessionResolved(const QDBusObjectPath& path) {
	this->mSessionPath = path.path();
	emit this->sessionResolved();
	qCInfo(logLogind) << "resolved session" << this->mSessionPath;
}

DBusLogindSession* Logind::sessionProxy() {
	if (this->session == nullptr && !this->mSessionPath.isEmpty()) {
		this->session = new DBusLogindSession(
		    "org.freedesktop.login1",
		    this->mSessionPath,
		    QDBusConnection::systemBus(),
		    this
		);
	}
	return this->session;
}

void Logind::lock() {
	auto* s = this->sessionProxy();
	if (s == nullptr) {
		qCWarning(logLogind) << "lock() before session was resolved";
		return;
	}
	s->Lock();
}

void Logind::unlock() {
	auto* s = this->sessionProxy();
	if (s == nullptr) return;
	s->Unlock();
}

void Logind::suspend(bool interactive) { this->manager->Suspend(interactive); }
void Logind::hibernate(bool interactive) { this->manager->Hibernate(interactive); }
void Logind::powerOff(bool interactive) { this->manager->PowerOff(interactive); }
void Logind::reboot(bool interactive) { this->manager->Reboot(interactive); }

void Logind::setBrightness(const QString& subsystem, const QString& name, quint32 value) {
	auto* s = this->sessionProxy();
	if (s == nullptr) {
		qCWarning(logLogind) << "setBrightness() before session was resolved";
		return;
	}
	s->SetBrightness(subsystem, name, value);
}

void Logind::onPrepareForSleep(bool start) {
	this->bPrepareForSleep = start;
}
void Logind::onPrepareForShutdown(bool start) {
	this->bPrepareForShutdown = start;
}

// ── LogindQml singleton wrapper ───────────────────────────────────

LogindQml::LogindQml(QObject* parent): QObject(parent) {
	auto* core = Logind::instance();
	QObject::connect(core, &Logind::sessionResolved, this, &LogindQml::sessionResolved);
	QObject::connect(core, &Logind::prepareForSleepChanged,
	                 this, &LogindQml::prepareForSleepChanged);
	QObject::connect(core, &Logind::prepareForShutdownChanged,
	                 this, &LogindQml::prepareForShutdownChanged);
}

} // namespace qs::service::logind
