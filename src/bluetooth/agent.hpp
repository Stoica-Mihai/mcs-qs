#pragma once

#include <qdbuscontext.h>
#include <qdbusextratypes.h>
#include <qdbusmessage.h>
#include <qobject.h>
#include <qqmlintegration.h>
#include <qstring.h>
#include <qtmetamacros.h>
#include <qtypes.h>

namespace qs::bluetooth {

class BluetoothAgent;

///! A pending pairing prompt from BlueZ.
/// When BlueZ asks our registered agent to confirm, prompt for, or display a
/// passkey/PIN, an instance of this class is created and emitted via
/// @@BluetoothAgent.requestReceived. Consumers are expected to either render
/// a UI for the user (PIN entry, confirm passkey, etc.) and call the
/// matching `respond*` / `approve` / `reject` invokable, or `reject()` if
/// no UI is available — leaving requests unanswered will block the pairing
/// flow until BlueZ times out.
class PairingRequest: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_UNCREATABLE("PairingRequests are emitted by BluetoothAgent");

	// clang-format off
	/// Kind of request — drives which respond method is appropriate:
	///   pinCode            → respondString(<digits>)
	///   passkey            → respondUint(<6-digit passkey>)
	///   confirmation       → approve() / reject()  (passkey carries the value to confirm)
	///   authorization      → approve() / reject()
	///   serviceAuthorization → approve() / reject()  (uuid carries the service UUID)
	///   displayPinCode     → no response — UI just shows the value
	///   displayPasskey     → no response — UI just shows the value
	Q_PROPERTY(QString kind READ kind CONSTANT);
	/// Object path of the BlueZ device the request relates to.
	Q_PROPERTY(QString devicePath READ devicePath CONSTANT);
	/// Numeric passkey associated with this request, or 0 if none.
	Q_PROPERTY(quint32 passkey READ passkey CONSTANT);
	/// PIN code string for displayPinCode, or empty.
	Q_PROPERTY(QString pinCode READ pinCode CONSTANT);
	/// Service UUID for serviceAuthorization, or empty.
	Q_PROPERTY(QString serviceUuid READ serviceUuid CONSTANT);
	/// Number of digits already entered (DisplayPasskey only).
	Q_PROPERTY(quint16 entered READ entered CONSTANT);
	/// True after one of the respond/approve/reject methods has been called.
	Q_PROPERTY(bool answered READ answered NOTIFY answeredChanged);
	// clang-format on

public:
	explicit PairingRequest(
	    QObject* parent,
	    QString kind,
	    QString devicePath,
	    QDBusMessage message
	);

	[[nodiscard]] QString kind() const { return this->mKind; }
	[[nodiscard]] QString devicePath() const { return this->mDevicePath; }
	[[nodiscard]] quint32 passkey() const { return this->mPasskey; }
	[[nodiscard]] QString pinCode() const { return this->mPinCode; }
	[[nodiscard]] QString serviceUuid() const { return this->mServiceUuid; }
	[[nodiscard]] quint16 entered() const { return this->mEntered; }
	[[nodiscard]] bool answered() const { return this->mAnswered; }

	void setPasskey(quint32 v) { this->mPasskey = v; }
	void setPinCode(const QString& v) { this->mPinCode = v; }
	void setServiceUuid(const QString& v) { this->mServiceUuid = v; }
	void setEntered(quint16 v) { this->mEntered = v; }

	/// Reply with a string (use for `pinCode`).
	Q_INVOKABLE void respondString(const QString& value);
	/// Reply with a numeric passkey (use for `passkey`).
	Q_INVOKABLE void respondUint(quint32 value);
	/// Empty success reply — for `confirmation` / `authorization` / `serviceAuthorization`.
	Q_INVOKABLE void approve();
	/// Reject the request with `org.bluez.Error.Rejected`. Valid for any
	/// reply-expecting kind. No-op for display-only kinds.
	Q_INVOKABLE void reject();
	/// Cancel the request with `org.bluez.Error.Canceled`. Identical to
	/// reject() except for the error name.
	Q_INVOKABLE void cancelByUser();

signals:
	void answeredChanged();

private:
	QString mKind;
	QString mDevicePath;
	QDBusMessage mMessage;
	quint32 mPasskey = 0;
	QString mPinCode;
	QString mServiceUuid;
	quint16 mEntered = 0;
	bool mAnswered = false;

	void markAnswered();
};

///! BlueZ pairing agent.
/// mcs-qs registers a single agent with BlueZ on the `KeyboardDisplay`
/// capability so consumers can render their own pairing prompts instead of
/// having BlueZ fall back to its no-input agent (which silently rejects
/// secret-required pairings).
///
/// Listen for @@requestReceived and call the appropriate respond/approve/reject
/// method on the supplied @@PairingRequest. If no consumer is connected to
/// the signal, the agent automatically rejects the request after the default
/// timeout to keep BlueZ from hanging.
class BluetoothAgent
    : public QObject
    , public QDBusContext {
	Q_OBJECT;
	Q_CLASSINFO("D-Bus Interface", "org.bluez.Agent1");
	QML_ELEMENT;
	QML_UNCREATABLE("BluetoothAgent is exposed via Bluetooth.pairingAgent");

	/// Path the agent is registered at on the bus. Constant.
	Q_PROPERTY(QString agentPath READ agentPath CONSTANT);
	/// True once the agent has been registered with BlueZ.
	Q_PROPERTY(bool registered READ registered NOTIFY registeredChanged);

public:
	explicit BluetoothAgent(QObject* parent = nullptr);

	[[nodiscard]] QString agentPath() const;
	[[nodiscard]] bool registered() const { return this->mRegistered; }

	void setRegistered(bool registered);

public slots:
	// Agent1 wire methods — names + signatures match the BlueZ spec.
	void Release();
	QString RequestPinCode(const QDBusObjectPath& device);
	void DisplayPinCode(const QDBusObjectPath& device, const QString& pincode);
	quint32 RequestPasskey(const QDBusObjectPath& device);
	void DisplayPasskey(const QDBusObjectPath& device, quint32 passkey, quint16 entered);
	void RequestConfirmation(const QDBusObjectPath& device, quint32 passkey);
	void RequestAuthorization(const QDBusObjectPath& device);
	void AuthorizeService(const QDBusObjectPath& device, const QString& uuid);
	void Cancel();

signals:
	void registeredChanged();
	/// Emitted whenever BlueZ asks us to prompt the user. The supplied
	/// @@PairingRequest must be answered (respond / approve / reject) to
	/// keep the pairing state machine moving.
	void requestReceived(qs::bluetooth::PairingRequest* request);
	/// Emitted when BlueZ tells us a previously-emitted request is no
	/// longer relevant (e.g. the device timed out). The active
	/// @@PairingRequest, if any, is auto-cancelled before this fires.
	void cancelled();

private:
	PairingRequest* createRequest(const QString& kind, const QDBusObjectPath& device);

	bool mRegistered = false;
	PairingRequest* mActive = nullptr;
};

} // namespace qs::bluetooth
