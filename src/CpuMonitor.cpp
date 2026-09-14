#include "CpuMonitor.h"

#include <cwchar>

#pragma comment(lib, "pdh.lib")

CpuMonitor::CpuMonitor() = default;

CpuMonitor::~CpuMonitor()
{
    if (m_query)
    {
        PdhCloseQuery(m_query);
        m_query = nullptr;
    }
}

bool CpuMonitor::Initialize()
{
    if (PdhOpenQuery(nullptr, 0, &m_query) != ERROR_SUCCESS)
        return false;

    SYSTEM_INFO sysInfo{};
    GetSystemInfo(&sysInfo);
    const DWORD coreCount = sysInfo.dwNumberOfProcessors;

    m_coreCounters.resize(coreCount);
    m_coreHistory.resize(coreCount);
    m_coreUsage.assign(coreCount, 0.0);

    for (DWORD i = 0; i < coreCount; ++i)
    {
        wchar_t path[128];
        swprintf_s(path, L"\\Processor(%u)\\%% Processor Time", i);
        if (PdhAddEnglishCounterW(m_query, path, 0, &m_coreCounters[i]) != ERROR_SUCCESS)
            return false;
    }

    if (PdhAddEnglishCounterW(m_query, L"\\Processor(_Total)\\% Processor Time", 0, &m_totalCounter) != ERROR_SUCCESS)
        return false;

    // First collection call establishes the baseline; formatted values are
    // not meaningful until a second call has been made.
    PdhCollectQueryData(m_query);

    return true;
}

void CpuMonitor::PushSample(std::deque<double>& history, double value)
{
    history.push_back(value);
    while (history.size() > kHistoryLength)
        history.pop_front();
}

bool CpuMonitor::Update()
{
    if (!m_query)
        return false;

    if (PdhCollectQueryData(m_query) != ERROR_SUCCESS)
        return false;

    PDH_FMT_COUNTERVALUE value{};

    for (size_t i = 0; i < m_coreCounters.size(); ++i)
    {
        DWORD type = 0;
        if (PdhGetFormattedCounterValue(m_coreCounters[i], PDH_FMT_DOUBLE, &type, &value) == ERROR_SUCCESS
            && value.CStatus == ERROR_SUCCESS)
        {
            double v = value.doubleValue;
            if (v < 0.0) v = 0.0;
            if (v > 100.0) v = 100.0;
            m_coreUsage[i] = v;
            PushSample(m_coreHistory[i], v);
        }
    }

    DWORD type = 0;
    if (PdhGetFormattedCounterValue(m_totalCounter, PDH_FMT_DOUBLE, &type, &value) == ERROR_SUCCESS
        && value.CStatus == ERROR_SUCCESS)
    {
        double v = value.doubleValue;
        if (v < 0.0) v = 0.0;
        if (v > 100.0) v = 100.0;
        m_totalUsage = v;
    }

    return true;
}

double CpuMonitor::GetCoreUsage(size_t coreIndex) const
{
    if (coreIndex >= m_coreUsage.size())
        return 0.0;
    return m_coreUsage[coreIndex];
}

const std::deque<double>& CpuMonitor::GetCoreHistory(size_t coreIndex) const
{
    static const std::deque<double> empty;
    if (coreIndex >= m_coreHistory.size())
        return empty;
    return m_coreHistory[coreIndex];
}
