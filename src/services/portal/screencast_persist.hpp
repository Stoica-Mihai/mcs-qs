#pragma once

#include <qstring.h>
#include <qstringlist.h>

namespace qs::service::portal::ScreenCastPersist {

/// Look up a previously approved source list keyed by (appId, token).
/// Returns an empty list when no entry exists.
[[nodiscard]] QStringList load(const QString& appId, const QString& token);

/// Persist `sources` keyed by (appId, token).
void save(const QString& appId, const QString& token, const QStringList& sources);

/// Forget a previously saved (appId, token) entry.
void clear(const QString& appId, const QString& token);

} // namespace qs::service::portal::ScreenCastPersist
