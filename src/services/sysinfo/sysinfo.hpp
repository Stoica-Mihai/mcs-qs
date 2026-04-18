#pragma once

#include <qelapsedtimer.h>
#include <qobject.h>
#include <qqmlintegration.h>
#include <qtimer.h>
#include <qtmetamacros.h>
#include <qvariant.h>

#include <memory>
#include <vector>

namespace qs::service::sysinfo {

class GpuScanner;

///! System information singleton — CPU, memory, temps, network, disk, GPU.
/// Polls /proc, /sys, NVML at a configurable interval (default 2 s).
/// Delta-based metrics (CPU %, network throughput, disk I/O) need two
/// samples, so they read 0 until the first timer tick after construction.
class SysInfo: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;

	// ---- configuration ----
	/// Poll interval in milliseconds (minimum 100). Default 2000.
	Q_PROPERTY(int interval READ interval WRITE setInterval NOTIFY intervalChanged);
	/// Enable or disable polling. Default true.
	Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged);

	// ---- cpu ----
	Q_PROPERTY(qreal cpuPercent READ cpuPercent NOTIFY cpuChanged);
	Q_PROPERTY(QVariantList cpuCores READ cpuCores NOTIFY cpuChanged);
	Q_PROPERTY(int cpuFreqMhz READ cpuFreqMhz NOTIFY cpuChanged);
	Q_PROPERTY(int cpuCount READ cpuCount CONSTANT);

	// ---- memory (bytes) ----
	Q_PROPERTY(qlonglong memTotal READ memTotal NOTIFY memChanged);
	Q_PROPERTY(qlonglong memAvailable READ memAvailable NOTIFY memChanged);
	Q_PROPERTY(qlonglong memUsed READ memUsed NOTIFY memChanged);
	Q_PROPERTY(qreal memPercent READ memPercent NOTIFY memChanged);
	Q_PROPERTY(qlonglong swapTotal READ swapTotal NOTIFY memChanged);
	Q_PROPERTY(qlonglong swapUsed READ swapUsed NOTIFY memChanged);

	// ---- load average ----
	Q_PROPERTY(qreal loadAvg1 READ loadAvg1 NOTIFY loadChanged);
	Q_PROPERTY(qreal loadAvg5 READ loadAvg5 NOTIFY loadChanged);
	Q_PROPERTY(qreal loadAvg15 READ loadAvg15 NOTIFY loadChanged);
	Q_PROPERTY(int processesRunning READ processesRunning NOTIFY loadChanged);
	Q_PROPERTY(int processesTotal READ processesTotal NOTIFY loadChanged);

	// ---- uptime ----
	Q_PROPERTY(qreal uptime READ uptime NOTIFY uptimeChanged);

	// ---- temperatures ----
	Q_PROPERTY(QVariantList temperatures READ temperatures NOTIFY tempsChanged);

	// ---- network throughput ----
	Q_PROPERTY(QVariantList netInterfaces READ netInterfaces NOTIFY netChanged);

	// ---- disk I/O ----
	Q_PROPERTY(QVariantList diskDevices READ diskDevices NOTIFY diskChanged);

	// ---- GPUs ----
	/// List of detected GPUs. Each entry:
	/// `{ vendor, name, utilization, vramUsed, vramTotal, power, clock, temperature }`.
	/// Fields unavailable for a given GPU are `-1`.
	Q_PROPERTY(QVariantList gpus READ gpus NOTIFY gpusChanged);

public:
	explicit SysInfo(QObject* parent = nullptr);
	~SysInfo() override;

	// config
	[[nodiscard]] int interval() const { return mInterval; }
	void setInterval(int ms);
	[[nodiscard]] bool enabled() const { return mEnabled; }
	void setEnabled(bool en);

	// cpu
	[[nodiscard]] qreal cpuPercent() const { return mCpuPercent; }
	[[nodiscard]] QVariantList cpuCores() const { return mCpuCores; }
	[[nodiscard]] int cpuFreqMhz() const { return mCpuFreqMhz; }
	[[nodiscard]] int cpuCount() const { return mCpuCount; }

	// memory
	[[nodiscard]] qlonglong memTotal() const { return mMemTotal; }
	[[nodiscard]] qlonglong memAvailable() const { return mMemAvailable; }
	[[nodiscard]] qlonglong memUsed() const { return mMemTotal - mMemAvailable; }
	[[nodiscard]] qreal memPercent() const;
	[[nodiscard]] qlonglong swapTotal() const { return mSwapTotal; }
	[[nodiscard]] qlonglong swapUsed() const { return mSwapTotal - mSwapFree; }

	// load
	[[nodiscard]] qreal loadAvg1() const { return mLoadAvg1; }
	[[nodiscard]] qreal loadAvg5() const { return mLoadAvg5; }
	[[nodiscard]] qreal loadAvg15() const { return mLoadAvg15; }
	[[nodiscard]] int processesRunning() const { return mProcsRunning; }
	[[nodiscard]] int processesTotal() const { return mProcsTotal; }

	// uptime
	[[nodiscard]] qreal uptime() const { return mUptime; }

	// models
	[[nodiscard]] QVariantList temperatures() const { return mTemperatures; }
	[[nodiscard]] QVariantList netInterfaces() const { return mNetInterfaces; }
	[[nodiscard]] QVariantList diskDevices() const { return mDiskDevices; }
	[[nodiscard]] QVariantList gpus() const { return mGpus; }

signals:
	void intervalChanged();
	void enabledChanged();
	void cpuChanged();
	void memChanged();
	void loadChanged();
	void uptimeChanged();
	void tempsChanged();
	void netChanged();
	void diskChanged();
	void gpusChanged();

private:
	void tick();
	void readCpu();
	void readMemory();
	void readLoad();
	void readUptime();
	void readTemperatures();
	void readNetwork();
	void readDisk();
	void readGpus();
	void scanHwmon();

	QTimer mTimer;
	QElapsedTimer mElapsed;
	int mInterval = 2000;
	bool mEnabled = true;

	// cpu
	int mCpuCount = 0;
	qreal mCpuPercent = 0;
	QVariantList mCpuCores;
	int mCpuFreqMhz = 0;

	struct CpuSample {
		qlonglong total = 0;
		qlonglong idle = 0;
	};

	CpuSample mPrevCpuAgg;
	std::vector<CpuSample> mPrevCpuPerCore;

	// memory
	qlonglong mMemTotal = 0;
	qlonglong mMemAvailable = 0;
	qlonglong mSwapTotal = 0;
	qlonglong mSwapFree = 0;

	// load
	qreal mLoadAvg1 = 0;
	qreal mLoadAvg5 = 0;
	qreal mLoadAvg15 = 0;
	int mProcsRunning = 0;
	int mProcsTotal = 0;

	// uptime
	qreal mUptime = 0;

	// temperatures
	struct TempSensor {
		QString name;
		QString label;
		QString inputPath;
	};

	std::vector<TempSensor> mTempSensors;
	QVariantList mTemperatures;

	// network
	struct NetIfState {
		QByteArray name;
		qlonglong prevRx = 0;
		qlonglong prevTx = 0;
	};

	std::vector<NetIfState> mNetStates;
	QVariantList mNetInterfaces;
	bool mNetFirstRead = true;

	// disk
	struct DiskState {
		QByteArray name;
		qlonglong prevReadSectors = 0;
		qlonglong prevWriteSectors = 0;
	};

	std::vector<DiskState> mDiskStates;
	QVariantList mDiskDevices;
	bool mDiskFirstRead = true;

	// gpu
	std::unique_ptr<GpuScanner> mGpuScanner;
	QVariantList mGpus;
};

} // namespace qs::service::sysinfo
