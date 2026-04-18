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
	/// power, clock, temperature } — one entry per detected GPU.
	/// Unavailable fields are -1.
	QVariantList sample();

private:
	struct AmdCard {
		QString path;       // /sys/class/drm/cardN/device
		QString name;       // e.g. "AMD Radeon ..."
	};

	struct IntelCard {
		QString path;       // /sys/class/drm/cardN
		QString name;
	};

	void scanDrmCards();
	void sampleAmd(const AmdCard& card, QVariantList& out);
	void sampleIntel(const IntelCard& card, QVariantList& out);
	void sampleNvidia(QVariantList& out);

	// NVML dlopen state
	void initNvml();
	void shutdownNvml();
	bool mNvmlAttempted = false;
	void* mNvmlLib = nullptr;
	bool mNvmlOk = false;

	std::vector<AmdCard> mAmdCards;
	std::vector<IntelCard> mIntelCards;
};

} // namespace qs::service::sysinfo
