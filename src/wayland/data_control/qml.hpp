#pragma once

#include <qobject.h>
#include <qqmlintegration.h>
#include <qtmetamacros.h>

#include "../../core/model.hpp"
#include "history.hpp"

namespace qs::wayland::data_control {

///! Native clipboard history via ext-data-control-v1.
/// Monitors clipboard changes and maintains an in-memory history.
/// Each entry preserves the original MIME types and data, so selecting
/// an entry from history restores it to the clipboard exactly.
///
/// > [!NOTE] Requires compositor support for the [ext-data-control-v1] protocol.
///
/// [ext-data-control-v1]: https://wayland.app/protocols/ext-data-control-v1
class ClipboardHistoryQml: public QObject {
	Q_OBJECT;
	QML_NAMED_ELEMENT(ClipboardHistory);
	QML_SINGLETON;

	/// The clipboard history entries, newest first.
	Q_PROPERTY(UntypedObjectModel* entries READ entries CONSTANT);
	/// Whether the compositor supports the data control protocol.
	Q_PROPERTY(bool available READ available CONSTANT);

public:
	explicit ClipboardHistoryQml(QObject* parent = nullptr);

	[[nodiscard]] UntypedObjectModel* entries();
	[[nodiscard]] bool available() const;

	/// Copy a history entry back to the clipboard.
	Q_INVOKABLE void select(ClipboardEntry* entry);
	/// Remove a specific entry from history.
	Q_INVOKABLE void remove(ClipboardEntry* entry);
	/// Clear all clipboard history.
	Q_INVOKABLE void clear();
	/// Add a text entry and set it as the clipboard.
	Q_INVOKABLE void addText(const QString& text);
	/// Add an entry from a file and set it as the clipboard.
	Q_INVOKABLE void addFromFile(const QString& path, const QString& mimeType);
};

} // namespace qs::wayland::data_control
