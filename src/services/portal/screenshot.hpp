#pragma once

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

class ScreenshotPortal;

///! A pending xdg-desktop-portal Screenshot request.
/// xdg-desktop-portal forwards an app's screenshot request to whichever
/// backend implements `org.freedesktop.impl.portal.Screenshot`. mcs-qs is
/// that backend; this object wraps the in-flight call so the QML side can
/// respond with either a captured file URI, an interactive cancel, or an
/// error. The backing D-Bus reply is held back via QDBusContext until one
/// of the response invokables is called.
class ScreenshotPortalRequest: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_UNCREATABLE("ScreenshotPortalRequests are emitted by ScreenshotPortal");

	// clang-format off
	/// app_id of the requesting application (may be empty for non-Flatpak apps).
	Q_PROPERTY(QString appId READ appId CONSTANT);
	/// Parent-window identifier supplied by the requester. Format is
	/// compositor-specific (`wayland:<handle>` or `x11:<xid>`); usually
	/// safe to ignore unless we want to anchor the prompt.
	Q_PROPERTY(QString parentWindow READ parentWindow CONSTANT);
	/// True if the app asked for interactive UI (preview / region selection).
	/// When false, the spec says we should capture immediately without UI.
	Q_PROPERTY(bool interactive READ interactive CONSTANT);
	/// True if the app asked for the prompt to be modal.
	Q_PROPERTY(bool modal READ modal CONSTANT);
	/// True after one of the respond/cancel/fail invokables has been called.
	Q_PROPERTY(bool answered READ answered NOTIFY answeredChanged);
	// clang-format on

public:
	ScreenshotPortalRequest(
	    QObject* parent,
	    QString appId,
	    QString parentWindow,
	    bool interactive,
	    bool modal,
	    QDBusMessage message
	);

	[[nodiscard]] QString appId() const { return this->mAppId; }
	[[nodiscard]] QString parentWindow() const { return this->mParentWindow; }
	[[nodiscard]] bool interactive() const { return this->mInteractive; }
	[[nodiscard]] bool modal() const { return this->mModal; }
	[[nodiscard]] bool answered() const { return this->mAnswered; }

	/// Reply with success and the file URI of the captured screenshot.
	/// `fileUri` should be a `file://` URL pointing at a readable file.
	Q_INVOKABLE void respondWithFile(const QString& fileUri);
	/// Reply with the user-cancelled response code (1).
	Q_INVOKABLE void cancel();
	/// Reply with the generic-error response code (2).
	Q_INVOKABLE void fail();

signals:
	void answeredChanged();

private:
	QString mAppId;
	QString mParentWindow;
	bool mInteractive;
	bool mModal;
	QDBusMessage mMessage;
	bool mAnswered = false;

	void sendResponse(quint32 response, const QString& fileUri);
};

class ScreenshotImpl
    : public QObject
    , public QDBusContext {
	Q_OBJECT;
	Q_CLASSINFO("D-Bus Interface", "org.freedesktop.impl.portal.Screenshot");

	Q_PROPERTY(quint32 version READ version CONSTANT);

public:
	explicit ScreenshotImpl(ScreenshotPortal* parent);

	[[nodiscard]] quint32 version() const { return 2; }

public slots:
	void Screenshot(
	    const QDBusObjectPath& handle,
	    const QString& app_id,
	    const QString& parent_window,
	    const QVariantMap& options,
	    quint32& response,
	    QVariantMap& results
	);

	void PickColor(
	    const QDBusObjectPath& handle,
	    const QString& app_id,
	    const QString& parent_window,
	    const QVariantMap& options,
	    quint32& response,
	    QVariantMap& results
	);

private:
	ScreenshotPortal* portal;
};

} // namespace qs::service::portal
