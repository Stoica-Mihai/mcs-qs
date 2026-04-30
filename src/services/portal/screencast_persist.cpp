#include "screencast_persist.hpp"

#include <qsettings.h>
#include <qstring.h>
#include <qstringlist.h>

namespace qs::service::portal::ScreenCastPersist {

namespace {
constexpr auto kGroup = "screencast/persist";

QString keyFor(const QString& appId, const QString& token) {
	// QSettings encodes "/" as a sub-group separator. Replace it in inputs
	// to get a flat key: appId___token.
	auto safeApp = QString(appId).replace('/', '_');
	auto safeToken = QString(token).replace('/', '_');
	return safeApp + QStringLiteral("___") + safeToken;
}

QSettings& store() {
	// Use the same org/app name pattern QSettings defaults to. Quickshell
	// already initializes QCoreApplication::organizationName / app, so
	// the default-constructed QSettings lands in the right config file.
	static QSettings s;
	return s;
}
} // namespace

QStringList load(const QString& appId, const QString& token) {
	if (appId.isEmpty() || token.isEmpty()) return {};
	auto& s = store();
	s.beginGroup(QString::fromLatin1(kGroup));
	auto v = s.value(keyFor(appId, token));
	s.endGroup();
	return v.toStringList();
}

void save(const QString& appId, const QString& token, const QStringList& sources) {
	if (appId.isEmpty() || token.isEmpty() || sources.isEmpty()) return;
	auto& s = store();
	s.beginGroup(QString::fromLatin1(kGroup));
	s.setValue(keyFor(appId, token), sources);
	s.endGroup();
	s.sync();
}

void clear(const QString& appId, const QString& token) {
	if (appId.isEmpty() || token.isEmpty()) return;
	auto& s = store();
	s.beginGroup(QString::fromLatin1(kGroup));
	s.remove(keyFor(appId, token));
	s.endGroup();
	s.sync();
}

} // namespace qs::service::portal::ScreenCastPersist
