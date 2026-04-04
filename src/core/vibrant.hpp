#pragma once

#include <qimage.h>
#include <qobject.h>
#include <qqmlintegration.h>
#include <qtmetamacros.h>
#include <qurl.h>

namespace qs::core {

///! Extract the most vibrant hue from an image for theming.
/// Uses CIELAB chroma-weighted hue binning (inspired by Material Color Utilities)
/// to find the most colorful hue in an image, ignoring desaturated backgrounds.
///
/// #### Example
/// ```qml
/// VibrantColor {
///   source: Qt.resolvedUrl("./wallpaper.png")
///   onHueChanged: console.log("seed hue:", hue)
/// }
/// ```
class VibrantColor: public QObject {
	Q_OBJECT;
	QML_ELEMENT;

	/// The source image URL.
	Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY sourceChanged);
	/// The extracted hue in [0, 1] range (Qt HSV convention). -1 if no vibrant color found.
	Q_PROPERTY(qreal hue READ hue NOTIFY hueChanged);

public:
	explicit VibrantColor(QObject* parent = nullptr): QObject(parent) {}

	[[nodiscard]] QUrl source() const { return mSource; }
	void setSource(const QUrl& source);

	[[nodiscard]] qreal hue() const { return mHue; }

signals:
	void sourceChanged();
	void hueChanged();

private:
	void extract();
	static qreal extractHue(const QImage& image);

	QUrl mSource;
	qreal mHue = -1;
};

} // namespace qs::core
