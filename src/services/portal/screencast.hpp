#pragma once

#include <qdbusabstractadaptor.h>
#include <qdbusextratypes.h>
#include <qdbusmessage.h>
#include <qlist.h>
#include <qobject.h>
#include <qpointer.h>
#include <qqmlintegration.h>
#include <qstring.h>
#include <qtmetamacros.h>
#include <qtypes.h>
#include <qvariant.h>

namespace qs::service::portal {

class ScreenCastPortal;
class ScreenCastSession;

/// Identifies a single source the user can pick. Wraps either a screen
/// (output capture) or a toplevel (window capture). Region capture lands
/// in step 8 as an extra mode on top of an output pick.
class ScreenCastPickerSource: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_UNCREATABLE("created by ScreenCastPickerRequest");

	Q_PROPERTY(QString id READ id CONSTANT);
	Q_PROPERTY(QString label READ label CONSTANT);
	Q_PROPERTY(quint32 sourceType READ sourceType CONSTANT); // Monitor or Window flag
	Q_PROPERTY(int width READ width CONSTANT);
	Q_PROPERTY(int height READ height CONSTANT);

public:
	ScreenCastPickerSource(
	    QObject* parent,
	    QString id,
	    QString label,
	    quint32 sourceType,
	    int width,
	    int height
	);

	[[nodiscard]] QString id() const { return this->mId; }
	[[nodiscard]] QString label() const { return this->mLabel; }
	[[nodiscard]] quint32 sourceType() const { return this->mSourceType; }
	[[nodiscard]] int width() const { return this->mWidth; }
	[[nodiscard]] int height() const { return this->mHeight; }

private:
	QString mId;
	QString mLabel;
	quint32 mSourceType;
	int mWidth;
	int mHeight;
};

/// In-flight ScreenCast picker prompt. Emitted as the argument of
/// `ScreenCastPortal.pickerRequested`. The QML side reads `availableSources`
/// (and the type/cursor/persist hints), assigns one or more to
/// `selectedSourceIds`, then calls approve()/cancel()/fail() to release
/// the held SelectSources reply.
class ScreenCastPickerRequest: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_UNCREATABLE("emitted by ScreenCastPortal.pickerRequested");

	Q_PROPERTY(QString appId READ appId CONSTANT);
	Q_PROPERTY(QString sessionHandle READ sessionHandle CONSTANT);
	/// Bitfield of allowed source types (Monitor | Window).
	Q_PROPERTY(quint32 sourceTypes READ sourceTypes CONSTANT);
	/// Whether the caller asked for multi-select.
	Q_PROPERTY(bool multiple READ multiple CONSTANT);
	/// Bitfield of allowed cursor modes (Hidden | Embedded | Metadata).
	Q_PROPERTY(quint32 cursorMode READ cursorMode CONSTANT);
	/// 0=no-persist, 1=persist-while-running, 2=persist-permanently.
	Q_PROPERTY(quint32 persistMode READ persistMode CONSTANT);
	/// Caller-supplied restore token from a prior approved session, or "".
	Q_PROPERTY(QString restoreToken READ restoreToken CONSTANT);
	Q_PROPERTY(QList<qs::service::portal::ScreenCastPickerSource*>
	           availableSources READ availableSources CONSTANT);
	Q_PROPERTY(bool answered READ answered NOTIFY answeredChanged);

public:
	ScreenCastPickerRequest(
	    QObject* parent,
	    QString appId,
	    QString sessionHandle,
	    quint32 sourceTypes,
	    bool multiple,
	    quint32 cursorMode,
	    quint32 persistMode,
	    QString restoreToken,
	    QList<ScreenCastPickerSource*> availableSources,
	    QDBusMessage message
	);

	[[nodiscard]] QString appId() const { return this->mAppId; }
	[[nodiscard]] QString sessionHandle() const { return this->mSessionHandle; }
	[[nodiscard]] quint32 sourceTypes() const { return this->mSourceTypes; }
	[[nodiscard]] bool multiple() const { return this->mMultiple; }
	[[nodiscard]] quint32 cursorMode() const { return this->mCursorMode; }
	[[nodiscard]] quint32 persistMode() const { return this->mPersistMode; }
	[[nodiscard]] QString restoreToken() const { return this->mRestoreToken; }
	[[nodiscard]] QList<ScreenCastPickerSource*> availableSources() const {
		return this->mAvailableSources;
	}
	[[nodiscard]] bool answered() const { return this->mAnswered; }

	/// Mark these source ids as the user's selection. Call before approve().
	Q_INVOKABLE void setSelectedSourceIds(const QStringList& ids);
	Q_INVOKABLE void approve();
	Q_INVOKABLE void cancel();
	Q_INVOKABLE void fail();

	[[nodiscard]] QStringList selectedSourceIds() const { return this->mSelectedIds; }

signals:
	void answeredChanged();

private:
	void sendResponse(quint32 response);

	QString mAppId;
	QString mSessionHandle;
	quint32 mSourceTypes;
	bool mMultiple;
	quint32 mCursorMode;
	quint32 mPersistMode;
	QString mRestoreToken;
	QList<ScreenCastPickerSource*> mAvailableSources;
	QStringList mSelectedIds;
	QDBusMessage mMessage;
	bool mAnswered = false;
};

/// Bitfield mirroring the spec's `AvailableSourceTypes`.
namespace ScreenCastSourceType {
constexpr quint32 Monitor = 1 << 0;
constexpr quint32 Window = 1 << 1;
constexpr quint32 Virtual = 1 << 2;
} // namespace ScreenCastSourceType

/// Bitfield mirroring the spec's `AvailableCursorModes`.
namespace ScreenCastCursorMode {
constexpr quint32 Hidden = 1 << 0;
constexpr quint32 Embedded = 1 << 1;
constexpr quint32 Metadata = 1 << 2;
} // namespace ScreenCastCursorMode

/// `org.freedesktop.impl.portal.ScreenCast` adaptor. Skeleton: every slot
/// currently returns response=2 (other-error) so the spec surface is
/// visible (introspectable, slot signatures resolvable) without yet
/// pretending to deliver streams. Real session/stream logic lands in
/// follow-up steps; see PLAN-screencast-portal.md.
class ScreenCastImpl: public QDBusAbstractAdaptor {
	Q_OBJECT;
	Q_CLASSINFO("D-Bus Interface", "org.freedesktop.impl.portal.ScreenCast");

	Q_PROPERTY(quint32 version READ version CONSTANT);
	Q_PROPERTY(quint32 AvailableSourceTypes READ availableSourceTypes CONSTANT);
	Q_PROPERTY(quint32 AvailableCursorModes READ availableCursorModes CONSTANT);

public:
	explicit ScreenCastImpl(QObject* parent, ScreenCastPortal* portal);

	[[nodiscard]] quint32 version() const { return 4; }
	[[nodiscard]] quint32 availableSourceTypes() const {
		return ScreenCastSourceType::Monitor | ScreenCastSourceType::Window;
	}
	[[nodiscard]] quint32 availableCursorModes() const {
		return ScreenCastCursorMode::Hidden | ScreenCastCursorMode::Embedded;
	}

public slots:
	void CreateSession(
	    const QDBusObjectPath& handle,
	    const QDBusObjectPath& session_handle,
	    const QString& app_id,
	    const QVariantMap& options,
	    quint32& response,
	    QVariantMap& results
	);

	void SelectSources(
	    const QDBusObjectPath& handle,
	    const QDBusObjectPath& session_handle,
	    const QString& app_id,
	    const QVariantMap& options,
	    quint32& response,
	    QVariantMap& results
	);

	void Start(
	    const QDBusObjectPath& handle,
	    const QDBusObjectPath& session_handle,
	    const QString& app_id,
	    const QString& parent_window,
	    const QVariantMap& options,
	    quint32& response,
	    QVariantMap& results
	);

private:
	ScreenCastPortal* portal;
};

} // namespace qs::service::portal
