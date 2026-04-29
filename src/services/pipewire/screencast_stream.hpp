#pragma once

#include <pipewire/stream.h>
#include <qcolor.h>
#include <qelapsedtimer.h>
#include <qobject.h>
#include <qqmlintegration.h>
#include <qtclasshelpermacros.h>
#include <qtmetamacros.h>
#include <qtypes.h>

#include "core.hpp"

namespace qs::service::pipewire {

///! Publishes a video stream as a PipeWire output node.
///
/// Building block for the ScreenCast portal — wraps a `pw_stream` of type
/// `Video/Source` so any PipeWire-aware consumer (Chrome screen-share,
/// OBS PipeWire capture, `gst-launch-1.0 pipewiresrc`, etc.) can connect
/// to the published node and receive frames.
///
/// Step 3 of PLAN-screencast-portal.md ships this as a standalone
/// element with a built-in **test mode**: when @@testMode is true, every
/// frame the consumer requests is filled with @@testColor so we can
/// validate format negotiation and the consumer side without screencopy
/// in the picture yet. Real frame ingestion lands in step 6.
///
/// Format negotiation is BGRA fixed-size for now (the simplest path that
/// every consumer supports). dmabuf modifier negotiation lands in step 7.
///
/// ```qml
/// PwScreenCastStream {
///     width: 640; height: 480; framerate: 30
///     testMode: true
///     testColor: "red"
///     enabled: true
///     onNodeIdChanged: console.log("ready as PW node", nodeId)
/// }
/// ```
class PwScreenCastStream: public QObject {
	Q_OBJECT;
	// clang-format off
	/// Width of the published video stream in pixels. Default 1920.
	Q_PROPERTY(int width READ width WRITE setWidth NOTIFY paramsChanged);
	/// Height of the published video stream in pixels. Default 1080.
	Q_PROPERTY(int height READ height WRITE setHeight NOTIFY paramsChanged);
	/// Maximum frame rate in frames-per-second (numerator over 1).
	/// Default 60. Consumer can negotiate any rate up to this.
	Q_PROPERTY(int framerate READ framerate WRITE setFramerate NOTIFY paramsChanged);
	/// When true, attempt to create + connect the PipeWire stream.
	/// Toggle to off to release the node. Default false.
	Q_PROPERTY(bool enabled READ isEnabled WRITE setEnabled NOTIFY enabledChanged);
	/// Built-in test mode. When true, the stream answers every consumer
	/// frame request with a solid-color buffer of @@testColor — no
	/// external producer needed. Default true (this property primarily
	/// exists so step 3 can be exercised standalone).
	Q_PROPERTY(bool testMode READ testMode WRITE setTestMode NOTIFY testModeChanged);
	/// Fill color for test-mode frames. Default red. Updated values take
	/// effect on the next frame.
	Q_PROPERTY(QColor testColor READ testColor WRITE setTestColor NOTIFY testColorChanged);
	/// PipeWire global object id of the published node, or 0 if not yet
	/// connected. Read this once @@ready turns true; pass to a consumer
	/// (e.g. `gst-launch-1.0 pipewiresrc target-object=<nodeId>`).
	Q_PROPERTY(quint32 nodeId READ nodeId NOTIFY nodeIdChanged);
	/// True once the stream has reached the PAUSED or STREAMING state
	/// with a negotiated format. Frames cannot flow until this is true.
	Q_PROPERTY(bool ready READ isReady NOTIFY readyChanged);
	// clang-format on
	QML_ELEMENT;

public:
	explicit PwScreenCastStream(QObject* parent = nullptr);
	~PwScreenCastStream() override;
	Q_DISABLE_COPY_MOVE(PwScreenCastStream);

	[[nodiscard]] int width() const { return this->mWidth; }
	void setWidth(int width);

	[[nodiscard]] int height() const { return this->mHeight; }
	void setHeight(int height);

	[[nodiscard]] int framerate() const { return this->mFramerate; }
	void setFramerate(int rate);

	[[nodiscard]] bool isEnabled() const { return this->mEnabled; }
	void setEnabled(bool enabled);

	[[nodiscard]] bool testMode() const { return this->mTestMode; }
	void setTestMode(bool enabled);

	[[nodiscard]] QColor testColor() const { return this->mTestColor; }
	void setTestColor(const QColor& color);

	[[nodiscard]] quint32 nodeId() const { return this->mNodeId; }
	[[nodiscard]] bool isReady() const { return this->mReady; }

signals:
	void paramsChanged();
	void enabledChanged();
	void testModeChanged();
	void testColorChanged();
	void nodeIdChanged();
	void readyChanged();
	void failed(const QString& reason);

private:
	bool start();
	void stop();
	void rebuildIfRunning();

	static const pw_stream_events EVENTS;
	static void onStateChanged(
	    void* data,
	    pw_stream_state oldState,
	    pw_stream_state state,
	    const char* error
	);
	static void onParamChanged(void* data, uint32_t id, const spa_pod* param);
	static void onProcess(void* data);

	void handleStateChanged(pw_stream_state state, const char* error);
	void handleParamChanged(uint32_t id, const spa_pod* param);
	void handleProcess();

	void setNodeId(quint32 id);
	void setReady(bool ready);

	int mWidth = 1920;
	int mHeight = 1080;
	int mFramerate = 60;
	bool mEnabled = false;
	bool mTestMode = true;
	QColor mTestColor;
	quint32 mNodeId = 0;
	bool mReady = false;

	pw_stream* mStream = nullptr;
	SpaHook mListener;
	int mNegotiatedStride = 0;
	QElapsedTimer mFrameClock;
};

} // namespace qs::service::pipewire
