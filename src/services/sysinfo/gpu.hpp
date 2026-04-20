#pragma once

#include <qstring.h>
#include <qvariant.h>

#include <vector>

namespace qs::service::sysinfo {

/// One-shot per-tick GPU sampler.
/// Vendor detected via /sys/class/drm/card*/device/vendor.
/// AMD (0x1002): pure sysfs.
/// NVIDIA (0x10de): libnvidia-ml.so.1 via dlopen; gracefully disabled when absent.
/// Intel (0x8086): sysfs (partial — only what the kernel exposes).
class GpuScanner {
public:
	GpuScanner();
	~GpuScanner();

	GpuScanner(const GpuScanner&) = delete;
	GpuScanner& operator=(const GpuScanner&) = delete;

	/// Returns list of { vendor, name, utilization, vramUsed, vramTotal,
	/// power, clock, temperature, connectedDisplay } — one entry per
	/// detected GPU. `connectedDisplay` is true when at least one
	/// connector on the card reports "connected" in sysfs; false otherwise.
	/// Unavailable numeric fields are -1.
	QVariantList sample();

private:
	struct AmdCard {
		QString cardName;   // "card0"
		QString path;       // /sys/class/drm/cardN/device
		QString name;       // e.g. "AMD Radeon ..."
	};

	struct IntelCard {
		QString cardName;   // "card0"
		QString path;       // /sys/class/drm/cardN
		QString name;
	};

	struct NvidiaCard {
		QString cardName;   // "card0"
		QString pciBusId;   // e.g. "0000:01:00.0" — used to match NVML devices
	};

	void scanDrmCards();
	void sampleAmd(const AmdCard& card, QVariantList& out);
	void sampleIntel(const IntelCard& card, QVariantList& out);
	void sampleNvidia(QVariantList& out);

	// True when /sys/class/drm/<cardName>-*/status reports "connected" for
	// at least one connector.
	static bool cardHasConnectedDisplay(const QString& cardName);

	// NVML dlopen state
	void initNvml();
	void shutdownNvml();
	bool mNvmlAttempted = false;
	void* mNvmlLib = nullptr;
	bool mNvmlOk = false;

	std::vector<AmdCard> mAmdCards;
	std::vector<IntelCard> mIntelCards;
	std::vector<NvidiaCard> mNvidiaCards;
};

} // namespace qs::service::sysinfo
