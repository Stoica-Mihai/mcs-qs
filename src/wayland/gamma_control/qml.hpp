#pragma once

#include <qlist.h>
#include <qobject.h>
#include <qqmlintegration.h>
#include <qtclasshelpermacros.h>
#include <qtmetamacros.h>
#include <qtypes.h>

#include "proto.hpp"

namespace qs::wayland::gamma_control {

///! Control display color temperature via gamma ramps.
/// Sets the color temperature for all outputs using the
/// zwlr-gamma-control-unstable-v1 Wayland protocol.
///
/// > [!NOTE] Requires compositor support for [wlr-gamma-control-unstable-v1].
///
/// [wlr-gamma-control-unstable-v1]: https://wayland.app/protocols/wlr-gamma-control-unstable-v1
class NightLight: public QObject {
	Q_OBJECT;
	QML_NAMED_ELEMENT(NightLight);
	QML_SINGLETON;

	/// The color temperature in Kelvin. 6500 = daylight, lower = warmer.
	/// Clamped to [1000, 6500]. Setting to 6500 restores neutral gamma.
	Q_PROPERTY(quint32 temperature READ temperature WRITE setTemperature NOTIFY temperatureChanged);
	/// Whether the compositor supports the gamma control protocol.
	Q_PROPERTY(bool available READ available CONSTANT);

public:
	explicit NightLight(QObject* parent = nullptr);

	[[nodiscard]] quint32 temperature() const { return mTemperature; }
	void setTemperature(quint32 temp);

	[[nodiscard]] bool available() const { return mManager != nullptr; }

signals:
	void temperatureChanged();

private:
	void applyGamma();
	void acquireOutputs();
	void releaseOutputs();

	static void temperatureToRgb(quint32 kelvin, double& r, double& g, double& b);

	struct OutputControl {
		impl::GammaControl* control = nullptr;
		quint32 gammaSize = 0;
	};

	impl::GammaControlManager* mManager = nullptr;
	QList<OutputControl> mOutputs;
	quint32 mTemperature = 6500;
	bool mAcquired = false;
};

} // namespace qs::wayland::gamma_control
