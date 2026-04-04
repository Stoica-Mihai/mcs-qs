#include "history.hpp"

#include <fcntl.h>
#include <qbytearray.h>
#include <qcryptographichash.h>
#include <qdatetime.h>
#include <thread>

#include <qclipboard.h>
#include <qfile.h>
#include <qguiapplication.h>
#include <qimage.h>
#include <qlogging.h>
#include <qmetaobject.h>
#include <qobject.h>
#include <qstring.h>
#include <qtimer.h>
#include <unistd.h>

#include <private/qwaylanddisplay_p.h>
#include <private/qwaylandinputdevice_p.h>
#include <private/qwaylandintegration_p.h>

#include "proto.hpp"

namespace qs::wayland::data_control {

namespace {
bool isImageMime(const QString& mime) { return mime.startsWith("image/"); }
} // namespace

// ── ClipboardEntry ──────────────────────────────────

ClipboardEntry::ClipboardEntry(
    QByteArray data,
    QString mimeType,
    QStringList allMimeTypes,
    QObject* parent
)
    : QObject(parent)
    , mData(std::move(data))
    , mHash(QCryptographicHash::hash(mData, QCryptographicHash::Md5))
    , mMimeType(std::move(mimeType))
    , mAllMimeTypes(std::move(allMimeTypes))
    , mTimestamp(QDateTime::currentDateTime()) {
	mIsImage = isImageMime(mMimeType);

	if (mIsImage) {
		auto sizeBytes = mData.size();
		QString unit;
		if (sizeBytes >= 1024 * 1024)
			unit = QString::number(sizeBytes / (1024.0 * 1024.0), 'f', 1) + " MB";
		else if (sizeBytes >= 1024)
			unit = QString::number(sizeBytes / 1024) + " KB";
		else
			unit = QString::number(sizeBytes) + " B";
		mContent = "[" + mMimeType + " " + unit + "]";
	} else {
		mContent = QString::fromUtf8(mData).left(500);
	}
}


// ── ClipboardHistory ────────────────────────────────

ClipboardHistory* ClipboardHistory::instance() {
	static auto* inst = new ClipboardHistory(); // NOLINT
	return inst;
}

ClipboardHistory::ClipboardHistory() {
	auto* manager = impl::DataControlManager::instance();
	if (!manager) {
		qCWarning(logDataControl) << "ext-data-control-v1 not supported by compositor.";
		return;
	}

	auto* display = QtWaylandClient::QWaylandIntegration::instance()->display();
	auto* inputDevice = display->lastInputDevice();
	if (!inputDevice) inputDevice = display->defaultInputDevice();
	if (!inputDevice) {
		qCCritical(logDataControl) << "Could not create data device: No seat.";
		return;
	}

	auto* rawDevice = manager->get_data_device(inputDevice->object());
	mDevice = new impl::DataControlDevice(rawDevice);
	mDevice->setParent(this);

	QObject::connect(
	    mDevice,
	    &impl::DataControlDevice::selectionChanged,
	    this,
	    &ClipboardHistory::onSelectionChanged
	);

	qCDebug(logDataControl) << "Clipboard history initialized";
}

QString ClipboardHistory::chooseMimeType(const QStringList& mimeTypes) {
	// Prefer UTF-8 text, then plain text, then any image
	for (const auto& mime : mimeTypes) {
		if (mime == "text/plain;charset=utf-8") return mime;
	}
	for (const auto& mime : mimeTypes) {
		if (mime == "text/plain") return mime;
	}
	for (const auto& mime : mimeTypes) {
		if (isImageMime(mime)) return mime;
	}
	return {};
}

void ClipboardHistory::onSelectionChanged(impl::DataControlOffer* offer) {
	if (mIsSettingSelection) {
		delete offer;
		return;
	}

	delete mDeferredOffer;
	mDeferredOffer = nullptr;

	if (!offer) return;

	auto mime = chooseMimeType(offer->mimeTypes());
	if (mime.isEmpty()) {
		delete offer;
		return;
	}

	// Defer the read to avoid racing with Qt's QWaylandClipboard,
	// which also reads on selection change.
	mDeferredOffer = offer;
	QTimer::singleShot(100, this, &ClipboardHistory::startReading);
}

void ClipboardHistory::startReading() {
	auto* offer = mDeferredOffer;
	mDeferredOffer = nullptr;
	if (!offer) return;

	auto mime = chooseMimeType(offer->mimeTypes());
	if (mime.isEmpty()) {
		delete offer;
		return;
	}

	// Blocking pipe — the read happens on a background thread
	int fds[2]; // NOLINT
	if (::pipe2(fds, O_CLOEXEC) != 0) {
		qCWarning(logDataControl) << "Failed to create pipe for clipboard read";
		delete offer;
		return;
	}

	auto allMimes = offer->mimeTypes();
	offer->receive(mime, fds[1]);
	::close(fds[1]);
	delete offer;

	qCDebug(logDataControl) << "Reading clipboard data for MIME:" << mime;

	// Read on a background thread to avoid deadlock: if the clipboard source
	// is Qt itself (e.g. screenshot setClipboardImage), Qt writes synchronously
	// on the event loop. Reading on the same event loop would deadlock.
	auto maxBytes = isImageMime(mime) ? MAX_IMAGE_BYTES : MAX_ENTRIES * 4096;
	std::thread([this, fd = fds[0], mime, allMimes, maxBytes]() {
		QByteArray buffer;
		char buf[8192]; // NOLINT
		while (true) {
			auto n = ::read(fd, buf, sizeof(buf));
			if (n > 0) {
				buffer.append(buf, static_cast<qsizetype>(n));
				if (buffer.size() > maxBytes) break;
			} else {
				break;
			}
		}
		::close(fd);

		if (!buffer.isEmpty() && buffer.size() <= maxBytes) {
			QMetaObject::invokeMethod(
			    this,
			    [this, buffer = std::move(buffer), mime, allMimes]() mutable {
				    addEntry(std::move(buffer), mime, allMimes);
			    },
			    Qt::QueuedConnection
			);
		}
	}).detach();
}

void ClipboardHistory::addEntry(
    QByteArray data, const QString& mimeType, const QStringList& allMimeTypes
) {
	// Deduplication: compare cached hashes instead of rehashing all entries
	auto hash = QCryptographicHash::hash(data, QCryptographicHash::Md5);
	auto& list = mEntries.valueList();
	for (qsizetype i = 0; i < list.size(); i++) {
		if (hash == list[i]->hash()) {
			// Move existing entry to top
			auto* existing = list[i];
			mEntries.removeAt(i);
			mEntries.insertObject(existing, 0);
			qCDebug(logDataControl) << "Deduplicated clipboard entry, moved to top";
			return;
		}
	}

	// Evict oldest if at capacity
	while (list.size() >= MAX_ENTRIES) {
		auto* last = list.last();
		mEntries.removeObject(last);
		delete last;
	}

	auto* entry = new ClipboardEntry(std::move(data), mimeType, allMimeTypes, this);
	mEntries.insertObject(entry, 0);
	qCDebug(logDataControl) << "Added clipboard entry:" << entry->content().left(50);
}

void ClipboardHistory::select(ClipboardEntry* entry) {
	if (!entry) return;

	// Use Qt's clipboard directly instead of ext_data_control.
	// Setting via ext_data_control causes a deadlock: Qt's QWaylandClipboard
	// does a synchronous pipe read on selection change, blocking the event loop,
	// so our source's send handler never fires → timeout.
	mIsSettingSelection = true;
	auto* clipboard = static_cast<QGuiApplication*>(QGuiApplication::instance())->clipboard(); // NOLINT

	if (entry->isImage()) {
		QImage image;
		image.loadFromData(entry->data());
		clipboard->setImage(image);
	} else {
		clipboard->setText(QString::fromUtf8(entry->data()));
	}

	// Clear flag after the echo from compositor has been processed
	QTimer::singleShot(500, this, [this]() { mIsSettingSelection = false; });

	qCDebug(logDataControl) << "Set clipboard from history:" << entry->content().left(50);
}

void ClipboardHistory::addFromFile(const QString& path, const QString& mimeType) {
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) return;
	auto data = file.readAll();
	if (data.isEmpty()) return;

	// Set clipboard from the same loaded data (avoids a second file read)
	if (isImageMime(mimeType)) {
		QImage image;
		image.loadFromData(data);
		if (!image.isNull()) {
			static_cast<QGuiApplication*>(QGuiApplication::instance()) // NOLINT
			    ->clipboard()
			    ->setImage(image);
		}
	} else {
		static_cast<QGuiApplication*>(QGuiApplication::instance()) // NOLINT
		    ->clipboard()
		    ->setText(QString::fromUtf8(data));
	}

	mIsSettingSelection = true;
	QTimer::singleShot(500, this, [this]() { mIsSettingSelection = false; });

	addEntry(std::move(data), mimeType, {mimeType});
}

void ClipboardHistory::remove(ClipboardEntry* entry) {
	if (mEntries.removeObject(entry)) {
		delete entry;
	}
}

void ClipboardHistory::clear() {
	auto& list = mEntries.valueList();
	while (!list.isEmpty()) {
		auto* entry = list.last();
		mEntries.removeObject(entry);
		delete entry;
	}
}

} // namespace qs::wayland::data_control
