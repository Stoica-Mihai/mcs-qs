#include "qml.hpp"

#include "history.hpp"

namespace qs::wayland::data_control {

ClipboardHistoryQml::ClipboardHistoryQml(QObject* parent): QObject(parent) {}

UntypedObjectModel* ClipboardHistoryQml::entries() {
	return ClipboardHistory::instance()->entries();
}

bool ClipboardHistoryQml::available() const {
	return ClipboardHistory::instance()->available();
}

void ClipboardHistoryQml::select(ClipboardEntry* entry) {
	ClipboardHistory::instance()->select(entry);
}

void ClipboardHistoryQml::remove(ClipboardEntry* entry) {
	ClipboardHistory::instance()->remove(entry);
}

void ClipboardHistoryQml::clear() {
	ClipboardHistory::instance()->clear();
}

void ClipboardHistoryQml::addFromFile(const QString& path, const QString& mimeType) {
	ClipboardHistory::instance()->addFromFile(path, mimeType);
}

} // namespace qs::wayland::data_control
