#pragma once

#include <qobject.h>
#include <qqmlintegration.h>
#include <qtmetamacros.h>
#include <qtypes.h>

#include "../dbus/properties.hpp"
#include "dbus_device.h"

namespace qs::bluetooth {

///! Connection state of a Bluetooth device.
class BluetoothDeviceState: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;

public:
	enum Enum : quint8 {
		/// The device is not connected.
		Disconnected = 0,
		/// The device is connected.
		Connected = 1,
		/// The device is disconnecting.
		Disconnecting = 2,
		/// The device is connecting.
		Connecting = 3,
	};
	Q_ENUM(Enum);

	Q_INVOKABLE static QString toString(BluetoothDeviceState::Enum state);
};

struct BatteryPercentage {};

} // namespace qs::bluetooth

namespace qs::dbus {

template <>
struct DBusDataTransform<qs::bluetooth::BatteryPercentage> {
	using Wire = quint8;
	using Data = qreal;
	static DBusResult<Data> fromWire(Wire percentage);
};

} // namespace qs::dbus

namespace qs::bluetooth {

class BluetoothAdapter;

///! A tracked Bluetooth device.
class BluetoothDevice: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_UNCREATABLE("");
	// clang-format off
	/// MAC address of the device.
	Q_PROPERTY(QString address READ default NOTIFY addressChanged BINDABLE bindableAddress);
	/// The name of the Bluetooth device. This property may be written to create an alias, or set to
	/// an empty string to fall back to the device provided name.
	///
	/// See @@deviceName for the name provided by the device.
	Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged);
	/// The name of the Bluetooth device, ignoring user provided aliases. See also @@name
	/// which returns a user provided alias if set.
	Q_PROPERTY(QString deviceName READ default NOTIFY deviceNameChanged BINDABLE bindableDeviceName);
	/// System icon representing the device type. Use @@Quickshell.Quickshell.iconPath() to display this in an image.
	Q_PROPERTY(QString icon READ default NOTIFY iconChanged BINDABLE bindableIcon);
	/// Connection state of the device.
	Q_PROPERTY(BluetoothDeviceState::Enum state READ default NOTIFY stateChanged BINDABLE bindableState);
	/// True if the device is currently connected to the computer.
	///
	/// Setting this property is equivalent to calling @@connect() and @@disconnect().
	///
	/// > [!NOTE] @@state provides more detailed information if required.
	Q_PROPERTY(bool connected READ connected WRITE setConnected NOTIFY connectedChanged);
	/// True if the device is paired to the computer.
	///
	/// > [!NOTE] @@pair() can be used to pair a device, however you must @@forget() the device to unpair it.
	Q_PROPERTY(bool paired READ default NOTIFY pairedChanged BINDABLE bindablePaired);
	/// True if pairing information is stored for future connections.
	Q_PROPERTY(bool bonded READ default NOTIFY bondedChanged BINDABLE bindableBonded);
	/// True if the device is currently being paired.
	///
	/// > [!NOTE] @@cancelPair() can be used to cancel the pairing process.
	Q_PROPERTY(bool pairing READ pairing NOTIFY pairingChanged);
	/// True if the device is considered to be trusted by the system.
	/// Trusted devices are allowed to reconnect themselves to the system without intervention.
	Q_PROPERTY(bool trusted READ trusted WRITE setTrusted NOTIFY trustedChanged);
	/// True if the device is blocked from connecting.
	/// If a device is blocked, any connection attempts will be immediately rejected by the system.
	Q_PROPERTY(bool blocked READ blocked WRITE setBlocked NOTIFY blockedChanged);
	/// True if the device is allowed to wake up the host system from suspend.
	Q_PROPERTY(bool wakeAllowed READ wakeAllowed WRITE setWakeAllowed NOTIFY wakeAllowedChanged);
	/// True if the connected device reports its battery level. Battery level can be accessed via @@battery.
	Q_PROPERTY(bool batteryAvailable READ batteryAvailable NOTIFY batteryAvailableChanged);
	/// Battery level of the connected device, from `0.0` to `1.0`. Only valid if @@batteryAvailable is true.
	Q_PROPERTY(qreal battery READ default NOTIFY batteryChanged BINDABLE bindableBattery);
	/// Received Signal Strength Indicator from the most recent advertisement, in dBm. 0 if unknown
	/// (typical for paired devices once connected — RSSI is mainly useful during discovery).
	Q_PROPERTY(qint16 rssi READ default NOTIFY rssiChanged BINDABLE bindableRssi);
	/// Advertised transmit power in dBm. 0 if unknown.
	Q_PROPERTY(qint16 txPower READ default NOTIFY txPowerChanged BINDABLE bindableTxPower);
	/// Service UUIDs the device advertises. Useful to tell apart audio (A2DP/HFP), input (HID),
	/// and other profile types without round-tripping through the icon string.
	Q_PROPERTY(QStringList uuids READ default NOTIFY uuidsChanged BINDABLE bindableUuids);
	/// Raw 24-bit Bluetooth Class-of-Device value. 0 if unknown.
	Q_PROPERTY(quint32 bluetoothClass READ default NOTIFY bluetoothClassChanged BINDABLE bindableBluetoothClass);
	/// BLE appearance value (0 if unknown). See the Bluetooth assigned numbers spec for decoded categories.
	Q_PROPERTY(quint16 appearance READ default NOTIFY appearanceChanged BINDABLE bindableAppearance);
	/// Linux modalias string (e.g. `usb:v1234p5678d0001`), if the device exposes one.
	Q_PROPERTY(QString modalias READ default NOTIFY modaliasChanged BINDABLE bindableModalias);
	/// True if the device only supports legacy pairing (no SSP).
	Q_PROPERTY(bool legacyPairing READ default NOTIFY legacyPairingChanged BINDABLE bindableLegacyPairing);
	/// True once GATT/SDP service discovery has completed for this connection.
	Q_PROPERTY(bool servicesResolved READ default NOTIFY servicesResolvedChanged BINDABLE bindableServicesResolved);
	/// The Bluetooth adapter this device belongs to.
	Q_PROPERTY(BluetoothAdapter* adapter READ adapter NOTIFY adapterChanged);
	/// DBus path of the device under the `org.bluez` system service.
	Q_PROPERTY(QString dbusPath READ path CONSTANT);
	// clang-format on

public:
	explicit BluetoothDevice(const QString& path, QObject* parent = nullptr);

	/// Attempt to connect to the device.
	Q_INVOKABLE void connect();
	/// Disconnect from the device.
	Q_INVOKABLE void disconnect();
	/// Attempt to pair the device.
	///
	/// > [!NOTE] @@paired and @@pairing return the current pairing status of the device.
	Q_INVOKABLE void pair();
	/// Cancel an active pairing attempt.
	Q_INVOKABLE void cancelPair();
	/// Forget the device.
	Q_INVOKABLE void forget();

	[[nodiscard]] bool isValid() const { return this->mInterface && this->mInterface->isValid(); }
	[[nodiscard]] QString path() const {
		return this->mInterface ? this->mInterface->path() : QString();
	}

	[[nodiscard]] bool batteryAvailable() const { return this->mBatteryInterface != nullptr; }
	[[nodiscard]] BluetoothAdapter* adapter() const;
	[[nodiscard]] QDBusObjectPath adapterPath() const { return this->bAdapterPath.value(); }

	[[nodiscard]] bool connected() const { return this->bConnected; }
	void setConnected(bool connected);

	[[nodiscard]] bool trusted() const { return this->bTrusted; }
	void setTrusted(bool trusted);

	[[nodiscard]] bool blocked() const { return this->bBlocked; }
	void setBlocked(bool blocked);

	[[nodiscard]] QString name() const { return this->bName; }
	void setName(const QString& name);

	[[nodiscard]] bool wakeAllowed() const { return this->bWakeAllowed; }
	void setWakeAllowed(bool wakeAllowed);

	[[nodiscard]] bool pairing() const { return this->bPairing; }

	[[nodiscard]] QBindable<QString> bindableAddress() { return &this->bAddress; }
	[[nodiscard]] QBindable<QString> bindableDeviceName() { return &this->bDeviceName; }
	[[nodiscard]] QBindable<QString> bindableName() { return &this->bName; }
	[[nodiscard]] QBindable<bool> bindableConnected() { return &this->bConnected; }
	[[nodiscard]] QBindable<bool> bindablePaired() { return &this->bPaired; }
	[[nodiscard]] QBindable<bool> bindableBonded() { return &this->bBonded; }
	[[nodiscard]] QBindable<bool> bindableTrusted() { return &this->bTrusted; }
	[[nodiscard]] QBindable<bool> bindableBlocked() { return &this->bBlocked; }
	[[nodiscard]] QBindable<bool> bindableWakeAllowed() { return &this->bWakeAllowed; }
	[[nodiscard]] QBindable<QString> bindableIcon() { return &this->bIcon; }
	[[nodiscard]] QBindable<qreal> bindableBattery() { return &this->bBattery; }
	[[nodiscard]] QBindable<BluetoothDeviceState::Enum> bindableState() { return &this->bState; }
	[[nodiscard]] QBindable<qint16> bindableRssi() { return &this->bRssi; }
	[[nodiscard]] QBindable<qint16> bindableTxPower() { return &this->bTxPower; }
	[[nodiscard]] QBindable<QStringList> bindableUuids() { return &this->bUuids; }
	[[nodiscard]] QBindable<quint32> bindableBluetoothClass() { return &this->bBluetoothClass; }
	[[nodiscard]] QBindable<quint16> bindableAppearance() { return &this->bAppearance; }
	[[nodiscard]] QBindable<QString> bindableModalias() { return &this->bModalias; }
	[[nodiscard]] QBindable<bool> bindableLegacyPairing() { return &this->bLegacyPairing; }
	[[nodiscard]] QBindable<bool> bindableServicesResolved() { return &this->bServicesResolved; }

	void addInterface(const QString& interface, const QVariantMap& properties);
	void removeInterface(const QString& interface);

signals:
	void addressChanged();
	void deviceNameChanged();
	void nameChanged();
	void connectedChanged();
	void stateChanged();
	void pairedChanged();
	void bondedChanged();
	void pairingChanged();
	void trustedChanged();
	void blockedChanged();
	void wakeAllowedChanged();
	void iconChanged();
	void batteryAvailableChanged();
	void batteryChanged();
	void adapterChanged();
	void rssiChanged();
	void txPowerChanged();
	void uuidsChanged();
	void bluetoothClassChanged();
	void appearanceChanged();
	void modaliasChanged();
	void legacyPairingChanged();
	void servicesResolvedChanged();

private:
	void onConnectedChanged();

	DBusBluezDeviceInterface* mInterface = nullptr;
	QDBusInterface* mBatteryInterface = nullptr;

	// clang-format off
	Q_OBJECT_BINDABLE_PROPERTY(BluetoothDevice, QString, bAddress, &BluetoothDevice::addressChanged);
	Q_OBJECT_BINDABLE_PROPERTY(BluetoothDevice, QString, bDeviceName, &BluetoothDevice::deviceNameChanged);
	Q_OBJECT_BINDABLE_PROPERTY(BluetoothDevice, QString, bName, &BluetoothDevice::nameChanged);
	Q_OBJECT_BINDABLE_PROPERTY(BluetoothDevice, bool, bConnected, &BluetoothDevice::onConnectedChanged);
	Q_OBJECT_BINDABLE_PROPERTY(BluetoothDevice, bool, bPaired, &BluetoothDevice::pairedChanged);
	Q_OBJECT_BINDABLE_PROPERTY(BluetoothDevice, bool, bBonded, &BluetoothDevice::bondedChanged);
	Q_OBJECT_BINDABLE_PROPERTY(BluetoothDevice, bool, bTrusted, &BluetoothDevice::trustedChanged);
	Q_OBJECT_BINDABLE_PROPERTY(BluetoothDevice, bool, bBlocked, &BluetoothDevice::blockedChanged);
	Q_OBJECT_BINDABLE_PROPERTY(BluetoothDevice, bool, bWakeAllowed, &BluetoothDevice::wakeAllowedChanged);
	Q_OBJECT_BINDABLE_PROPERTY(BluetoothDevice, QString, bIcon, &BluetoothDevice::iconChanged);
	Q_OBJECT_BINDABLE_PROPERTY(BluetoothDevice, QDBusObjectPath, bAdapterPath);
	Q_OBJECT_BINDABLE_PROPERTY(BluetoothDevice, qreal, bBattery, &BluetoothDevice::batteryChanged);
	Q_OBJECT_BINDABLE_PROPERTY(BluetoothDevice, BluetoothDeviceState::Enum, bState, &BluetoothDevice::stateChanged);
	Q_OBJECT_BINDABLE_PROPERTY(BluetoothDevice, bool, bPairing, &BluetoothDevice::pairingChanged);
	Q_OBJECT_BINDABLE_PROPERTY(BluetoothDevice, qint16, bRssi, &BluetoothDevice::rssiChanged);
	Q_OBJECT_BINDABLE_PROPERTY(BluetoothDevice, qint16, bTxPower, &BluetoothDevice::txPowerChanged);
	Q_OBJECT_BINDABLE_PROPERTY(BluetoothDevice, QStringList, bUuids, &BluetoothDevice::uuidsChanged);
	Q_OBJECT_BINDABLE_PROPERTY(BluetoothDevice, quint32, bBluetoothClass, &BluetoothDevice::bluetoothClassChanged);
	Q_OBJECT_BINDABLE_PROPERTY(BluetoothDevice, quint16, bAppearance, &BluetoothDevice::appearanceChanged);
	Q_OBJECT_BINDABLE_PROPERTY(BluetoothDevice, QString, bModalias, &BluetoothDevice::modaliasChanged);
	Q_OBJECT_BINDABLE_PROPERTY(BluetoothDevice, bool, bLegacyPairing, &BluetoothDevice::legacyPairingChanged);
	Q_OBJECT_BINDABLE_PROPERTY(BluetoothDevice, bool, bServicesResolved, &BluetoothDevice::servicesResolvedChanged);

	QS_DBUS_BINDABLE_PROPERTY_GROUP(BluetoothDevice, properties);
	QS_DBUS_PROPERTY_BINDING(BluetoothDevice, pAddress, bAddress, properties, "Address");
	QS_DBUS_PROPERTY_BINDING(BluetoothDevice, pDeviceName, bDeviceName, properties, "Name");
	QS_DBUS_PROPERTY_BINDING(BluetoothDevice, pName, bName, properties, "Alias");
	QS_DBUS_PROPERTY_BINDING(BluetoothDevice, pConnected, bConnected, properties, "Connected");
	QS_DBUS_PROPERTY_BINDING(BluetoothDevice, pPaired, bPaired, properties, "Paired");
	QS_DBUS_PROPERTY_BINDING(BluetoothDevice, pBonded, bBonded, properties, "Bonded");
	QS_DBUS_PROPERTY_BINDING(BluetoothDevice, pTrusted, bTrusted, properties, "Trusted");
	QS_DBUS_PROPERTY_BINDING(BluetoothDevice, pBlocked, bBlocked, properties, "Blocked");
	QS_DBUS_PROPERTY_BINDING(BluetoothDevice, pWakeAllowed, bWakeAllowed, properties, "WakeAllowed");
	QS_DBUS_PROPERTY_BINDING(BluetoothDevice, pIcon, bIcon, properties, "Icon");
	QS_DBUS_PROPERTY_BINDING(BluetoothDevice, pAdapterPath, bAdapterPath, properties, "Adapter");
	// BlueZ exposes RSSI / TxPower / Appearance / Modalias only when relevant
	// (RSSI / TxPower require active discovery; Appearance is BLE-only;
	// Modalias is set by some devices, omitted by others). Mark optional so
	// reading them on devices that don't advertise them doesn't log warnings.
	QS_DBUS_PROPERTY_BINDING(BluetoothDevice, pRssi, bRssi, properties, "RSSI", false);
	QS_DBUS_PROPERTY_BINDING(BluetoothDevice, pTxPower, bTxPower, properties, "TxPower", false);
	QS_DBUS_PROPERTY_BINDING(BluetoothDevice, pUuids, bUuids, properties, "UUIDs");
	QS_DBUS_PROPERTY_BINDING(BluetoothDevice, pBluetoothClass, bBluetoothClass, properties, "Class");
	QS_DBUS_PROPERTY_BINDING(BluetoothDevice, pAppearance, bAppearance, properties, "Appearance", false);
	QS_DBUS_PROPERTY_BINDING(BluetoothDevice, pModalias, bModalias, properties, "Modalias", false);
	QS_DBUS_PROPERTY_BINDING(BluetoothDevice, pLegacyPairing, bLegacyPairing, properties, "LegacyPairing");
	QS_DBUS_PROPERTY_BINDING(BluetoothDevice, pServicesResolved, bServicesResolved, properties, "ServicesResolved");

	QS_DBUS_BINDABLE_PROPERTY_GROUP(BluetoothDevice, batteryProperties);
	QS_DBUS_PROPERTY_BINDING(BluetoothDevice, BatteryPercentage, pBattery, bBattery, batteryProperties, "Percentage", true);
	// clang-format on
};

} // namespace qs::bluetooth

QDebug operator<<(QDebug debug, const qs::bluetooth::BluetoothDevice* device);
