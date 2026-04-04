#pragma once

#include <qcontainerfwd.h>
#include <qobject.h>
#include <qtclasshelpermacros.h>
#include <qtmetamacros.h>
#include <qwayland-ext-data-control-v1.h>
#include <qwaylandclientextension.h>
#include <wayland-ext-data-control-v1-client-protocol.h>

#include "../../core/logcat.hpp"

namespace qs::wayland::data_control {
QS_DECLARE_LOGGING_CATEGORY(logDataControl);
}

namespace qs::wayland::data_control::impl {

class DataControlOffer;

class DataControlManager
    : public QWaylandClientExtensionTemplate<DataControlManager>
    , public QtWayland::ext_data_control_manager_v1 {
public:
	explicit DataControlManager();
	static DataControlManager* instance();
};

class DataControlDevice
    : public QObject
    , public QtWayland::ext_data_control_device_v1 {
	Q_OBJECT;

public:
	explicit DataControlDevice(::ext_data_control_device_v1* device);
	~DataControlDevice() override;
	Q_DISABLE_COPY_MOVE(DataControlDevice);

signals:
	void selectionChanged(DataControlOffer* offer);
	void finished();

protected:
	void ext_data_control_device_v1_data_offer(::ext_data_control_offer_v1* id) override;
	void ext_data_control_device_v1_selection(::ext_data_control_offer_v1* id) override;
	void ext_data_control_device_v1_primary_selection(::ext_data_control_offer_v1* id) override;
	void ext_data_control_device_v1_finished() override;

private:
	DataControlOffer* mPendingOffer = nullptr;
};

class DataControlOffer
    : public QObject
    , public QtWayland::ext_data_control_offer_v1 {
	Q_OBJECT;

public:
	explicit DataControlOffer(::ext_data_control_offer_v1* offer);
	~DataControlOffer() override;
	Q_DISABLE_COPY_MOVE(DataControlOffer);

	[[nodiscard]] const QStringList& mimeTypes() const { return mMimeTypes; }
	void receive(const QString& mimeType, int fd);

protected:
	void ext_data_control_offer_v1_offer(const QString& mimeType) override;

private:
	QStringList mMimeTypes;
};

} // namespace qs::wayland::data_control::impl
