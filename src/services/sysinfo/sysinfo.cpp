#include "sysinfo.hpp"

#include <qdir.h>
#include <qfile.h>

#include "gpu.hpp"

namespace qs::service::sysinfo {

namespace {

QByteArray readFile(const char* path) {
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly)) return {};
	return f.readAll();
}

QByteArray readFile(const QString& path) {
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly)) return {};
	return f.readAll();
}

bool isPhysicalNetIf(const QByteArray& name) {
	return !name.isEmpty()
	    && name != "lo"
	    && !name.startsWith("docker")
	    && !name.startsWith("br-")
	    && !name.startsWith("veth")
	    && !name.startsWith("virbr")
	    && !name.startsWith("vnet");
}

bool isWholeDisk(const QByteArray& name) {
	if (name.startsWith("loop") || name.startsWith("ram")
	    || name.startsWith("zram") || name.startsWith("dm-"))
		return false;

	if (name.startsWith("nvme")) {
		int nPos = name.lastIndexOf('n');
		return nPos >= 0 && name.indexOf('p', nPos) < 0;
	}

	if (name.startsWith("sd") || name.startsWith("vd") || name.startsWith("hd")) {
		auto last = name.back();
		return last >= 'a' && last <= 'z';
	}

	return false;
}

} // namespace

SysInfo::SysInfo(QObject* parent): QObject(parent) {
	QDir cpuDir("/sys/devices/system/cpu");
	mCpuCount = cpuDir.entryList({"cpu[0-9]*"}, QDir::Dirs).size();
	mPrevCpuPerCore.resize(mCpuCount);

	scanHwmon();
	mGpuScanner = std::make_unique<GpuScanner>();

	readCpu();
	readMemory();
	readLoad();
	readUptime();
	readTemperatures();
	readNetwork();
	readDisk();
	readGpus();

	mElapsed.start();
	connect(&mTimer, &QTimer::timeout, this, &SysInfo::tick);

	QTimer::singleShot(250, this, [this]() {
		tick();
		mTimer.start(mInterval);
	});
}

SysInfo::~SysInfo() = default;

void SysInfo::setInterval(int ms) {
	if (ms < 100) ms = 100;
	if (ms == mInterval) return;
	mInterval = ms;
	if (mEnabled) mTimer.start(mInterval);
	emit intervalChanged();
}

void SysInfo::setEnabled(bool en) {
	if (en == mEnabled) return;
	mEnabled = en;
	if (en) {
		mElapsed.restart();
		mTimer.start(mInterval);
	} else {
		mTimer.stop();
	}
	emit enabledChanged();
}

qreal SysInfo::memPercent() const {
	return mMemTotal > 0 ? 100.0 * (mMemTotal - mMemAvailable) / mMemTotal : 0;
}

void SysInfo::tick() {
	readCpu();
	readMemory();
	readLoad();
	readUptime();
	readTemperatures();
	readNetwork();
	readDisk();
	readGpus();
	mElapsed.restart();
}

void SysInfo::readCpu() {
	auto data = readFile("/proc/stat");
	if (data.isEmpty()) return;

	auto prevPercent = mCpuPercent;
	auto prevCores = mCpuCores;
	auto prevFreq = mCpuFreqMhz;

	for (const auto& line : data.split('\n')) {
		if (!line.startsWith("cpu")) break;

		auto parts = line.simplified().split(' ');
		if (parts.size() < 8) continue;

		qlonglong user    = parts[1].toLongLong();
		qlonglong nice    = parts[2].toLongLong();
		qlonglong system  = parts[3].toLongLong();
		qlonglong idle    = parts[4].toLongLong();
		qlonglong iowait  = parts[5].toLongLong();
		qlonglong irq     = parts[6].toLongLong();
		qlonglong softirq = parts[7].toLongLong();
		qlonglong steal   = parts.size() > 8 ? parts[8].toLongLong() : 0;

		qlonglong idleAll = idle + iowait;
		qlonglong total   = user + nice + system + idle + iowait + irq + softirq + steal;

		if (parts[0] == "cpu") {
			auto dt = total - mPrevCpuAgg.total;
			auto di = idleAll - mPrevCpuAgg.idle;
			if (dt > 0) mCpuPercent = 100.0 * (dt - di) / dt;
			mPrevCpuAgg = {total, idleAll};
		} else {
			int idx = parts[0].mid(3).toInt();
			if (idx < 0 || idx >= mCpuCount) continue;

			auto& prev = mPrevCpuPerCore[idx];
			auto dt = total - prev.total;
			auto di = idleAll - prev.idle;
			qreal pct = dt > 0 ? 100.0 * (dt - di) / dt : 0;

			while (mCpuCores.size() <= idx) mCpuCores.append(0.0);
			mCpuCores[idx] = pct;
			prev = {total, idleAll};
		}
	}

	qlonglong freqSum = 0;
	int freqN = 0;
	for (int i = 0; i < mCpuCount; ++i) {
		auto d = readFile(
		    QStringLiteral("/sys/devices/system/cpu/cpu%1/cpufreq/scaling_cur_freq").arg(i));
		if (!d.isEmpty()) {
			freqSum += d.trimmed().toLongLong();
			++freqN;
		}
	}
	if (freqN > 0) mCpuFreqMhz = static_cast<int>(freqSum / freqN / 1000);

	if (mCpuPercent != prevPercent || mCpuCores != prevCores || mCpuFreqMhz != prevFreq)
		emit cpuChanged();
}

void SysInfo::readMemory() {
	auto data = readFile("/proc/meminfo");
	if (data.isEmpty()) return;

	qlonglong mt = 0, ma = 0, st = 0, sf = 0;
	int found = 0;

	for (const auto& line : data.split('\n')) {
		if (found == 4) break;
		auto parts = line.simplified().split(' ');
		if (parts.size() < 2) continue;

		qlonglong val = parts[1].toLongLong() * 1024;
		if (line.startsWith("MemTotal:"))          { mt = val; ++found; }
		else if (line.startsWith("MemAvailable:")) { ma = val; ++found; }
		else if (line.startsWith("SwapTotal:"))    { st = val; ++found; }
		else if (line.startsWith("SwapFree:"))     { sf = val; ++found; }
	}

	if (mt != mMemTotal || ma != mMemAvailable || st != mSwapTotal || sf != mSwapFree) {
		mMemTotal = mt;
		mMemAvailable = ma;
		mSwapTotal = st;
		mSwapFree = sf;
		emit memChanged();
	}
}

void SysInfo::readLoad() {
	auto data = readFile("/proc/loadavg");
	if (data.isEmpty()) return;

	auto parts = data.simplified().split(' ');
	if (parts.size() < 4) return;

	auto l1  = parts[0].toDouble();
	auto l5  = parts[1].toDouble();
	auto l15 = parts[2].toDouble();

	int running = 0, total = 0;
	auto procs = parts[3].split('/');
	if (procs.size() == 2) {
		running = procs[0].toInt();
		total   = procs[1].toInt();
	}

	if (l1 != mLoadAvg1 || l5 != mLoadAvg5 || l15 != mLoadAvg15
	    || running != mProcsRunning || total != mProcsTotal) {
		mLoadAvg1 = l1;
		mLoadAvg5 = l5;
		mLoadAvg15 = l15;
		mProcsRunning = running;
		mProcsTotal = total;
		emit loadChanged();
	}
}

void SysInfo::readUptime() {
	auto data = readFile("/proc/uptime");
	if (data.isEmpty()) return;

	auto val = data.simplified().split(' ')[0].toDouble();
	if (val != mUptime) {
		mUptime = val;
		emit uptimeChanged();
	}
}

void SysInfo::scanHwmon() {
	mTempSensors.clear();

	QDir hwmon("/sys/class/hwmon");
	for (const auto& entry : hwmon.entryList({"hwmon*"}, QDir::Dirs)) {
		auto base = hwmon.filePath(entry);
		auto chipName = QString::fromUtf8(readFile(base + "/name").trimmed());

		QDir sensorDir(base);
		for (const auto& file : sensorDir.entryList({"temp*_input"}, QDir::Files)) {
			TempSensor s;
			s.name = chipName;
			s.inputPath = base + "/" + file;

			auto labelFile = QString(file).replace("_input", "_label");
			auto labelData = readFile(base + "/" + labelFile);
			s.label = labelData.isEmpty() ? chipName : QString::fromUtf8(labelData.trimmed());

			mTempSensors.push_back(std::move(s));
		}
	}
}

void SysInfo::readTemperatures() {
	QVariantList temps;
	temps.reserve(static_cast<int>(mTempSensors.size()));

	for (const auto& sensor : mTempSensors) {
		auto data = readFile(sensor.inputPath);
		if (data.isEmpty()) continue;

		QVariantMap m;
		m[QStringLiteral("name")]  = sensor.name;
		m[QStringLiteral("label")] = sensor.label;
		m[QStringLiteral("value")] = data.trimmed().toLongLong() / 1000.0;
		temps.append(m);
	}

	if (temps != mTemperatures) {
		mTemperatures = std::move(temps);
		emit tempsChanged();
	}
}

void SysInfo::readNetwork() {
	auto data = readFile("/proc/net/dev");
	if (data.isEmpty()) return;

	qint64 elapsedMs = mElapsed.isValid() ? mElapsed.elapsed() : mInterval;
	if (elapsedMs <= 0) elapsedMs = mInterval;
	double elapsedSec = elapsedMs / 1000.0;

	QVariantList ifaces;

	auto lines = data.split('\n');
	for (int i = 2; i < lines.size(); ++i) {
		auto line = lines[i].trimmed();
		if (line.isEmpty()) continue;

		int colon = line.indexOf(':');
		if (colon < 0) continue;

		auto name = line.left(colon).trimmed();
		if (!isPhysicalNetIf(name)) continue;

		auto parts = line.mid(colon + 1).simplified().split(' ');
		if (parts.size() < 10) continue;

		qlonglong rx = parts[0].toLongLong();
		qlonglong tx = parts[8].toLongLong();

		NetIfState* st = nullptr;
		for (auto& s : mNetStates) {
			if (s.name == name) { st = &s; break; }
		}
		if (!st) {
			mNetStates.push_back({name, rx, tx});
			if (mNetFirstRead) continue;
			st = &mNetStates.back();
		}

		if (!mNetFirstRead) {
			QVariantMap m;
			m[QStringLiteral("name")]          = QString::fromUtf8(name);
			m[QStringLiteral("rxBytesPerSec")] = (rx - st->prevRx) / elapsedSec;
			m[QStringLiteral("txBytesPerSec")] = (tx - st->prevTx) / elapsedSec;
			m[QStringLiteral("rxTotal")]       = rx;
			m[QStringLiteral("txTotal")]       = tx;
			ifaces.append(m);
		}

		st->prevRx = rx;
		st->prevTx = tx;
	}

	mNetFirstRead = false;

	if (ifaces != mNetInterfaces) {
		mNetInterfaces = std::move(ifaces);
		emit netChanged();
	}
}

void SysInfo::readDisk() {
	auto data = readFile("/proc/diskstats");
	if (data.isEmpty()) return;

	qint64 elapsedMs = mElapsed.elapsed();
	if (elapsedMs <= 0) elapsedMs = mInterval;
	double elapsedSec = elapsedMs / 1000.0;

	QVariantList disks;

	for (const auto& line : data.split('\n')) {
		auto parts = line.simplified().split(' ');
		if (parts.size() < 14) continue;

		auto name = parts[2];
		if (!isWholeDisk(name)) continue;

		qlonglong rdSec = parts[5].toLongLong();
		qlonglong wrSec = parts[9].toLongLong();

		DiskState* st = nullptr;
		for (auto& s : mDiskStates) {
			if (s.name == name) { st = &s; break; }
		}
		if (!st) {
			mDiskStates.push_back({name, rdSec, wrSec});
			if (mDiskFirstRead) continue;
			st = &mDiskStates.back();
		}

		if (!mDiskFirstRead) {
			QVariantMap m;
			m[QStringLiteral("name")]             = QString::fromUtf8(name);
			m[QStringLiteral("readBytesPerSec")]  = (rdSec - st->prevReadSectors) * 512.0 / elapsedSec;
			m[QStringLiteral("writeBytesPerSec")] = (wrSec - st->prevWriteSectors) * 512.0 / elapsedSec;
			disks.append(m);
		}

		st->prevReadSectors = rdSec;
		st->prevWriteSectors = wrSec;
	}

	mDiskFirstRead = false;

	if (disks != mDiskDevices) {
		mDiskDevices = std::move(disks);
		emit diskChanged();
	}
}

void SysInfo::readGpus() {
	auto next = mGpuScanner->sample();
	if (next != mGpus) {
		mGpus = std::move(next);
		emit gpusChanged();
	}
}

} // namespace qs::service::sysinfo
