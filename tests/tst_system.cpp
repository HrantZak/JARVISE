#include <QTest>

#include <chrono>
#include <thread>

#include "jarvis/system/NvmlGpuMonitor.h"
#include "jarvis/system/WindowsSystemMetricsProvider.h"

using namespace jarvis::system;

class TestSystem : public QObject {
    Q_OBJECT

private slots:
    void hardwareProfileIsPopulated();
    void firstSampleHasNoRateBaseline();
    void memoryReadingIsSelfConsistent();
    void cpuUsageStaysInRange();
    void networkRatesBecomeAvailable();
    void gpuIsEitherFullyAvailableOrFullyAbsent();
    void nvmlExplainsItselfWhenUnavailable();
};

void TestSystem::hardwareProfileIsPopulated() {
    WindowsSystemMetricsProvider provider;
    const HardwareProfile& profile = provider.profile();

    QVERIFY2(!profile.cpuName.empty(), "the CPU name must come from the registry");
    QVERIFY(profile.logicalProcessors > 0);
    QVERIFY2(profile.physicalCores > 0, "physical core count must be resolved");
    QVERIFY(profile.physicalCores <= profile.logicalProcessors);
    QVERIFY(profile.totalRamBytes > 0);
    QVERIFY2(!profile.osName.empty(), "the OS product name must come from the registry");
}

void TestSystem::firstSampleHasNoRateBaseline() {
    WindowsSystemMetricsProvider provider;

    const auto first = provider.sample();
    QVERIFY(first.has_value());

    // CPU and network are deltas: the very first call has nothing to subtract
    // from, and must say so rather than reporting a fabricated 0%.
    QVERIFY2(!first->cpu.valid, "the first CPU sample cannot be valid");
    QVERIFY2(!first->network.valid, "the first network sample cannot be valid");

    // Memory is an absolute reading and is available immediately.
    QVERIFY(first->memory.valid);
}

void TestSystem::memoryReadingIsSelfConsistent() {
    WindowsSystemMetricsProvider provider;

    const auto snapshot = provider.sample();
    QVERIFY(snapshot.has_value());

    const MemoryMetrics& memory = snapshot->memory;
    QVERIFY(memory.valid);
    QVERIFY(memory.totalBytes > 0);
    QVERIFY(memory.usedBytes <= memory.totalBytes);
    QVERIFY(memory.usagePercent >= 0.0);
    QVERIFY(memory.usagePercent <= 100.0);

    // The reported total must agree with the profile read at construction.
    QCOMPARE(memory.totalBytes, provider.profile().totalRamBytes);
}

void TestSystem::cpuUsageStaysInRange() {
    WindowsSystemMetricsProvider provider;

    QVERIFY(provider.sample().has_value());               // establish the baseline
    std::this_thread::sleep_for(std::chrono::milliseconds{120});

    const auto second = provider.sample();
    QVERIFY(second.has_value());
    QVERIFY2(second->cpu.valid, "the second CPU sample must have an interval to work with");
    QVERIFY(second->cpu.usagePercent >= 0.0);
    QVERIFY(second->cpu.usagePercent <= 100.0);
}

void TestSystem::networkRatesBecomeAvailable() {
    WindowsSystemMetricsProvider provider;

    QVERIFY(provider.sample().has_value());
    std::this_thread::sleep_for(std::chrono::milliseconds{120});

    const auto second = provider.sample();
    QVERIFY(second.has_value());
    QVERIFY(second->network.valid);
}

void TestSystem::gpuIsEitherFullyAvailableOrFullyAbsent() {
    WindowsSystemMetricsProvider provider;
    const auto snapshot = provider.sample();
    QVERIFY(snapshot.has_value());

    const GpuMetrics& gpu = snapshot->gpu;

    if (gpu.available) {
        QVERIFY2(gpu.vramTotalBytes > 0, "an available GPU must report its VRAM size");
        QVERIFY(gpu.vramUsedBytes <= gpu.vramTotalBytes);
        QVERIFY(gpu.utilizationPercent >= 0.0);
        QVERIFY(gpu.utilizationPercent <= 100.0);
        QVERIFY(gpu.vramPercent >= 0.0);
        QVERIFY(gpu.vramPercent <= 100.0);
        QVERIFY(provider.profile().gpuAvailable);
        QVERIFY(!provider.profile().gpuName.empty());
    } else {
        // An unavailable GPU must be all zeros, never a plausible-looking zero
        // load that the UI would render as a real reading.
        QCOMPARE(gpu.utilizationPercent, 0.0);
        QCOMPARE(gpu.vramTotalBytes, std::uint64_t{0});
        QCOMPARE(gpu.vramUsedBytes, std::uint64_t{0});
        QVERIFY(!gpu.temperatureValid);
        QVERIFY(!provider.gpuUnavailableReason().empty());
    }
}

void TestSystem::nvmlExplainsItselfWhenUnavailable() {
    NvmlGpuMonitor monitor;

    if (monitor.isAvailable()) {
        QVERIFY(monitor.unavailableReason().empty());
        QVERIFY(monitor.vramTotalBytes() > 0);
        QVERIFY(!monitor.deviceName().empty());
    } else {
        QVERIFY2(!monitor.unavailableReason().empty(),
                 "an unavailable GPU must explain why, for the log");
    }
}

QTEST_GUILESS_MAIN(TestSystem)

#include "tst_system.moc"
