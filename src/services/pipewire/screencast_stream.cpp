#include "screencast_stream.hpp"

#include <array>
#include <cstdint>
#include <cstring>

#include <pipewire/core.h>
#include <pipewire/keys.h>
#include <pipewire/properties.h>
#include <pipewire/stream.h>
#include <qcolor.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qscopeguard.h>
#include <qtypes.h>
#include <spa/buffer/buffer.h>
#include <spa/param/format-utils.h>
#include <spa/param/format.h>
#include <spa/param/param.h>
#include <spa/param/video/format-utils.h>
#include <spa/param/video/raw.h>
#include <spa/pod/builder.h>
#include <spa/pod/pod.h>
#include <spa/utils/defs.h>

#include "../../core/logcat.hpp"
#include "connection.hpp"
#include "core.hpp"

#pragma GCC diagnostic push
#ifdef __clang__
#pragma GCC diagnostic ignored "-Wmissing-designated-field-initializers"
#else
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

namespace qs::service::pipewire {

namespace {
QS_LOGGING_CATEGORY(logScCast, "quickshell.service.pipewire.screencast", QtWarningMsg);
}

// ── PwScreenCastStream ─────────────────────────────────────────────

const pw_stream_events PwScreenCastStream::EVENTS = {
    .version = PW_VERSION_STREAM_EVENTS,
    .state_changed = &PwScreenCastStream::onStateChanged,
    .param_changed = &PwScreenCastStream::onParamChanged,
    .process = &PwScreenCastStream::onProcess,
};

PwScreenCastStream::PwScreenCastStream(QObject* parent)
    : QObject(parent), mTestColor(Qt::red) {}

PwScreenCastStream::~PwScreenCastStream() { this->stop(); }

void PwScreenCastStream::setWidth(int width) {
	if (width <= 0 || this->mWidth == width) return;
	this->mWidth = width;
	emit this->paramsChanged();
	this->rebuildIfRunning();
}

void PwScreenCastStream::setHeight(int height) {
	if (height <= 0 || this->mHeight == height) return;
	this->mHeight = height;
	emit this->paramsChanged();
	this->rebuildIfRunning();
}

void PwScreenCastStream::setFramerate(int rate) {
	if (rate <= 0 || this->mFramerate == rate) return;
	this->mFramerate = rate;
	emit this->paramsChanged();
	this->rebuildIfRunning();
}

void PwScreenCastStream::setEnabled(bool enabled) {
	if (this->mEnabled == enabled) return;
	this->mEnabled = enabled;
	emit this->enabledChanged();
	if (enabled) this->start();
	else this->stop();
}

void PwScreenCastStream::setTestMode(bool enabled) {
	if (this->mTestMode == enabled) return;
	this->mTestMode = enabled;
	emit this->testModeChanged();
}

void PwScreenCastStream::setTestColor(const QColor& color) {
	if (this->mTestColor == color) return;
	this->mTestColor = color;
	emit this->testColorChanged();
}

void PwScreenCastStream::setNodeId(quint32 id) {
	if (this->mNodeId == id) return;
	this->mNodeId = id;
	emit this->nodeIdChanged();
}

void PwScreenCastStream::setReady(bool ready) {
	if (this->mReady == ready) return;
	this->mReady = ready;
	emit this->readyChanged();
}

void PwScreenCastStream::rebuildIfRunning() {
	if (!this->mEnabled || this->mStream == nullptr) return;
	this->stop();
	this->start();
}

bool PwScreenCastStream::start() {
	auto* core = PwConnection::instance()->registry.core;
	if (core == nullptr || !core->isValid()) {
		qCWarning(logScCast) << "PipeWire core not ready, cannot start ScreenCast stream";
		return false;
	}

	// Stream properties — Video/Source produces frames into the graph.
	// PW_KEY_PRIORITY_DRIVER lets the consumer drive timing.
	// clang-format off
	auto* props = pw_properties_new(
	    PW_KEY_MEDIA_CLASS, "Video/Source",
	    PW_KEY_MEDIA_TYPE, "Video",
	    PW_KEY_MEDIA_CATEGORY, "Capture",
	    PW_KEY_MEDIA_ROLE, "Screen",
	    PW_KEY_NODE_NAME, "quickshell-screencast",
	    PW_KEY_NODE_DESCRIPTION, "Quickshell ScreenCast",
	    nullptr
	);
	// clang-format on

	this->mStream = pw_stream_new(core->core, "quickshell-screencast", props);
	if (this->mStream == nullptr) {
		qCWarning(logScCast) << "pw_stream_new failed";
		return false;
	}

	pw_stream_add_listener(this->mStream, &this->mListener.hook, &EVENTS, this);

	// Build EnumFormat pod listing the formats we can produce. Step 3
	// keeps it simple — single fixed BGRA, fixed size, framerate in a
	// 1..maxFramerate range. Step 7 expands to multiple formats and
	// adds dmabuf modifiers.
	auto buf = std::array<quint8, 1024> {};
	auto builder = SPA_POD_BUILDER_INIT(buf.data(), buf.size()); // NOLINT

	auto rawSize = SPA_RECTANGLE(static_cast<uint32_t>(this->mWidth),
	                             static_cast<uint32_t>(this->mHeight));
	auto rateMin = SPA_FRACTION(1, 1);
	auto rateMax = SPA_FRACTION(static_cast<uint32_t>(this->mFramerate), 1);

	const spa_pod* params[1] = {
	    static_cast<const spa_pod*>(spa_pod_builder_add_object(
	        &builder,
	        SPA_TYPE_OBJECT_Format,
	        SPA_PARAM_EnumFormat,
	        SPA_FORMAT_mediaType,
	        SPA_POD_Id(SPA_MEDIA_TYPE_video),
	        SPA_FORMAT_mediaSubtype,
	        SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
	        SPA_FORMAT_VIDEO_format,
	        SPA_POD_CHOICE_ENUM_Id(
	            5,
	            SPA_VIDEO_FORMAT_BGRA,
	            SPA_VIDEO_FORMAT_RGBA,
	            SPA_VIDEO_FORMAT_BGRx,
	            SPA_VIDEO_FORMAT_RGBx,
	            SPA_VIDEO_FORMAT_BGRA
	        ),
	        SPA_FORMAT_VIDEO_size,
	        SPA_POD_Rectangle(&rawSize),
	        SPA_FORMAT_VIDEO_framerate,
	        SPA_POD_CHOICE_RANGE_Fraction(&rateMax, &rateMin, &rateMax)
	    )),
	};

	auto flags = static_cast<pw_stream_flags>(
	    PW_STREAM_FLAG_DRIVER | PW_STREAM_FLAG_ALLOC_BUFFERS | PW_STREAM_FLAG_MAP_BUFFERS
	);
	auto res = pw_stream_connect(this->mStream, PW_DIRECTION_OUTPUT, PW_ID_ANY,
	                             flags, params, 1);
	if (res < 0) {
		qCWarning(logScCast) << "pw_stream_connect failed:" << res;
		this->stop();
		emit this->failed(QStringLiteral("pw_stream_connect failed"));
		return false;
	}

	this->mFrameClock.start();
	qCInfo(logScCast) << "ScreenCast stream connecting"
	                  << this->mWidth << "x" << this->mHeight << "@" << this->mFramerate;
	return true;
}

void PwScreenCastStream::stop() {
	if (this->mStream == nullptr) return;
	this->mListener.remove();
	pw_stream_destroy(this->mStream);
	this->mStream = nullptr;
	this->setReady(false);
	this->setNodeId(0);
}

// ── pw_stream callbacks ────────────────────────────────────────────

void PwScreenCastStream::onStateChanged(
    void* data,
    pw_stream_state /*oldState*/,
    pw_stream_state state,
    const char* error
) {
	static_cast<PwScreenCastStream*>(data)->handleStateChanged(state, error); // NOLINT
}

void PwScreenCastStream::onParamChanged(void* data, uint32_t id, const spa_pod* param) {
	static_cast<PwScreenCastStream*>(data)->handleParamChanged(id, param); // NOLINT
}

void PwScreenCastStream::onProcess(void* data) {
	static_cast<PwScreenCastStream*>(data)->handleProcess(); // NOLINT
}

void PwScreenCastStream::handleStateChanged(pw_stream_state state, const char* error) {
	qCInfo(logScCast) << "stream state →" << pw_stream_state_as_string(state);
	switch (state) {
	case PW_STREAM_STATE_PAUSED:
	case PW_STREAM_STATE_STREAMING: {
		auto id = pw_stream_get_node_id(this->mStream);
		this->setNodeId(id);
		this->setReady(true);
		break;
	}
	case PW_STREAM_STATE_ERROR:
		qCWarning(logScCast) << "stream error:" << (error ? error : "(none)");
		this->setReady(false);
		emit this->failed(error ? QString::fromLocal8Bit(error) : QStringLiteral("stream error"));
		break;
	case PW_STREAM_STATE_UNCONNECTED:
	case PW_STREAM_STATE_CONNECTING:
		this->setReady(false);
		break;
	}
}

void PwScreenCastStream::handleParamChanged(uint32_t id, const spa_pod* param) {
	if (param == nullptr || id != SPA_PARAM_Format) return;

	uint32_t mediaType = 0;
	uint32_t mediaSubtype = 0;
	if (spa_format_parse(param, &mediaType, &mediaSubtype) < 0) return;
	if (mediaType != SPA_MEDIA_TYPE_video || mediaSubtype != SPA_MEDIA_SUBTYPE_raw) return;

	spa_video_info_raw info {};
	if (spa_format_video_raw_parse(param, &info) < 0) return;

	// Stride for our packed 4-byte formats.
	this->mNegotiatedStride = static_cast<int>(info.size.width) * 4;
	auto bufferBytes = this->mNegotiatedStride * static_cast<int>(info.size.height);

	qCInfo(logScCast) << "negotiated format:"
	                  << info.size.width << "x" << info.size.height
	                  << "stride=" << this->mNegotiatedStride
	                  << "bytes=" << bufferBytes;

	// Tell pw what our buffers will look like. Static config: 4 buffers,
	// stride * height bytes each, mappable shm.
	auto buf = std::array<quint8, 256> {};
	auto builder = SPA_POD_BUILDER_INIT(buf.data(), buf.size()); // NOLINT
	const spa_pod* params[1] = {
	    static_cast<const spa_pod*>(spa_pod_builder_add_object(
	        &builder,
	        SPA_TYPE_OBJECT_ParamBuffers,
	        SPA_PARAM_Buffers,
	        SPA_PARAM_BUFFERS_buffers,
	        SPA_POD_CHOICE_RANGE_Int(8, 2, 16),
	        SPA_PARAM_BUFFERS_blocks,
	        SPA_POD_Int(1),
	        SPA_PARAM_BUFFERS_size,
	        SPA_POD_Int(bufferBytes),
	        SPA_PARAM_BUFFERS_stride,
	        SPA_POD_Int(this->mNegotiatedStride),
	        SPA_PARAM_BUFFERS_dataType,
	        SPA_POD_CHOICE_FLAGS_Int(1 << SPA_DATA_MemPtr | 1 << SPA_DATA_MemFd)
	    )),
	};
	pw_stream_update_params(this->mStream, params, 1);
}

void PwScreenCastStream::handleProcess() {
	if (this->mStream == nullptr || this->mNegotiatedStride <= 0) return;

	auto* buffer = pw_stream_dequeue_buffer(this->mStream);
	if (buffer == nullptr) return;
	auto requeue = qScopeGuard([&, this] { pw_stream_queue_buffer(this->mStream, buffer); });

	auto* spaBuffer = buffer->buffer;
	if (spaBuffer == nullptr || spaBuffer->n_datas < 1) return;
	auto& datum = spaBuffer->datas[0]; // NOLINT
	if (datum.data == nullptr) return;

	auto height = this->mHeight;
	auto stride = this->mNegotiatedStride;
	auto totalBytes = stride * height;
	if (datum.maxsize < static_cast<uint32_t>(totalBytes)) totalBytes = static_cast<int>(datum.maxsize);

	if (this->mTestMode) {
		// Pack BGRA little-endian: B, G, R, A. QColor gives us 8-bit
		// channel values; build a single 32-bit pixel and memset-fill.
		auto c = this->mTestColor.toRgb();
		std::array<uint8_t, 4> px = {
		    static_cast<uint8_t>(c.blue()),
		    static_cast<uint8_t>(c.green()),
		    static_cast<uint8_t>(c.red()),
		    static_cast<uint8_t>(c.alpha()),
		};
		auto* dst = static_cast<uint8_t*>(datum.data);
		auto pixels = totalBytes / 4;
		for (int i = 0; i < pixels; i++) {
			std::memcpy(dst + i * 4, px.data(), 4);
		}
	} else {
		// Real frame producer wires here in step 6. For now zero-fill so
		// the consumer doesn't see uninitialized garbage.
		std::memset(datum.data, 0, totalBytes);
	}

	datum.chunk->offset = 0;
	datum.chunk->size = static_cast<uint32_t>(totalBytes);
	datum.chunk->stride = static_cast<int32_t>(stride);
}

} // namespace qs::service::pipewire

#pragma GCC diagnostic pop
