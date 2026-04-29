#include "wallpaper.hpp"

#include <qdbusconnection.h>
#include <qdbusextratypes.h>
#include <qdbusmessage.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qobject.h>
#include <qstring.h>
#include <qtypes.h>
#include <qvariant.h>

#include "../../core/logcat.hpp"
#include "backend.hpp"
#include "qml.hpp"

namespace qs::service::portal {

namespace {
QS_LOGGING_CATEGORY(logWallpaperPortal, "quickshell.service.portal.wallpaper", QtWarningMsg);
}

WallpaperPortalRequest::WallpaperPortalRequest(
    QObject* parent,
    QString appId,
    QString parentWindow,
    QString uri,
    QString setOn,
    bool showPreview,
    QDBusMessage message
)
    : QObject(parent)
    , mAppId(std::move(appId))
    , mParentWindow(std::move(parentWindow))
    , mUri(std::move(uri))
    , mSetOn(std::move(setOn))
    , mShowPreview(showPreview)
    , mMessage(std::move(message)) {}

void WallpaperPortalRequest::sendResponse(quint32 response) {
	if (this->mAnswered) return;
	auto reply = this->mMessage.createReply(QVariant(response));
	QDBusConnection::sessionBus().send(reply);
	this->mAnswered = true;
	emit this->answeredChanged();
}

void WallpaperPortalRequest::approve() { this->sendResponse(0); }
void WallpaperPortalRequest::cancel() { this->sendResponse(1); }
void WallpaperPortalRequest::fail() { this->sendResponse(2); }

WallpaperImpl::WallpaperImpl(QObject* parent, WallpaperPortal* portal)
    : QDBusAbstractAdaptor(parent), portal(portal) {}

void WallpaperImpl::SetWallpaperURI(
    const QDBusObjectPath& /*handle*/,
    const QString& app_id,
    const QString& parent_window,
    const QString& uri,
    const QVariantMap& options,
    quint32& /*response*/
) {
	this->setDelayedReply(true);

	auto setOn = options.value("set-on").toString();
	if (setOn.isEmpty()) setOn = "background";
	auto showPreview = options.value("show-preview").toBool();

	auto* req = new WallpaperPortalRequest(
	    this->portal, app_id, parent_window, uri, setOn, showPreview, this->message()
	);
	QObject::connect(
	    req,
	    &WallpaperPortalRequest::answeredChanged,
	    req,
	    &QObject::deleteLater
	);
	emit this->portal->wallpaperRequested(req);
}

WallpaperPortal::WallpaperPortal(QObject* parent): QObject(parent) {
	this->impl = new WallpaperImpl(PortalBackend::instance(), this);
}

} // namespace qs::service::portal
