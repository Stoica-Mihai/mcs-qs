#include "vibrant.hpp"

#include <algorithm>
#include <cmath>

#include <qimage.h>
#include <qlogging.h>
#include <qurl.h>

namespace qs::core {

namespace {

constexpr int HUE_BINS = 36;
constexpr double CHROMA_CUTOFF = 15.0;
constexpr double TONE_LOW = 10.0;
constexpr double TONE_HIGH = 95.0;
constexpr double PI = 3.14159265358979323846;

// sRGB → linear
double linearize(double c) {
	return (c <= 0.04045) ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

// CIE Lab f(t)
double labF(double t) {
	return (t > 0.008856) ? std::cbrt(t) : 7.787 * t + 16.0 / 116.0;
}

struct LabColor {
	double L, a, b, C, h; // L*, a*, b*, chroma, hue (degrees 0-360)
};

LabColor rgbToLab(int r, int g, int b) {
	// sRGB → linear → XYZ (D65)
	double lr = linearize(r / 255.0);
	double lg = linearize(g / 255.0);
	double lb = linearize(b / 255.0);

	double X = 0.4124564 * lr + 0.3575761 * lg + 0.1804375 * lb;
	double Y = 0.2126729 * lr + 0.7151522 * lg + 0.0721750 * lb;
	double Z = 0.0193339 * lr + 0.1191920 * lg + 0.9503041 * lb;

	// XYZ → Lab (D65 white point)
	double fx = labF(X / 0.95047);
	double fy = labF(Y / 1.00000);
	double fz = labF(Z / 1.08883);

	LabColor lab;
	lab.L = 116.0 * fy - 16.0;
	lab.a = 500.0 * (fx - fy);
	lab.b = 200.0 * (fy - fz);
	lab.C = std::sqrt(lab.a * lab.a + lab.b * lab.b);
	lab.h = std::atan2(lab.b, lab.a) * 180.0 / PI;
	if (lab.h < 0) lab.h += 360.0;

	return lab;
}

} // namespace

void VibrantColor::setSource(const QUrl& source) {
	if (source == mSource) return;
	mSource = source;
	emit sourceChanged();
	extract();
}

void VibrantColor::extract() {
	if (mSource.isEmpty()) return;

	QImage image(mSource.toLocalFile());
	if (image.isNull()) return;

	auto newHue = extractHue(image);
	if (newHue != mHue) {
		mHue = newHue;
		emit hueChanged();
	}
}

qreal VibrantColor::extractHue(const QImage& image) {
	auto w = image.width();
	auto h = image.height();
	if (w == 0 || h == 0) return -1;

	// Stride to sample ~32K pixels max
	int stride = std::max(1, static_cast<int>(std::sqrt(w * h / 32000.0)));

	double bins[HUE_BINS] = {};
	double sinSum[HUE_BINS] = {};
	double cosSum[HUE_BINS] = {};

	for (int y = 0; y < h; y += stride) {
		for (int x = 0; x < w; x += stride) {
			auto px = image.pixel(x, y);
			auto lab = rgbToLab(qRed(px), qGreen(px), qBlue(px));

			// Filter grays, blacks, whites
			if (lab.C < CHROMA_CUTOFF) continue;
			if (lab.L < TONE_LOW || lab.L > TONE_HIGH) continue;

			int idx = static_cast<int>(lab.h / 10.0) % HUE_BINS;
			bins[idx] += lab.C;
			double hRad = lab.h * PI / 180.0;
			sinSum[idx] += std::sin(hRad);
			cosSum[idx] += std::cos(hRad);
		}
	}

	// Find the bin with highest chroma-weighted sum
	int best = -1;
	double bestVal = 0;
	for (int i = 0; i < HUE_BINS; i++) {
		if (bins[i] > bestVal) {
			bestVal = bins[i];
			best = i;
		}
	}

	if (best < 0) return -1; // no vibrant colors found

	// Circular mean hue within the winning bin
	double meanHueDeg = std::atan2(sinSum[best], cosSum[best]) * 180.0 / PI;
	if (meanHueDeg < 0) meanHueDeg += 360.0;

	// Convert from degrees [0,360] to Qt HSV [0,1]
	return meanHueDeg / 360.0;
}

} // namespace qs::core
