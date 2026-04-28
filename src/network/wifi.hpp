#pragma once

#include <qobject.h>
#include <qproperty.h>
#include <qqmlintegration.h>
#include <qtmetamacros.h>
#include <qtypes.h>

#include "../core/doc.hpp"
#include "../core/model.hpp"
#include "device.hpp"
#include "enums.hpp"
#include "network.hpp"

namespace qs::network {

///! WiFi subtype of @@Network.
class WifiNetwork: public Network {
	Q_OBJECT;
	QML_ELEMENT;
	QML_UNCREATABLE("WifiNetwork can only be acquired through WifiDevice");
	// clang-format off
	/// The current signal strength of the network, from 0.0 to 1.0.
	Q_PROPERTY(qreal signalStrength READ default NOTIFY signalStrengthChanged BINDABLE bindableSignalStrength);
	/// The security type of the wifi network.
	Q_PROPERTY(WifiSecurityType::Enum security READ default NOTIFY securityChanged BINDABLE bindableSecurity);
	/// Radio frequency of the strongest known access point for this network, in MHz. 0 if unknown.
	/// 2400-2495 is the 2.4 GHz band; 5150-5895 is 5 GHz; 5945-7125 is 6 GHz.
	Q_PROPERTY(quint32 frequency READ default NOTIFY frequencyChanged BINDABLE bindableFrequency);
	/// MAC address (BSSID) of the strongest known access point for this network. Empty if unknown.
	Q_PROPERTY(QString bssid READ default NOTIFY bssidChanged BINDABLE bindableBssid);
	/// Maximum bitrate the strongest known access point advertises, in kbit/s. 0 if unknown.
	Q_PROPERTY(quint32 maxBitrate READ default NOTIFY maxBitrateChanged BINDABLE bindableMaxBitrate);
	// clang-format on

public:
	explicit WifiNetwork(QString ssid, QObject* parent = nullptr);
	/// Attempt to connect to the network with the given PSK. If the PSK is wrong,
	/// a @@Network.connectionFailed(s) signal will be emitted with `NoSecrets`.
	///
	/// The networking backend may store the PSK for future use with @@Network.connect().
	/// As such, calling that function first is recommended to avoid having to show a
	/// prompt if not required.
	///
	/// > [!NOTE] PSKs should only be provided when the @@security is one of
	/// > `WpaPsk`, `Wpa2Psk`, or `Sae`.
	Q_INVOKABLE void connectWithPsk(const QString& psk);

	QBindable<qreal> bindableSignalStrength() { return &this->bSignalStrength; }
	QBindable<WifiSecurityType::Enum> bindableSecurity() { return &this->bSecurity; }
	QBindable<quint32> bindableFrequency() { return &this->bFrequency; }
	QBindable<QString> bindableBssid() { return &this->bBssid; }
	QBindable<quint32> bindableMaxBitrate() { return &this->bMaxBitrate; }

signals:
	QSDOC_HIDE void requestConnectWithPsk(QString psk);
	void signalStrengthChanged();
	void securityChanged();
	void frequencyChanged();
	void bssidChanged();
	void maxBitrateChanged();

private:
	// clang-format off
	Q_OBJECT_BINDABLE_PROPERTY(WifiNetwork, qreal, bSignalStrength, &WifiNetwork::signalStrengthChanged);
	Q_OBJECT_BINDABLE_PROPERTY(WifiNetwork, WifiSecurityType::Enum, bSecurity, &WifiNetwork::securityChanged);
	Q_OBJECT_BINDABLE_PROPERTY(WifiNetwork, quint32, bFrequency, &WifiNetwork::frequencyChanged);
	Q_OBJECT_BINDABLE_PROPERTY(WifiNetwork, QString, bBssid, &WifiNetwork::bssidChanged);
	Q_OBJECT_BINDABLE_PROPERTY(WifiNetwork, quint32, bMaxBitrate, &WifiNetwork::maxBitrateChanged);
	// clang-format on
};

///! WiFi variant of a @@NetworkDevice.
class WifiDevice: public NetworkDevice {
	Q_OBJECT;
	QML_ELEMENT;
	QML_UNCREATABLE("");

	// clang-format off
	/// A list of this available or connected wifi networks.
	QSDOC_TYPE_OVERRIDE(ObjectModel<WifiNetwork>*);
	Q_PROPERTY(UntypedObjectModel* networks READ networks CONSTANT);
	/// True when currently scanning for networks.
	/// When enabled, the scanner populates the device with an active list of available wifi networks.
	Q_PROPERTY(bool scannerEnabled READ scannerEnabled WRITE setScannerEnabled NOTIFY scannerEnabledChanged BINDABLE bindableScannerEnabled);
	/// The 802.11 mode the device is in.
	Q_PROPERTY(WifiDeviceMode::Enum mode READ default NOTIFY modeChanged BINDABLE bindableMode);
	/// Current negotiated link bitrate in kbit/s, or 0 when not connected.
	Q_PROPERTY(quint32 bitrate READ default NOTIFY bitrateChanged BINDABLE bindableBitrate);
	// clang-format on

public:
	explicit WifiDevice(QObject* parent = nullptr);

	void networkAdded(WifiNetwork* net);
	void networkRemoved(WifiNetwork* net);

	[[nodiscard]] ObjectModel<WifiNetwork>* networks() { return &this->mNetworks; }
	QBindable<bool> bindableScannerEnabled() { return &this->bScannerEnabled; }
	[[nodiscard]] bool scannerEnabled() const { return this->bScannerEnabled; }
	void setScannerEnabled(bool enabled);
	QBindable<WifiDeviceMode::Enum> bindableMode() { return &this->bMode; }
	QBindable<quint32> bindableBitrate() { return &this->bBitrate; }

signals:
	void modeChanged();
	void scannerEnabledChanged(bool enabled);
	void bitrateChanged();

private:
	ObjectModel<WifiNetwork> mNetworks {this};
	Q_OBJECT_BINDABLE_PROPERTY(WifiDevice, bool, bScannerEnabled, &WifiDevice::scannerEnabledChanged);
	Q_OBJECT_BINDABLE_PROPERTY(WifiDevice, WifiDeviceMode::Enum, bMode, &WifiDevice::modeChanged);
	Q_OBJECT_BINDABLE_PROPERTY(WifiDevice, quint32, bBitrate, &WifiDevice::bitrateChanged);
};

}; // namespace qs::network

QDebug operator<<(QDebug debug, const qs::network::WifiNetwork* network);
QDebug operator<<(QDebug debug, const qs::network::WifiDevice* device);
