#include "proto.hpp"

#include <qlogging.h>
#include <qloggingcategory.h>
#include <qstring.h>
#include <qtypes.h>
#include <qwayland-ext-data-control-v1.h>
#include <qwaylandclientextension.h>
#include <wayland-ext-data-control-v1-client-protocol.h>

#include "../../core/logcat.hpp"

namespace qs::wayland::data_control {
QS_LOGGING_CATEGORY(logDataControl, "quickshell.wayland.data_control", QtWarningMsg);
}

namespace qs::wayland::data_control::impl {

// ── DataControlManager ──────────────────────────────

DataControlManager::DataControlManager(): QWaylandClientExtensionTemplate(1) {
	this->initialize();
}

DataControlManager* DataControlManager::instance() {
	static auto* instance = new DataControlManager(); // NOLINT
	return instance->isInitialized() ? instance : nullptr;
}

// ── DataControlDevice ───────────────────────────────

DataControlDevice::DataControlDevice(::ext_data_control_device_v1* device)
    : QtWayland::ext_data_control_device_v1(device) {}

DataControlDevice::~DataControlDevice() {
	delete this->mPendingOffer;
	if (this->isInitialized()) this->destroy();
}

void DataControlDevice::ext_data_control_device_v1_data_offer(
    ::ext_data_control_offer_v1* id
) {
	delete this->mPendingOffer;
	this->mPendingOffer = new DataControlOffer(id);
	qCDebug(logDataControl) << "New data offer created";
}

void DataControlDevice::ext_data_control_device_v1_selection(
    ::ext_data_control_offer_v1* id
) {
	auto* offer = this->mPendingOffer;
	this->mPendingOffer = nullptr;

	// id is null when clipboard is cleared
	if (!id) {
		delete offer;
		offer = nullptr;
	}

	qCDebug(logDataControl) << "Selection changed, offer:" << offer
	                        << "mimes:" << (offer ? offer->mimeTypes() : QStringList());
	emit this->selectionChanged(offer);
}

void DataControlDevice::ext_data_control_device_v1_primary_selection(
    ::ext_data_control_offer_v1* /*id*/
) {
	// Primary selection not used — ignore
}

void DataControlDevice::ext_data_control_device_v1_finished() {
	qCDebug(logDataControl) << "Device finished";
	emit this->finished();
}

// ── DataControlOffer ────────────────────────────────

DataControlOffer::DataControlOffer(::ext_data_control_offer_v1* offer)
    : QtWayland::ext_data_control_offer_v1(offer) {}

DataControlOffer::~DataControlOffer() {
	if (this->isInitialized()) this->destroy();
}

void DataControlOffer::receive(const QString& mimeType, int fd) {
	this->ext_data_control_offer_v1::receive(mimeType, fd);
}

void DataControlOffer::ext_data_control_offer_v1_offer(const QString& mimeType) {
	this->mMimeTypes.append(mimeType);
}

} // namespace qs::wayland::data_control::impl
