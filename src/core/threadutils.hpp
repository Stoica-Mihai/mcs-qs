#pragma once

#include <qcoreapplication.h>
#include <qmetaobject.h>
#include <qthread.h>

// Run a callable on the main (GUI) thread. If already on the main thread,
// calls directly. Otherwise blocks until the main thread executes it.
// Use for APIs that are not thread-safe (e.g. QIcon::fromTheme).
template<typename F>
void runOnMainThread(F&& fn) {
	auto* app = QCoreApplication::instance();
	if (QThread::currentThread() == app->thread()) {
		fn();
	} else {
		QMetaObject::invokeMethod(app, std::forward<F>(fn), Qt::BlockingQueuedConnection);
	}
}
