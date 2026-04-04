#pragma once

#include <qbytearray.h>
#include <qcontainerfwd.h>
#include <qdatetime.h>
#include <qobject.h>
#include <qqmlintegration.h>
#include <qtclasshelpermacros.h>
#include <qtmetamacros.h>
#include <qtimer.h>

#include "../../core/model.hpp"
#include "proto.hpp"

namespace qs::wayland::data_control {

class ClipboardEntry: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_UNCREATABLE("ClipboardEntry is created by ClipboardHistory.");

	Q_PROPERTY(QString content READ content CONSTANT);
	Q_PROPERTY(bool isImage READ isImage CONSTANT);
	Q_PROPERTY(QString mimeType READ mimeType CONSTANT);
	Q_PROPERTY(QDateTime timestamp READ timestamp CONSTANT);
	/// Data URI for image entries (lazy base64 encoding on first access).
	Q_PROPERTY(QString imageUrl READ imageUrl CONSTANT);

public:
	ClipboardEntry(
	    QByteArray data,
	    QString mimeType,
	    QStringList allMimeTypes,
	    QObject* parent = nullptr,
	    QDateTime timestamp = QDateTime::currentDateTime()
	);

	[[nodiscard]] const QString& content() const { return mContent; }
	[[nodiscard]] bool isImage() const { return mIsImage; }
	[[nodiscard]] const QString& mimeType() const { return mMimeType; }
	[[nodiscard]] const QDateTime& timestamp() const { return mTimestamp; }
	[[nodiscard]] QString imageUrl() const;

	[[nodiscard]] const QByteArray& data() const { return mData; }
	[[nodiscard]] const QStringList& allMimeTypes() const { return mAllMimeTypes; }
	[[nodiscard]] const QByteArray& hash() const { return mHash; }

private:
	QByteArray mData;
	QByteArray mHash;
	QString mMimeType;
	QStringList mAllMimeTypes;
	QString mContent;
	mutable QString mImageUrl; // lazy-cached data URI
	bool mIsImage;
	QDateTime mTimestamp;
};

class ClipboardHistory: public QObject {
	Q_OBJECT;

public:
	static constexpr int MAX_ENTRIES = 200;
	static constexpr int MAX_IMAGE_BYTES = 10 * 1024 * 1024;

	static ClipboardHistory* instance();

	[[nodiscard]] ObjectModel<ClipboardEntry>* entries() { return &mEntries; }
	[[nodiscard]] bool available() const { return mDevice != nullptr; }

	void select(ClipboardEntry* entry);
	void remove(ClipboardEntry* entry);
	void clear();

	/// Add a text entry and set it as the clipboard.
	void addText(const QString& text);

	/// Add an entry from a file and set it as the clipboard.
	/// which the compositor may not echo back via ext_data_control).
	void addFromFile(const QString& path, const QString& mimeType);

private:
	explicit ClipboardHistory();

	void onSelectionChanged(impl::DataControlOffer* offer);
	void startReading();
	void addEntry(QByteArray data, const QString& mimeType, const QStringList& allMimeTypes);
	void scheduleSave();
	void save();
	void load();

	static QString chooseMimeType(const QStringList& mimeTypes);
	static QString storagePath();

	ObjectModel<ClipboardEntry> mEntries{this};
	impl::DataControlDevice* mDevice = nullptr;
	impl::DataControlOffer* mDeferredOffer = nullptr;
	QTimer mSaveTimer;
	bool mIsSettingSelection = false;
};

} // namespace qs::wayland::data_control
