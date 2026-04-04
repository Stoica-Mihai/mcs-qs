#include "proto.hpp"

#include <qlogging.h>
#include <qloggingcategory.h>
#include <qtypes.h>
#include <qwayland-wlr-gamma-control-unstable-v1.h>
#include <qwaylandclientextension.h>

#include "../../core/logcat.hpp"

namespace qs::wayland::gamma_control {
QS_LOGGING_CATEGORY(logGammaControl, "quickshell.wayland.gamma_control", QtWarningMsg);
}

namespace qs::wayland::gamma_control::impl {

GammaControlManager::GammaControlManager(): QWaylandClientExtensionTemplate(1) {
	this->initialize();
}

GammaControlManager* GammaControlManager::instance() {
	static auto* instance = new GammaControlManager(); // NOLINT
	return instance->isInitialized() ? instance : nullptr;
}

GammaControl::GammaControl(::zwlr_gamma_control_v1* control)
    : QtWayland::zwlr_gamma_control_v1(control) {}

GammaControl::~GammaControl() {
	if (this->isInitialized()) this->destroy();
}

void GammaControl::setGamma(int fd) {
	this->set_gamma(fd);
}

void GammaControl::zwlr_gamma_control_v1_gamma_size(uint32_t size) {
	this->mGammaSize = size;
	qCDebug(logGammaControl) << "Gamma ramp size:" << size;
	emit this->gammaSizeReceived(size);
}

void GammaControl::zwlr_gamma_control_v1_failed() {
	emit this->failed();
}

} // namespace qs::wayland::gamma_control::impl
