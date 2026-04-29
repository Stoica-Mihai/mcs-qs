#pragma once

#include <qdbusabstractadaptor.h>
#include <qdbuscontext.h>
#include <qdbusextratypes.h>
#include <qdbusmessage.h>
#include <qobject.h>
#include <qqmlintegration.h>
#include <qstring.h>
#include <qtmetamacros.h>
#include <qtypes.h>
#include <qvariant.h>

namespace qs::service::portal {

class WallpaperPortal;

///! A pending xdg-desktop-portal Wallpaper request.
class WallpaperPortalRequest: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_UNCREATABLE("WallpaperPortalRequests are emitted by WallpaperPortal");

	// clang-format off
	Q_PROPERTY(QString appId READ appId CONSTANT);
	Q_PROPERTY(QString parentWindow READ parentWindow CONSTANT);
	/// `file://` URI the requester wants set as wallpaper.
	Q_PROPERTY(QString uri READ uri CONSTANT);
	/// Where to apply: "background", "lockscreen", or "both".
	Q_PROPERTY(QString setOn READ setOn CONSTANT);
	/// True if the requester asked us to preview before applying.
	Q_PROPERTY(bool showPreview READ showPreview CONSTANT);
	Q_PROPERTY(bool answered READ answered NOTIFY answeredChanged);
	// clang-format on

public:
	WallpaperPortalRequest(
	    QObject* parent,
	    QString appId,
	    QString parentWindow,
	    QString uri,
	    QString setOn,
	    bool showPreview,
	    QDBusMessage message
	);

	[[nodiscard]] QString appId() const { return this->mAppId; }
	[[nodiscard]] QString parentWindow() const { return this->mParentWindow; }
	[[nodiscard]] QString uri() const { return this->mUri; }
	[[nodiscard]] QString setOn() const { return this->mSetOn; }
	[[nodiscard]] bool showPreview() const { return this->mShowPreview; }
	[[nodiscard]] bool answered() const { return this->mAnswered; }

	/// Reply with success after the wallpaper has been applied.
	Q_INVOKABLE void approve();
	/// Reply with the user-cancelled response code.
	Q_INVOKABLE void cancel();
	/// Reply with the generic-error response code.
	Q_INVOKABLE void fail();

signals:
	void answeredChanged();

private:
	QString mAppId;
	QString mParentWindow;
	QString mUri;
	QString mSetOn;
	bool mShowPreview;
	QDBusMessage mMessage;
	bool mAnswered = false;

	void sendResponse(quint32 response);
};

class WallpaperImpl
    : public QDBusAbstractAdaptor
    , public QDBusContext {
	Q_OBJECT;
	Q_CLASSINFO("D-Bus Interface", "org.freedesktop.impl.portal.Wallpaper");

	Q_PROPERTY(quint32 version READ version CONSTANT);

public:
	explicit WallpaperImpl(QObject* parent, WallpaperPortal* portal);

	[[nodiscard]] quint32 version() const { return 1; }

public slots:
	void SetWallpaperURI(
	    const QDBusObjectPath& handle,
	    const QString& app_id,
	    const QString& parent_window,
	    const QString& uri,
	    const QVariantMap& options,
	    quint32& response
	);

private:
	WallpaperPortal* portal;
};

} // namespace qs::service::portal
