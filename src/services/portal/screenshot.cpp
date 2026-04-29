#include "screenshot.hpp"

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
QS_LOGGING_CATEGORY(logPortal, "quickshell.service.portal", QtWarningMsg);
}

// ── ScreenshotPortalRequest ──────────────────────────────────────

ScreenshotPortalRequest::ScreenshotPortalRequest(
    QObject* parent,
    QString appId,
    QString parentWindow,
    bool interactive,
    bool modal,
    QDBusMessage message
)
    : QObject(parent)
    , mAppId(std::move(appId))
    , mParentWindow(std::move(parentWindow))
    , mInteractive(interactive)
    , mModal(modal)
    , mMessage(std::move(message)) {}

void ScreenshotPortalRequest::sendResponse(quint32 response, const QString& fileUri) {
	if (this->mAnswered) return;
	QVariantMap results;
	if (response == 0 && !fileUri.isEmpty()) results.insert("uri", fileUri);

	auto reply =
	    this->mMessage.createReply({QVariant(response), QVariant::fromValue(results)});
	QDBusConnection::sessionBus().send(reply);

	this->mAnswered = true;
	emit this->answeredChanged();
}

void ScreenshotPortalRequest::respondWithFile(const QString& fileUri) {
	this->sendResponse(0, fileUri);
}

void ScreenshotPortalRequest::cancel() { this->sendResponse(1, {}); }
void ScreenshotPortalRequest::fail() { this->sendResponse(2, {}); }

// ── Screenshot interface ─────────────────────────────────────────

ScreenshotImpl::ScreenshotImpl(QObject* parent, ScreenshotPortal* portal)
    : QDBusAbstractAdaptor(parent), portal(portal) {}

void ScreenshotImpl::Screenshot(
    const QDBusObjectPath& /*handle*/,
    const QString& app_id,
    const QString& parent_window,
    const QVariantMap& options,
    quint32& /*response*/,
    QVariantMap& /*results*/
) {
	this->setDelayedReply(true);
	auto interactive = options.value("interactive").toBool();
	auto modal = options.value("modal").toBool();

	auto* req = new ScreenshotPortalRequest(
	    this->portal, app_id, parent_window, interactive, modal, this->message()
	);
	QObject::connect(
	    req,
	    &ScreenshotPortalRequest::answeredChanged,
	    req,
	    &QObject::deleteLater
	);
	emit this->portal->requestReceived(req);
}

void ScreenshotImpl::PickColor(
    const QDBusObjectPath& /*handle*/,
    const QString& /*app_id*/,
    const QString& /*parent_window*/,
    const QVariantMap& /*options*/,
    quint32& response,
    QVariantMap& results
) {
	// Not implemented yet — return generic error so the portal frontend
	// reports failure to the caller instead of hanging.
	response = 2;
	results = {};
	qCInfo(logPortal) << "PickColor not implemented; returning failure";
}

// ── ScreenshotPortal singleton ───────────────────────────────────

ScreenshotPortal::ScreenshotPortal(QObject* parent): QObject(parent) {
	// Adaptor attaches to the shared backend host so all impl-portal
	// interfaces share a single registerObject at /org/freedesktop/portal/desktop.
	this->impl = new ScreenshotImpl(PortalBackend::instance(), this);
}

} // namespace qs::service::portal
