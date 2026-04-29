#include "httpfetcher.hpp"

#include <qlogging.h>
#include <qloggingcategory.h>
#include <qnetworkreply.h>
#include <qnetworkrequest.h>
#include <qstring.h>
#include <qurl.h>

#include "../core/logcat.hpp"

namespace qs::io {

namespace {
QS_LOGGING_CATEGORY(logHttp, "quickshell.io.http", QtWarningMsg);
}

HttpFetcher::HttpFetcher(QObject* parent): QObject(parent) {}

void HttpFetcher::setTimeoutSeconds(int seconds) {
	if (this->mTimeoutSeconds == seconds) return;
	this->mTimeoutSeconds = seconds;
	emit this->timeoutSecondsChanged();
}

void HttpFetcher::fetch(const QString& url) {
	if (this->mReply != nullptr) {
		qCWarning(logHttp) << "fetch() called while a request is in flight; dropping";
		return;
	}
	if (url.isEmpty()) {
		emit this->response(0, {}, "empty-url");
		return;
	}

	QNetworkRequest req {QUrl(url)};
	req.setTransferTimeout(this->mTimeoutSeconds * 1000);
	req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
	                 QNetworkRequest::NoLessSafeRedirectPolicy);
	this->mSuppressNextReply = false;
	this->mReply = this->mNetwork.get(req);
	emit this->loadingChanged();
	QObject::connect(this->mReply, &QNetworkReply::finished, this, &HttpFetcher::onFinished);
}

void HttpFetcher::cancel() {
	if (this->mReply == nullptr) return;
	this->mSuppressNextReply = true;
	this->mReply->abort();
}

void HttpFetcher::onFinished() {
	auto* reply = this->mReply;
	this->mReply = nullptr;

	if (this->mSuppressNextReply) {
		reply->deleteLater();
		emit this->loadingChanged();
		return;
	}

	auto status =
	    reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
	auto body = reply->readAll();
	QString error;

	if (reply->error() == QNetworkReply::OperationCanceledError) {
		error = "timeout";
	} else if (reply->error() != QNetworkReply::NoError) {
		error = "network";
	} else if (status >= 400) {
		error = "http";
	}

	reply->deleteLater();
	emit this->response(status, body, error);
	emit this->loadingChanged();
}

} // namespace qs::io
