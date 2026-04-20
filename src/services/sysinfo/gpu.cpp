#include "gpu.hpp"

#include <qdir.h>
#include <qfile.h>
#include <qfileinfo.h>
#include <qloggingcategory.h>

#include <dlfcn.h>

namespace qs::service::sysinfo {

namespace {

Q_LOGGING_CATEGORY(logGpu, "quickshell.service.sysinfo.gpu", QtWarningMsg);

QByteArray readTrim(const QString& path) {
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly)) return {};
	return f.readAll().trimmed();
}

qlonglong readLongLong(const QString& path, qlonglong fallback = -1) {
	auto data = readTrim(path);
	if (data.isEmpty()) return fallback;
	bool ok = false;
	auto v = data.toLongLong(&ok);
	return ok ? v : fallback;
}

// Parse lines like:  "0: 200Mhz\n1: 500Mhz *\n2: 1000Mhz"
// Return the value marked with trailing '*' (currently active DPM level).
int parseActiveDpmMhz(const QString& path) {
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly)) return -1;
	for (const auto& line : f.readAll().split('\n')) {
		if (!line.contains('*')) continue;
		auto clean = QByteArray(line).replace('*', ' ').simplified();
		auto parts = clean.split(' ');
		if (parts.size() < 2) continue;
		auto mhz = parts[1];
		// strip "Mhz" / "MHz" suffix
		while (!mhz.isEmpty() && !(mhz.back() >= '0' && mhz.back() <= '9'))
			mhz.chop(1);
		if (mhz.isEmpty()) continue;
		bool ok = false;
		auto v = mhz.toInt(&ok);
		if (ok) return v;
	}
	return -1;
}

QString findHwmonDir(const QString& devicePath) {
	QDir d(devicePath + "/hwmon");
	if (!d.exists()) return {};
	auto entries = d.entryList({"hwmon*"}, QDir::Dirs);
	return entries.isEmpty() ? QString() : d.filePath(entries.first());
}

// NVML — minimal local ABI definitions so we don't need nvml.h at build time.
using nvmlReturn_t = int;
constexpr nvmlReturn_t NVML_SUCCESS = 0;
constexpr unsigned int NVML_CLOCK_GRAPHICS = 0;
constexpr unsigned int NVML_TEMPERATURE_GPU = 0;
constexpr unsigned int NVML_DEVICE_NAME_BUFFER_SIZE = 96;
constexpr unsigned int NVML_DEVICE_PCI_BUS_ID_BUFFER_SIZE = 32;

using nvmlDevice_t = void*;

struct nvmlUtilization_t {
	unsigned int gpu;
	unsigned int memory;
};

struct nvmlMemory_t {
	unsigned long long total;
	unsigned long long free;
	unsigned long long used;
};

// Layout matches nvml.h's nvmlPciInfo_st (v3). Fields after busId are
// present in the ABI but unused here.
struct nvmlPciInfo_t {
	char busIdLegacy[16];
	unsigned int domain;
	unsigned int bus;
	unsigned int device;
	unsigned int pciDeviceId;
	unsigned int pciSubSystemId;
	char busId[NVML_DEVICE_PCI_BUS_ID_BUFFER_SIZE];
};

using pfn_nvmlInit_v2 = nvmlReturn_t (*)();
using pfn_nvmlShutdown = nvmlReturn_t (*)();
using pfn_nvmlDeviceGetCount_v2 = nvmlReturn_t (*)(unsigned int*);
using pfn_nvmlDeviceGetHandleByIndex_v2 = nvmlReturn_t (*)(unsigned int, nvmlDevice_t*);
using pfn_nvmlDeviceGetName = nvmlReturn_t (*)(nvmlDevice_t, char*, unsigned int);
using pfn_nvmlDeviceGetUtilizationRates = nvmlReturn_t (*)(nvmlDevice_t, nvmlUtilization_t*);
using pfn_nvmlDeviceGetMemoryInfo = nvmlReturn_t (*)(nvmlDevice_t, nvmlMemory_t*);
using pfn_nvmlDeviceGetPowerUsage = nvmlReturn_t (*)(nvmlDevice_t, unsigned int*);
using pfn_nvmlDeviceGetClockInfo = nvmlReturn_t (*)(nvmlDevice_t, unsigned int, unsigned int*);
using pfn_nvmlDeviceGetTemperature = nvmlReturn_t (*)(nvmlDevice_t, unsigned int, unsigned int*);
using pfn_nvmlDeviceGetPciInfo_v3 = nvmlReturn_t (*)(nvmlDevice_t, nvmlPciInfo_t*);

struct Nvml {
	pfn_nvmlInit_v2 init = nullptr;
	pfn_nvmlShutdown shutdown = nullptr;
	pfn_nvmlDeviceGetCount_v2 getCount = nullptr;
	pfn_nvmlDeviceGetHandleByIndex_v2 getHandle = nullptr;
	pfn_nvmlDeviceGetName getName = nullptr;
	pfn_nvmlDeviceGetUtilizationRates getUtilization = nullptr;
	pfn_nvmlDeviceGetMemoryInfo getMemory = nullptr;
	pfn_nvmlDeviceGetPowerUsage getPower = nullptr;
	pfn_nvmlDeviceGetClockInfo getClock = nullptr;
	pfn_nvmlDeviceGetTemperature getTemperature = nullptr;
	pfn_nvmlDeviceGetPciInfo_v3 getPciInfo = nullptr; // optional — older drivers lack it
};

Nvml gNvml;

} // namespace

GpuScanner::GpuScanner() {
	scanDrmCards();
}

GpuScanner::~GpuScanner() {
	shutdownNvml();
}

void GpuScanner::scanDrmCards() {
	QDir drm("/sys/class/drm");
	for (const auto& entry : drm.entryList({"card[0-9]*"}, QDir::Dirs)) {
		// Skip card1-DP-1 style connector subdirs (contain '-').
		if (entry.contains('-')) continue;

		auto cardPath = drm.filePath(entry);
		auto vendor = QString::fromUtf8(readTrim(cardPath + "/device/vendor"));
		if (vendor.isEmpty()) continue;

		if (vendor == QStringLiteral("0x1002")) {
			AmdCard c;
			c.cardName = entry;
			c.path = cardPath + "/device";
			// Resolve human name from modalias or fall back to device id.
			c.name = QStringLiteral("AMD GPU (") + entry + QStringLiteral(")");
			auto product = QString::fromUtf8(readTrim(cardPath + "/device/product_name"));
			if (!product.isEmpty()) c.name = product;
			mAmdCards.push_back(std::move(c));
		} else if (vendor == QStringLiteral("0x8086")) {
			IntelCard c;
			c.cardName = entry;
			c.path = cardPath;
			c.name = QStringLiteral("Intel GPU (") + entry + QStringLiteral(")");
			mIntelCards.push_back(std::move(c));
		} else if (vendor == QStringLiteral("0x10de")) {
			// NVIDIA card — per-device data comes via NVML, but record the
			// DRM card here so we can look up connector status by PCI.
			NvidiaCard c;
			c.cardName = entry;
			// Resolve /sys/class/drm/cardN/device symlink; its final
			// segment is the PCI bus id (e.g., "0000:01:00.0").
			auto target = QFileInfo(cardPath + "/device").canonicalFilePath();
			if (!target.isEmpty()) c.pciBusId = target.section('/', -1);
			mNvidiaCards.push_back(std::move(c));
		}
	}
}

bool GpuScanner::cardHasConnectedDisplay(const QString& cardName) {
	QDir drm("/sys/class/drm");
	auto connectors = drm.entryList({cardName + "-*"}, QDir::Dirs);
	for (const auto& conn : connectors) {
		if (readTrim(drm.filePath(conn) + "/status") == "connected") return true;
	}
	return false;
}

void GpuScanner::initNvml() {
	if (mNvmlAttempted) return;
	mNvmlAttempted = true;

	mNvmlLib = dlopen("libnvidia-ml.so.1", RTLD_LAZY);
	if (!mNvmlLib) {
		qCDebug(logGpu) << "libnvidia-ml.so.1 not available, skipping NVIDIA detection";
		return;
	}

	auto resolve = [this]<typename F>(F& fn, const char* sym) -> bool {
		fn = reinterpret_cast<F>(dlsym(mNvmlLib, sym));
		return fn != nullptr;
	};

	if (!resolve(gNvml.init, "nvmlInit_v2")
	    || !resolve(gNvml.shutdown, "nvmlShutdown")
	    || !resolve(gNvml.getCount, "nvmlDeviceGetCount_v2")
	    || !resolve(gNvml.getHandle, "nvmlDeviceGetHandleByIndex_v2")
	    || !resolve(gNvml.getName, "nvmlDeviceGetName")
	    || !resolve(gNvml.getUtilization, "nvmlDeviceGetUtilizationRates")
	    || !resolve(gNvml.getMemory, "nvmlDeviceGetMemoryInfo")
	    || !resolve(gNvml.getPower, "nvmlDeviceGetPowerUsage")
	    || !resolve(gNvml.getClock, "nvmlDeviceGetClockInfo")
	    || !resolve(gNvml.getTemperature, "nvmlDeviceGetTemperature")) {
		qCWarning(logGpu) << "libnvidia-ml.so.1 loaded but missing expected symbols";
		dlclose(mNvmlLib);
		mNvmlLib = nullptr;
		return;
	}

	if (gNvml.init() != NVML_SUCCESS) {
		qCWarning(logGpu) << "nvmlInit_v2 failed";
		dlclose(mNvmlLib);
		mNvmlLib = nullptr;
		return;
	}

	mNvmlOk = true;
}

void GpuScanner::shutdownNvml() {
	if (!mNvmlOk || !mNvmlLib) return;
	if (gNvml.shutdown) gNvml.shutdown();
	dlclose(mNvmlLib);
	mNvmlLib = nullptr;
	mNvmlOk = false;
}

void GpuScanner::sampleAmd(const AmdCard& card, QVariantList& out) {
	QVariantMap m;
	m[QStringLiteral("vendor")] = QStringLiteral("AMD");
	m[QStringLiteral("name")] = card.name;

	auto util = readLongLong(card.path + "/gpu_busy_percent");
	m[QStringLiteral("utilization")] = util;

	auto vramUsed = readLongLong(card.path + "/mem_info_vram_used");
	auto vramTotal = readLongLong(card.path + "/mem_info_vram_total");
	m[QStringLiteral("vramUsed")] = vramUsed;
	m[QStringLiteral("vramTotal")] = vramTotal;

	m[QStringLiteral("clock")] = parseActiveDpmMhz(card.path + "/pp_dpm_sclk");

	qreal power = -1;
	qreal temperature = -1;
	auto hwmon = findHwmonDir(card.path);
	if (!hwmon.isEmpty()) {
		// AMD exposes power1_average in microwatts.
		auto uw = readLongLong(hwmon + "/power1_average");
		if (uw > 0) power = uw / 1'000'000.0;
		auto mc = readLongLong(hwmon + "/temp1_input");
		if (mc > 0) temperature = mc / 1000.0;
	}
	m[QStringLiteral("power")] = power;
	m[QStringLiteral("temperature")] = temperature;
	m[QStringLiteral("connectedDisplay")] = cardHasConnectedDisplay(card.cardName);

	out.append(m);
}

void GpuScanner::sampleIntel(const IntelCard& card, QVariantList& out) {
	QVariantMap m;
	m[QStringLiteral("vendor")] = QStringLiteral("Intel");
	m[QStringLiteral("name")] = card.name;
	m[QStringLiteral("utilization")] = -1;  // i915 utilization requires PMU, skipping
	m[QStringLiteral("vramUsed")] = -1;
	m[QStringLiteral("vramTotal")] = -1;
	m[QStringLiteral("power")] = -1;

	auto clock = readLongLong(card.path + "/gt_act_freq_mhz");
	m[QStringLiteral("clock")] = clock;
	m[QStringLiteral("temperature")] = -1;
	m[QStringLiteral("connectedDisplay")] = cardHasConnectedDisplay(card.cardName);

	out.append(m);
}

void GpuScanner::sampleNvidia(QVariantList& out) {
	initNvml();
	if (!mNvmlOk) return;

	unsigned int count = 0;
	if (gNvml.getCount(&count) != NVML_SUCCESS) return;

	for (unsigned int i = 0; i < count; ++i) {
		nvmlDevice_t dev = nullptr;
		if (gNvml.getHandle(i, &dev) != NVML_SUCCESS) continue;

		QVariantMap m;
		m[QStringLiteral("vendor")] = QStringLiteral("NVIDIA");

		char name[NVML_DEVICE_NAME_BUFFER_SIZE] = {};
		if (gNvml.getName(dev, name, NVML_DEVICE_NAME_BUFFER_SIZE) == NVML_SUCCESS)
			m[QStringLiteral("name")] = QString::fromUtf8(name);
		else
			m[QStringLiteral("name")] = QStringLiteral("NVIDIA GPU");

		nvmlUtilization_t util = {};
		m[QStringLiteral("utilization")]
		    = gNvml.getUtilization(dev, &util) == NVML_SUCCESS ? qlonglong(util.gpu) : -1;

		nvmlMemory_t mem = {};
		if (gNvml.getMemory(dev, &mem) == NVML_SUCCESS) {
			m[QStringLiteral("vramUsed")] = qlonglong(mem.used);
			m[QStringLiteral("vramTotal")] = qlonglong(mem.total);
		} else {
			m[QStringLiteral("vramUsed")] = -1;
			m[QStringLiteral("vramTotal")] = -1;
		}

		unsigned int powerMw = 0;
		m[QStringLiteral("power")]
		    = gNvml.getPower(dev, &powerMw) == NVML_SUCCESS ? powerMw / 1000.0 : -1.0;

		unsigned int clockMhz = 0;
		m[QStringLiteral("clock")]
		    = gNvml.getClock(dev, NVML_CLOCK_GRAPHICS, &clockMhz) == NVML_SUCCESS
		        ? int(clockMhz)
		        : -1;

		unsigned int tempC = 0;
		m[QStringLiteral("temperature")]
		    = gNvml.getTemperature(dev, NVML_TEMPERATURE_GPU, &tempC) == NVML_SUCCESS
		        ? qreal(tempC)
		        : -1.0;

		// Match this NVML device to a DRM card by PCI bus id so we can
		// surface connectedDisplay consistently with AMD/Intel. When the
		// match fails (older driver without getPciInfo_v3, virtual device,
		// etc.) fall back to the first NVIDIA card's status if there's
		// exactly one — otherwise treat as not-connected rather than lie.
		bool connected = false;
		bool matched = false;
		if (gNvml.getPciInfo) {
			nvmlPciInfo_t pci = {};
			if (gNvml.getPciInfo(dev, &pci) == NVML_SUCCESS) {
				auto busId = QString::fromLatin1(pci.busId).toLower();
				for (const auto& card : mNvidiaCards) {
					if (card.pciBusId.toLower() == busId) {
						connected = cardHasConnectedDisplay(card.cardName);
						matched = true;
						break;
					}
				}
			}
		}
		if (!matched && mNvidiaCards.size() == 1) {
			connected = cardHasConnectedDisplay(mNvidiaCards.front().cardName);
		}
		m[QStringLiteral("connectedDisplay")] = connected;

		out.append(m);
	}
}

QVariantList GpuScanner::sample() {
	QVariantList out;
	for (const auto& c : mAmdCards) sampleAmd(c, out);
	for (const auto& c : mIntelCards) sampleIntel(c, out);
	sampleNvidia(out);
	return out;
}

} // namespace qs::service::sysinfo
