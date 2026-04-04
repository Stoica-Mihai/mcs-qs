#include "qml.hpp"

#include <algorithm>
#include <cmath>

#include <qguiapplication.h>
#include <qlogging.h>
#include <qscreen.h>
#include <qtypes.h>
#include <sys/mman.h>
#include <unistd.h>

#include <private/qwaylandintegration_p.h>
#include <private/qwaylandscreen_p.h>

#include "proto.hpp"

namespace qs::wayland::gamma_control {

NightLight::NightLight(QObject* parent): QObject(parent) {
	mManager = impl::GammaControlManager::instance();
	if (!mManager) {
		qCWarning(logGammaControl) << "zwlr-gamma-control-v1 not supported by compositor.";
	}
	// Don't acquire gamma control yet — only when temperature != 6500.
	// The protocol is exclusive: acquiring blocks other gamma clients.
}

void NightLight::acquireOutputs() {
	if (mAcquired || !mManager) return;

	const auto screens = QGuiApplication::screens();
	for (auto* screen : screens) {
		auto* waylandScreen = dynamic_cast<QtWaylandClient::QWaylandScreen*>(screen->handle());
		if (!waylandScreen) continue;

		auto* rawControl = mManager->get_gamma_control(waylandScreen->output());
		auto* control = new impl::GammaControl(rawControl);
		control->setParent(this);

		auto& output = mOutputs.emplaceBack();
		output.control = control;

		QObject::connect(control, &impl::GammaControl::gammaSizeReceived, this,
		    [this, idx = mOutputs.size() - 1](quint32 size) {
			    mOutputs[idx].gammaSize = size;
			    if (mTemperature != 6500) applyGamma();
		    }
		);

		QObject::connect(control, &impl::GammaControl::failed, this,
		    [this, idx = mOutputs.size() - 1]() {
			    mOutputs[idx].control = nullptr;
			    for (const auto& o : mOutputs) {
				    if (o.control) return;
			    }
			    qCInfo(logGammaControl) << "Night light unavailable"
			                            << "(driver limitation or another client holds the lock)";
		    }
		);
	}

	mAcquired = true;
	qCDebug(logGammaControl) << "Gamma control acquired for" << mOutputs.size() << "outputs";
}

void NightLight::releaseOutputs() {
	if (!mAcquired) return;

	// Destroying the gamma control objects restores original gamma
	for (auto& output : mOutputs) {
		delete output.control;
	}
	mOutputs.clear();
	mAcquired = false;

	qCDebug(logGammaControl) << "Gamma control released";
}

void NightLight::setTemperature(quint32 temp) {
	temp = std::clamp(temp, quint32(1000), quint32(6500));
	if (temp == mTemperature) return;
	mTemperature = temp;

	if (temp == 6500) {
		// Neutral — release gamma control so other clients can use it
		releaseOutputs();
	} else {
		// Need gamma control
		if (!mAcquired) acquireOutputs();
		applyGamma();
	}

	emit temperatureChanged();
}

void NightLight::temperatureToRgb(quint32 kelvin, double& r, double& g, double& b) {
	// Tanner Helland's algorithm (used by Redshift, wl-gammarelay-rs, f.lux)
	auto t = static_cast<double>(kelvin) / 100.0;

	if (t <= 66.0) {
		r = 1.0;
		g = std::clamp(0.39008157876901960784 * std::log(t) - 0.63184144378862745098, 0.0, 1.0);
	} else {
		r = std::clamp(1.29293618606274509804 * std::pow(t - 60.0, -0.1332047592), 0.0, 1.0);
		g = std::clamp(1.12989086089529411765 * std::pow(t - 60.0, -0.0755148492), 0.0, 1.0);
	}

	if (t >= 66.0) {
		b = 1.0;
	} else if (t <= 19.0) {
		b = 0.0;
	} else {
		b = std::clamp(0.54320678911019607843 * std::log(t - 10.0) - 1.19625408914, 0.0, 1.0);
	}
}

void NightLight::applyGamma() {
	double r = 1.0;
	double g = 1.0;
	double b = 1.0;
	temperatureToRgb(mTemperature, r, g, b);

	for (auto& output : mOutputs) {
		if (!output.control || output.gammaSize == 0) continue;

		auto rampBytes = output.gammaSize * 3 * sizeof(uint16_t);

		int fd = memfd_create("gamma-ramp", MFD_CLOEXEC | MFD_ALLOW_SEALING);
		if (fd < 0) continue;

		if (ftruncate(fd, static_cast<off_t>(rampBytes)) != 0) {
			::close(fd);
			continue;
		}

		auto* data = static_cast<uint16_t*>(
		    mmap(nullptr, rampBytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0)
		);

		if (data == MAP_FAILED) { // NOLINT
			::close(fd);
			continue;
		}

		auto size = output.gammaSize;
		auto* rampR = data;
		auto* rampG = data + size;
		auto* rampB = data + size * 2;

		for (quint32 i = 0; i < size; i++) {
			auto linear = static_cast<double>(i) / static_cast<double>(size - 1);
			rampR[i] = static_cast<uint16_t>(std::clamp(linear * r * 65535.0, 0.0, 65535.0));
			rampG[i] = static_cast<uint16_t>(std::clamp(linear * g * 65535.0, 0.0, 65535.0));
			rampB[i] = static_cast<uint16_t>(std::clamp(linear * b * 65535.0, 0.0, 65535.0));
		}

		munmap(data, rampBytes);
		output.control->setGamma(fd);
		::close(fd);
	}

	qCDebug(logGammaControl) << "Applied gamma for temperature" << mTemperature << "K";
}

} // namespace qs::wayland::gamma_control
