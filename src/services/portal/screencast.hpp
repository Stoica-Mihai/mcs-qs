#pragma once

#include <qdbusabstractadaptor.h>
#include <qdbusextratypes.h>
#include <qobject.h>
#include <qstring.h>
#include <qtmetamacros.h>
#include <qtypes.h>
#include <qvariant.h>

namespace qs::service::portal {

class ScreenCastPortal;

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
