#include "screencast_stream.hpp"

#include <private/qwaylandshmbackingstore_p.h>
#include <qimage.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qobject.h>
#include <qpointer.h>
#include <qscreen.h>

#include "../../core/logcat.hpp"
#include "../../core/qmlscreen.hpp"
#include "../../wayland/buffer/manager.hpp"
#include "../../wayland/buffer/shm.hpp"
#include "../../wayland/screencopy/manager.hpp"
#include "../pipewire/screencast_stream.hpp"

namespace qs::service::portal {

namespace {
QS_LOGGING_CATEGORY(logScStream, "quickshell.service.portal.screencast", QtWarningMsg);
}

ScreenCastStream::ScreenCastStream(SourceKind kind, QScreen* screen, bool paintCursors,
                                   QObject* parent)
    : QObject(parent)
    , mKind(kind)
    , mScreen(screen)
    , mPaintCursors(paintCursors) {}

ScreenCastStream::~ScreenCastStream() = default;

bool ScreenCastStream::isReady() const {
	return this->mPw != nullptr && this->mPw->isReady();
}

quint32 ScreenCastStream::nodeId() const {
	return this->mPw != nullptr ? this->mPw->nodeId() : 0;
}

void ScreenCastStream::start() {
	if (this->mScreen.isNull()) {
		emit this->failed(QStringLiteral("source screen no longer available"));
		return;
	}

	// Wrap the QScreen in a QuickshellScreenInfo so ScreencopyManager's
	// dispatch can recognise it (the manager keys on the QObject type).
	auto* info = new QuickshellScreenInfo(this, this->mScreen);
	this->mCapture = wayland::screencopy::ScreencopyManager::createContext(info, this->mPaintCursors);
	if (this->mCapture == nullptr) {
		emit this->failed(QStringLiteral("no screencopy backend supports this source"));
		return;
	}
	this->mCapture->setParent(this);

	QObject::connect(this->mCapture, &wayland::screencopy::ScreencopyContext::frameCaptured,
	                 this, &ScreenCastStream::onFrameCaptured);
	QObject::connect(this->mCapture, &wayland::screencopy::ScreencopyContext::stopped,
	                 this, &ScreenCastStream::onCaptureStopped);

	// Kick off — first frame tells us the source dimensions, which we
	// then use to size the PipeWire stream.
	this->mCapture->captureFrame();
}

void ScreenCastStream::stop() {
	// Disconnect signals so any in-flight captureFrame() callback doesn't
	// reach back into us after we've decided we're done. The QObject
	// children themselves are torn down by parent-child deletion when
	// the owning session goes away — explicit deletes here race with
	// pending Wayland protocol events on those buffers.
	if (this->mCapture != nullptr) {
		QObject::disconnect(this->mCapture, nullptr, this, nullptr);
	}
	if (this->mPw != nullptr) {
		QObject::disconnect(this->mPw, nullptr, this, nullptr);
		this->mPw->setEnabled(false);
	}
}

void ScreenCastStream::initPwStream() {
	this->mPw = new pipewire::PwScreenCastStream(this);
	this->mPw->setTestMode(false);
	this->mPw->setWidth(this->mWidth);
	this->mPw->setHeight(this->mHeight);
	this->mPw->setFramerate(60);

	QObject::connect(this->mPw, &pipewire::PwScreenCastStream::readyChanged,
	                 this, &ScreenCastStream::onPwReadyChanged);
	QObject::connect(this->mPw, &pipewire::PwScreenCastStream::failed,
	                 this, &ScreenCastStream::onPwFailed);

	this->mPw->setEnabled(true);
}

void ScreenCastStream::onFrameCaptured() {
	if (this->mCapture == nullptr) return;
	const auto* front = this->mCapture->swapchain().frontbuffer();
	if (front == nullptr) {
		this->mCapture->captureFrame();
		return;
	}

	auto size = front->size();
	if (front->transform.flipSize()) size.transpose();

	if (this->mPw == nullptr) {
		this->mWidth = size.width();
		this->mHeight = size.height();
		this->initPwStream();
	}

	// Pull pixels out of the shm front buffer and shove them at the PW
	// stream. dmabuf path is step 7 — without QS_DISABLE_DMABUF=1 the
	// screencopy backend will hand back a dmabuf and the cast fails the
	// type check below. Explicit warning so the limitation is visible.
	const auto* shmBuf = dynamic_cast<const wayland::buffer::shm::WlShmBuffer*>(front);
	if (shmBuf == nullptr) {
		static bool warned = false;
		if (!warned) {
			warned = true;
			qCWarning(logScStream) << "frontbuffer is not shm — set QS_DISABLE_DMABUF=1 "
			                          "in the shell's environment until step 7 lands";
		}
	} else if (this->mPw != nullptr && this->mPw->isReady()) {
		auto* image = shmBuf->image();
		if (image != nullptr && !image->isNull()) {
			this->mPw->pushFrame(image->constBits(), static_cast<int>(image->bytesPerLine()));
		}
	}

	// Loop — request the next frame at whatever rate the screencopy
	// backend supports. The PW consumer paces us via dropped frames if
	// it's slower than capture.
	this->mCapture->captureFrame();
}

void ScreenCastStream::onCaptureStopped() {
	qCInfo(logScStream) << "screencopy stopped, ScreenCastStream tearing down";
	emit this->failed(QStringLiteral("screencopy stopped"));
}

void ScreenCastStream::onPwReadyChanged() {
	if (this->mPw == nullptr || !this->mPw->isReady()) return;
	if (this->mReadyEmitted) return;
	this->mReadyEmitted = true;
	qCInfo(logScStream) << "stream ready, node id =" << this->mPw->nodeId();
	emit this->readyForCaller();
}

void ScreenCastStream::onPwFailed(const QString& reason) {
	qCWarning(logScStream) << "PW stream failed:" << reason;
	emit this->failed(reason);
}

} // namespace qs::service::portal
