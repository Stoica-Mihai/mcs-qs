#include "screencast_stream.hpp"

#include <cstdint>
#include <vector>

#include <linux/dma-buf.h>
#include <private/qwaylandshmbackingstore_p.h>
#include <qimage.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qobject.h>
#include <qpointer.h>
#include <qscreen.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "../../core/logcat.hpp"
#include "../../core/qmlscreen.hpp"
#include "../../wayland/buffer/dmabuf.hpp"
#include "../../wayland/buffer/manager.hpp"
#include "../../wayland/buffer/shm.hpp"
#include "../../wayland/screencopy/manager.hpp"
#include "../pipewire/screencast_stream.hpp"
#include "qml.hpp"

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

	// Borrow a long-lived screencopy context from the portal singleton.
	// Reusing across sessions for the same screen avoids the wl_buffer
	// teardown race that would otherwise crash the shell on rapid
	// session re-creation.
	this->mCapture = ScreenCastPortal::instance()->getOrCreateScreencopy(
	    this->mScreen, this->mPaintCursors
	);
	if (this->mCapture == nullptr) {
		emit this->failed(QStringLiteral("no screencopy backend supports this source"));
		return;
	}

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

	if (this->mPw != nullptr && this->mPw->isReady()) {
		// shm: direct pointer into the mapped backing store.
		if (const auto* shmBuf = dynamic_cast<const wayland::buffer::shm::WlShmBuffer*>(front)) {
			if (auto* image = shmBuf->image(); image != nullptr && !image->isNull()) {
				this->mPw->pushFrame(image->constBits(),
				                     static_cast<int>(image->bytesPerLine()));
			}
		} else if (const auto* dmaBuf =
		               dynamic_cast<const wayland::buffer::dmabuf::WlDmaBuffer*>(front)) {
			// dmabuf: mmap the first plane (single-plane formats only —
			// good enough for ARGB/BGRA/RGBA which is what screencopy
			// produces). True zero-copy via dmabuf-export to PW is a
			// follow-up; this gets us off QS_DISABLE_DMABUF=1.
			this->pushDmabufFrame(dmaBuf);
		}
	}

	// Loop — request the next frame at whatever rate the screencopy
	// backend supports. The PW consumer paces us via dropped frames if
	// it's slower than capture.
	this->mCapture->captureFrame();
}

void ScreenCastStream::pushDmabufFrame(const wayland::buffer::dmabuf::WlDmaBuffer* dmaBuf) {
	if (dmaBuf == nullptr || dmaBuf->planes_() < 1) return;

	auto fd = dmaBuf->planeFd(0);
	auto stride = dmaBuf->planeStride(0);
	auto offset = dmaBuf->planeOffset(0);
	auto rows = static_cast<int>(dmaBuf->size().height());
	auto totalBytes = stride * static_cast<uint32_t>(rows);

	// dma-buf needs explicit cache-sync ioctls around CPU reads when the
	// producer wrote with a GPU. Skip if the kernel/driver doesn't expose
	// the ioctl — most consumer GPUs do.
	dma_buf_sync sync_start { DMA_BUF_SYNC_START | DMA_BUF_SYNC_READ };
	ioctl(fd, DMA_BUF_IOCTL_SYNC, &sync_start); // best-effort

	auto* mapped = static_cast<uint8_t*>(
	    mmap(nullptr, offset + totalBytes, PROT_READ, MAP_SHARED, fd, 0)
	);
	if (mapped == MAP_FAILED) {
		dma_buf_sync sync_end { DMA_BUF_SYNC_END | DMA_BUF_SYNC_READ };
		ioctl(fd, DMA_BUF_IOCTL_SYNC, &sync_end);
		static bool warned = false;
		if (!warned) { warned = true; qCWarning(logScStream) << "dmabuf mmap failed"; }
		return;
	}

	this->mPw->pushFrame(mapped + offset, static_cast<int>(stride));

	munmap(mapped, offset + totalBytes);
	dma_buf_sync sync_end { DMA_BUF_SYNC_END | DMA_BUF_SYNC_READ };
	ioctl(fd, DMA_BUF_IOCTL_SYNC, &sync_end);
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
