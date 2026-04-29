#include "agent.hpp"

#include <qdbusconnection.h>
#include <qdbusextratypes.h>
#include <qdbusmessage.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qobject.h>
#include <qstring.h>
#include <qtypes.h>
#include <qvariant.h>

#include "../core/logcat.hpp"

namespace qs::bluetooth {

namespace {
QS_LOGGING_CATEGORY(logAgent, "quickshell.bluetooth.agent", QtWarningMsg);

constexpr auto AGENT_PATH = "/com/mcshell/BluetoothAgent";
constexpr auto BLUEZ_REJECTED = "org.bluez.Error.Rejected";
constexpr auto BLUEZ_CANCELED = "org.bluez.Error.Canceled";
} // namespace

// ── PairingRequest ────────────────────────────────────────────────

PairingRequest::PairingRequest(
    QObject* parent,
    QString kind,
    QString devicePath,
    QDBusMessage message
)
    : QObject(parent)
    , mKind(std::move(kind))
    , mDevicePath(std::move(devicePath))
    , mMessage(std::move(message)) {}

void PairingRequest::markAnswered() {
	if (this->mAnswered) return;
	this->mAnswered = true;
	emit this->answeredChanged();
}

void PairingRequest::respondString(const QString& value) {
	if (this->mAnswered) return;
	auto reply = this->mMessage.createReply(QVariant(value));
	QDBusConnection::systemBus().send(reply);
	this->markAnswered();
}

void PairingRequest::respondUint(quint32 value) {
	if (this->mAnswered) return;
	auto reply = this->mMessage.createReply(QVariant(value));
	QDBusConnection::systemBus().send(reply);
	this->markAnswered();
}

void PairingRequest::approve() {
	if (this->mAnswered) return;
	auto reply = this->mMessage.createReply();
	QDBusConnection::systemBus().send(reply);
	this->markAnswered();
}

void PairingRequest::reject() {
	if (this->mAnswered) return;
	auto reply = this->mMessage.createErrorReply(BLUEZ_REJECTED, "User rejected pairing");
	QDBusConnection::systemBus().send(reply);
	this->markAnswered();
}

void PairingRequest::cancelByUser() {
	if (this->mAnswered) return;
	auto reply = this->mMessage.createErrorReply(BLUEZ_CANCELED, "User canceled pairing");
	QDBusConnection::systemBus().send(reply);
	this->markAnswered();
}

// ── BluetoothAgent ────────────────────────────────────────────────

BluetoothAgent::BluetoothAgent(QObject* parent): QObject(parent) {}

QString BluetoothAgent::agentPath() const { return AGENT_PATH; }

void BluetoothAgent::setRegistered(bool registered) {
	if (this->mRegistered == registered) return;
	this->mRegistered = registered;
	emit this->registeredChanged();
}

PairingRequest*
BluetoothAgent::createRequest(const QString& kind, const QDBusObjectPath& device) {
	if (this->mActive != nullptr) {
		// BlueZ should serialize Agent1 calls per-device, but if we somehow
		// see a second request before the first was answered, cancel the
		// stale one to keep the bus from hanging on the orphan.
		this->mActive->cancelByUser();
		this->mActive->deleteLater();
	}
	auto* req = new PairingRequest(this, kind, device.path(), this->message());
	this->mActive = req;
	this->setDelayedReply(true);
	QObject::connect(req, &PairingRequest::answeredChanged, this, [this, req]() {
		if (this->mActive == req) this->mActive = nullptr;
		req->deleteLater();
	});
	return req;
}

void BluetoothAgent::Release() {
	qCInfo(logAgent) << "BlueZ released our agent";
	this->setRegistered(false);
}

QString BluetoothAgent::RequestPinCode(const QDBusObjectPath& device) {
	emit this->requestReceived(this->createRequest("pinCode", device));
	return {}; // delayed reply
}

void BluetoothAgent::DisplayPinCode(const QDBusObjectPath& device, const QString& pincode) {
	auto* req = this->createRequest("displayPinCode", device);
	req->setPinCode(pincode);
	emit this->requestReceived(req);
	// Display methods don't expect a reply value, but they DO need the
	// empty success acknowledgement. Send it now and immediately clear
	// the request — the UI just observes pinCode for the duration the
	// device is being paired.
	req->approve();
}

quint32 BluetoothAgent::RequestPasskey(const QDBusObjectPath& device) {
	emit this->requestReceived(this->createRequest("passkey", device));
	return 0; // delayed reply
}

void BluetoothAgent::DisplayPasskey(
    const QDBusObjectPath& device,
    quint32 passkey,
    quint16 entered
) {
	auto* req = this->createRequest("displayPasskey", device);
	req->setPasskey(passkey);
	req->setEntered(entered);
	emit this->requestReceived(req);
	req->approve();
}

void BluetoothAgent::RequestConfirmation(const QDBusObjectPath& device, quint32 passkey) {
	auto* req = this->createRequest("confirmation", device);
	req->setPasskey(passkey);
	emit this->requestReceived(req);
}

void BluetoothAgent::RequestAuthorization(const QDBusObjectPath& device) {
	emit this->requestReceived(this->createRequest("authorization", device));
}

void BluetoothAgent::AuthorizeService(const QDBusObjectPath& device, const QString& uuid) {
	auto* req = this->createRequest("serviceAuthorization", device);
	req->setServiceUuid(uuid);
	emit this->requestReceived(req);
}

void BluetoothAgent::Cancel() {
	if (this->mActive != nullptr) this->mActive->cancelByUser();
	emit this->cancelled();
}

} // namespace qs::bluetooth
