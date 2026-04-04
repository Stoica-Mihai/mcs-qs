#pragma once

#include <qobject.h>
#include <qtclasshelpermacros.h>
#include <qtmetamacros.h>
#include <qtypes.h>
#include <qwayland-wlr-gamma-control-unstable-v1.h>
#include <qwaylandclientextension.h>
#include <wayland-wlr-gamma-control-unstable-v1-client-protocol.h>

#include "../../core/logcat.hpp"

namespace qs::wayland::gamma_control {
QS_DECLARE_LOGGING_CATEGORY(logGammaControl);
}

namespace qs::wayland::gamma_control::impl {

class GammaControlManager
    : public QWaylandClientExtensionTemplate<GammaControlManager>
    , public QtWayland::zwlr_gamma_control_manager_v1 {
public:
	explicit GammaControlManager();
	static GammaControlManager* instance();
};

class GammaControl
    : public QObject
    , public QtWayland::zwlr_gamma_control_v1 {
	Q_OBJECT;

public:
	explicit GammaControl(::zwlr_gamma_control_v1* control);
	~GammaControl() override;
	Q_DISABLE_COPY_MOVE(GammaControl);

	[[nodiscard]] quint32 gammaSize() const { return mGammaSize; }
	void setGamma(int fd);

signals:
	void gammaSizeReceived(quint32 size);
	void failed();

protected:
	void zwlr_gamma_control_v1_gamma_size(uint32_t size) override;
	void zwlr_gamma_control_v1_failed() override;

private:
	quint32 mGammaSize = 0;
};

} // namespace qs::wayland::gamma_control::impl
