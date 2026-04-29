#pragma once

#include <qobject.h>
#include <qpointer.h>
#include <qscreen.h>
#include <qstring.h>
#include <qtmetamacros.h>
#include <qtypes.h>

#include "../pipewire/screencast_stream.hpp"
#include "../../wayland/screencopy/manager.hpp"

namespace qs::service::portal {

/// One ScreenCast source in flight. Bridges a Wayland screencopy
/// producer (output capture for now; toplevel + region in step 8) to a
/// PipeWire video output stream owned by `pipewire::PwScreenCastStream`.
/// Lives for the lifetime of the owning ScreenCastSession; destroyed
/// when Session.Close fires or the caller drops off the bus.
class ScreenCastStream: public QObject {
	Q_OBJECT;

public:
	enum class SourceKind : quint8 { Output, Window };

	explicit ScreenCastStream(
	    SourceKind kind,
	    QScreen* screen,
	    bool paintCursors,
	    QObject* parent = nullptr
	);
	~ScreenCastStream() override;

	/// Begin capture + publish. Connects screencopy first; once a frame
	/// arrives we know the source size and can spin up the PipeWire
	/// stream with matching dimensions.
	void start();
	void stop();

	[[nodiscard]] bool isReady() const;
	[[nodiscard]] quint32 nodeId() const;
	[[nodiscard]] int width() const { return this->mWidth; }
	[[nodiscard]] int height() const { return this->mHeight; }
	[[nodiscard]] SourceKind kind() const { return this->mKind; }
	[[nodiscard]] QScreen* screen() const { return this->mScreen; }

signals:
	/// Emitted exactly once, when the PipeWire stream first reaches a
	/// negotiated/ready state. The session listens for this to send the
	/// delayed Start reply containing each stream's node id.
	void readyForCaller();

	/// Stream failed terminally. The session translates this into a
	/// fail() reply on the pending Start.
	void failed(QString reason);

private slots:
	void onFrameCaptured();
	void onCaptureStopped();
	void onPwReadyChanged();
	void onPwFailed(const QString& reason);

private:
	void initPwStream();

	SourceKind mKind;
	QPointer<QScreen> mScreen;
	bool mPaintCursors;
	int mWidth = 0;
	int mHeight = 0;
	bool mReadyEmitted = false;

	wayland::screencopy::ScreencopyContext* mCapture = nullptr;
	pipewire::PwScreenCastStream* mPw = nullptr;
};

} // namespace qs::service::portal
