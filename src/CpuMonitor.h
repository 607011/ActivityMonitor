#pragma once

#include <Windows.h>
#include <Pdh.h>
#include <vector>
#include <deque>
#include <string>

// Samples per-core (and overall) CPU usage using the Performance Data Helper
// (PDH) API. One PDH counter is opened per logical processor plus one for
// the "_Total" instance.
class CpuMonitor
{
public:
    CpuMonitor();
    ~CpuMonitor();

    // Opens the PDH query and adds one counter per logical CPU core.
    // Returns false if PDH initialization failed.
    bool Initialize();

    // Collects a new sample. Call this periodically (e.g. every second).
    // Returns false if the sample could not be collected (first call after
    // opening a counter typically returns no data - this is normal).
    bool Update();

    size_t GetCoreCount() const { return m_coreCounters.size(); }

    // Most recent value in percent [0..100] for a given core.
    double GetCoreUsage(size_t coreIndex) const;

    // Most recent overall ("_Total") usage in percent [0..100].
    double GetTotalUsage() const { return m_totalUsage; }

    // Rolling history for a given core, oldest first. Capped at kHistoryLength.
    const std::deque<double>& GetCoreHistory(size_t coreIndex) const;

    static constexpr size_t kHistoryLength = 300; // number of samples kept per core

private:
    PDH_HQUERY m_query = nullptr;
    std::vector<PDH_HCOUNTER> m_coreCounters;
    PDH_HCOUNTER m_totalCounter = nullptr;

    std::vector<std::deque<double>> m_coreHistory;
    std::vector<double> m_coreUsage;
    double m_totalUsage = 0.0;

    void PushSample(std::deque<double>& history, double value);
};
