#pragma once

#include <qimage.h>
#include <qpixmap.h>
#include <qquickimageprovider.h>

class IconImageProvider: public QQuickImageProvider {
public:
	explicit IconImageProvider(): QQuickImageProvider(QQuickImageProvider::Image) {}

	QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

	static QString requestString(
	    const QString& icon,
	    const QString& path = QString(),
	    const QString& fallback = QString()
	);

	static QPixmap missingPixmap(const QSize& size);

private:
	static QImage doIconLookup(const QString& id, QSize* size, const QSize& requestedSize);
};
