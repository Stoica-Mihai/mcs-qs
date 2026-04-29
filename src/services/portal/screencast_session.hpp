#pragma once

#include <qdbusabstractadaptor.h>
#include <qdbusextratypes.h>
#include <qdbusservicewatcher.h>
#include <qhash.h>
#include <qobject.h>
#include <qstring.h>
#include <qstringlist.h>
#include <qtmetamacros.h>
#include <qtypes.h>
#include <utility>

namespace qs::service::portal {

class ScreenCastSessionImpl;

/// One in-flight ScreenCast session. Created by ScreenCastImpl::CreateSession,
/// destroyed when the caller invokes Session.Close or drops off the bus
/// (whichever comes first). At this stage there are no streams yet — the
/// session just owns its registered Session adaptor and tears it down on
/// close. Stream construction lands in step 6.
class ScreenCastSession: public QObject {
	Q_OBJECT;

public:
	explicit ScreenCastSession(
	    QDBusObjectPath sessionHandle,
	    QString appId,
	    QString senderUniqueName,
	    QObject* parent = nullptr
	);
	~ScreenCastSession() override;
	Q_DISABLE_COPY_MOVE(ScreenCastSession);

	[[nodiscard]] QString appId() const { return this->mAppId; }
	[[nodiscard]] QDBusObjectPath sessionHandle() const { return this->mSessionHandle; }

	void setSelectedSourceIds(QStringList ids) { this->mSelectedSourceIds = std::move(ids); }
	[[nodiscard]] QStringList selectedSourceIds() const { return this->mSelectedSourceIds; }
	void setCursorMode(quint32 mode) { this->mCursorMode = mode; }
	[[nodiscard]] quint32 cursorMode() const { return this->mCursorMode; }
	void setPersistMode(quint32 mode) { this->mPersistMode = mode; }
	[[nodiscard]] quint32 persistMode() const { return this->mPersistMode; }

	/// Looks up an active session by its registered object path, or nullptr.
	static ScreenCastSession* find(const QString& sessionHandlePath);

	/// Tear down the session: unregister the adaptor and schedule deletion.
	/// Safe to call multiple times.
	void close();

signals:
	/// Emitted exactly once, just before the session is deleted. Consumers
	/// (the impl adaptor) use this to drop dangling pointers in pending
	/// reply maps.
	void closing(ScreenCastSession* self);

private slots:
	void onCallerLost(const QString& service);

private:
	QDBusObjectPath mSessionHandle;
	QString mAppId;
	QString mSenderUniqueName;
	ScreenCastSessionImpl* mAdaptor = nullptr;
	QDBusServiceWatcher* mWatcher = nullptr;
	bool mClosed = false;
	QStringList mSelectedSourceIds;
	quint32 mCursorMode = 0;
	quint32 mPersistMode = 0;

	/// Map<object-path, ScreenCastSession*>. Used so SelectSources/Start can
	/// resolve the session by handle, and so Session.Close can find its
	/// owning ScreenCastSession via the adaptor.
	static QHash<QString, ScreenCastSession*>& registry();
};

/// `org.freedesktop.impl.portal.Session` adaptor — the per-session bus
/// surface. xdg-desktop-portal calls Close on this object path when the
/// caller is done; the spec also requires us to emit Closed (a signal,
/// not implemented here yet — front-end mostly drives close itself).
class ScreenCastSessionImpl: public QDBusAbstractAdaptor {
	Q_OBJECT;
	Q_CLASSINFO("D-Bus Interface", "org.freedesktop.impl.portal.Session");

	Q_PROPERTY(quint32 version READ version CONSTANT);

public:
	explicit ScreenCastSessionImpl(QObject* parent, ScreenCastSession* session);

	[[nodiscard]] quint32 version() const { return 1; }

public slots:
	void Close();

signals:
	void Closed();

private:
	ScreenCastSession* session;
};

} // namespace qs::service::portal
