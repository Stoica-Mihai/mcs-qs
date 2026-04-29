#pragma once

#include <qbytearray.h>
#include <qnetworkaccessmanager.h>
#include <qnetworkreply.h>
#include <qobject.h>
#include <qqmlintegration.h>
#include <qstring.h>
#include <qtmetamacros.h>
#include <qvariant.h>

namespace qs::io {

///! One-shot HTTP fetcher that decodes JSON responses on the QML side.
///
/// Replaces the `Process { command: ["curl", ...] }` / StdioCollector
/// pattern for fetching small JSON payloads (weather, holiday APIs).
/// Uses Qt's QNetworkAccessManager so there's no fork/exec — the fetch
/// happens on the GUI thread's event loop.
///
/// ### Usage
///
/// ```qml
/// HttpFetcher {
///   id: weather
///   onResponse: (status, body, error) => {
///     if (error) { console.warn(error); return; }
///     const data = JSON.parse(body);
///     // ...
///   }
/// }
/// // somewhere: weather.fetch("https://api.example.com/...");
/// ```
///
/// `fetch()` is single-flight — calling it while `loading` is true
/// drops the new call to mirror @@JsonFetcher's behaviour.
class HttpFetcher: public QObject {
	Q_OBJECT;
	QML_ELEMENT;

	Q_PROPERTY(int timeoutSeconds READ timeoutSeconds WRITE setTimeoutSeconds NOTIFY timeoutSecondsChanged);
	Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged);

public:
	explicit HttpFetcher(QObject* parent = nullptr);

	[[nodiscard]] int timeoutSeconds() const { return this->mTimeoutSeconds; }
	void setTimeoutSeconds(int seconds);

	[[nodiscard]] bool loading() const { return this->mReply != nullptr; }

	/// Issue a GET request to the supplied URL.
	Q_INVOKABLE void fetch(const QString& url);
	/// Cancel the in-flight request, if any. Suppresses both the response
	/// signals so callers can safely reset their state.
	Q_INVOKABLE void cancel();

signals:
	/// Emitted exactly once per `fetch()` (unless cancelled).
	/// `status` is the HTTP status code (0 if the request never reached
	/// the server). `body` is the response body. `error` is empty on
	/// success, or a short string ("network", "timeout", "http") on
	/// failure — the consumer parses `body` itself.
	void response(int status, const QByteArray& body, const QString& error);
	void timeoutSecondsChanged();
	void loadingChanged();

private slots:
	void onFinished();

private:
	int mTimeoutSeconds = 10;
	QNetworkAccessManager mNetwork {this};
	QNetworkReply* mReply = nullptr;
	bool mSuppressNextReply = false;
};

} // namespace qs::io
