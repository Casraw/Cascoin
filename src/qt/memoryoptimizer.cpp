#include <qt/memoryoptimizer.h>
#include <wallet/wallet.h> // For CBeeCreationTransactionInfo

#include <QDebug>
#include <QFile>
#include <QIODevice>
#include <QProcess>
#include <QSysInfo>
#include <QTextStream>
#include <algorithm>

#ifdef Q_OS_LINUX
#include <sys/sysinfo.h>
#include <unistd.h>
#endif

#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif

#ifdef Q_OS_MAC
#include <sys/types.h>
#include <sys/sysctl.h>
#include <mach/mach.h>
#endif

MemoryOptimizer& MemoryOptimizer::instance()
{
    static MemoryOptimizer instance;
    return instance;
}

MemoryOptimizer::MemoryOptimizer()
    : m_lowMemoryMode(false)
{
    // Setup periodic cleanup timer
    m_cleanupTimer = new QTimer(this);
    m_cleanupTimer->setInterval(60000); // 1 minute
    connect(m_cleanupTimer, &QTimer::timeout, this, &MemoryOptimizer::performPeriodicCleanup);
    m_cleanupTimer->start();
    
    // Check initial memory state
    MemoryStats stats = getMemoryStats();
    if (stats.availableRAM < 1024 * 1024 * 1024) { // Less than 1GB available
        enableLowMemoryMode(true);
        Q_EMIT lowMemoryWarning(stats.availableRAM);
    }
}

MemoryOptimizer::MemoryStats MemoryOptimizer::getMemoryStats() const
{
    MemoryStats stats = {0, 0, 0, 0.0};
    
    stats.totalRAM = getSystemRAM();
    stats.availableRAM = getAvailableRAM();
    stats.processRAM = getProcessRAM();
    
    if (stats.totalRAM > 0) {
        stats.ramUsagePercent = (double(stats.totalRAM - stats.availableRAM) / double(stats.totalRAM)) * 100.0;
    }
    
    return stats;
}

qint64 MemoryOptimizer::getSystemRAM() const
{
#ifdef Q_OS_LINUX
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        return info.totalram * info.mem_unit;
    }
#endif

#ifdef Q_OS_WIN
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    if (GlobalMemoryStatusEx(&memInfo)) {
        return memInfo.ullTotalPhys;
    }
#endif

#ifdef Q_OS_MAC
    int64_t memSize;
    size_t size = sizeof(memSize);
    if (sysctlbyname("hw.memsize", &memSize, &size, NULL, 0) == 0) {
        return memSize;
    }
#endif

    return 0; // Unable to determine
}

qint64 MemoryOptimizer::getAvailableRAM() const
{
#ifdef Q_OS_LINUX
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        return (info.freeram + info.bufferram + info.sharedram) * info.mem_unit;
    }
#endif

#ifdef Q_OS_WIN
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    if (GlobalMemoryStatusEx(&memInfo)) {
        return memInfo.ullAvailPhys;
    }
#endif

#ifdef Q_OS_MAC
    vm_size_t page_size;
    vm_statistics64_data_t vm_stats;
    mach_msg_type_number_t host_size = sizeof(vm_statistics64_data_t) / sizeof(natural_t);
    
    host_page_size(mach_host_self(), &page_size);
    if (host_statistics64(mach_host_self(), HOST_VM_INFO, (host_info64_t)&vm_stats, &host_size) == KERN_SUCCESS) {
        return (vm_stats.free_count + vm_stats.inactive_count) * page_size;
    }
#endif

    return 0;
}

qint64 MemoryOptimizer::getProcessRAM() const
{
#ifdef Q_OS_LINUX
    QFile file("/proc/self/status");
    if (file.open(QIODevice::ReadOnly)) {
        while (!file.atEnd()) {
            QByteArray line = file.readLine();
            if (line.startsWith("VmRSS:")) {
                QList<QByteArray> parts = line.split(' ');
                if (parts.size() >= 2) {
                    return parts[1].toLongLong() * 1024; // Convert from KB to bytes
                }
            }
        }
    }
#endif

#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return pmc.WorkingSetSize;
    }
#endif

#ifdef Q_OS_MAC
    struct mach_task_basic_info info;
    mach_msg_type_number_t infoCount = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &infoCount) == KERN_SUCCESS) {
        return info.resident_size;
    }
#endif

    return 0;
}

void MemoryOptimizer::enableLowMemoryMode(bool enable)
{
    QMutexLocker locker(&m_mutex);
    if (m_lowMemoryMode != enable) {
        m_lowMemoryMode = enable;
        qDebug() << "Low memory mode" << (enable ? "enabled" : "disabled");
        
        if (enable) {
            adjustCacheSizes();
        }
    }
}

bool MemoryOptimizer::isLowMemoryMode() const
{
    QMutexLocker locker(&m_mutex);
    return m_lowMemoryMode;
}

int MemoryOptimizer::getRecommendedCacheSize() const
{
    MemoryStats stats = getMemoryStats();
    
    if (m_lowMemoryMode || stats.availableRAM < 512 * 1024 * 1024) { // Less than 512MB available
        return 32; // Very conservative
    } else if (stats.availableRAM < 1024 * 1024 * 1024) { // Less than 1GB available
        return 64;
    } else if (stats.availableRAM < 2LL * 1024 * 1024 * 1024) { // Less than 2GB available
        return 128;
    } else {
        return 256; // Plenty of RAM available
    }
}

void MemoryOptimizer::adjustCacheSizes()
{
    // This would be implemented to adjust various caches in the application
    // For now, it's a placeholder for future cache adjustments
    qDebug() << "Adjusting cache sizes based on memory availability";
}

void MemoryOptimizer::requestGarbageCollection()
{
    // Trigger cleanup of various caches and temporary data
    performPeriodicCleanup();
}

void MemoryOptimizer::performPeriodicCleanup()
{
    MemoryStats stats = getMemoryStats();
    
    // Check if we're running low on memory
    if (stats.availableRAM < 256 * 1024 * 1024 && !m_lowMemoryMode) { // Less than 256MB
        enableLowMemoryMode(true);
        Q_EMIT lowMemoryWarning(stats.availableRAM);
    } else if (stats.availableRAM > 1024 * 1024 * 1024 && m_lowMemoryMode) { // More than 1GB
        enableLowMemoryMode(false);
    }
    
    Q_EMIT memoryStatsChanged(stats);
}

void MemoryOptimizer::handleLowMemoryWarning()
{
    qDebug() << "Handling low memory warning - performing aggressive cleanup";
    enableLowMemoryMode(true);
    requestGarbageCollection();
}

// BCTCache implementation
BCTCache::BCTCache(size_t maxMemoryMB)
    : m_maxMemoryBytes(maxMemoryMB * 1024 * 1024)
    , m_currentMemoryBytes(0)
{
}

bool BCTCache::get(const QString& key, std::vector<CBeeCreationTransactionInfo>& result, bool includeExpired)
{
    QMutexLocker locker(&m_mutex);
    
    auto it = m_cache.find(key);
    if (it != m_cache.end()) {
        // Check if cache entry matches our requirements and is not too old
        auto now = std::chrono::steady_clock::now();
        auto age = std::chrono::duration_cast<std::chrono::minutes>(now - it->second.lastAccess);
        
        if (age.count() < 15 && it->second.includesExpired >= includeExpired) {
            it->second.lastAccess = now; // Update access time
            result = it->second.data;
            return true;
        } else {
            // Remove stale entry
            m_currentMemoryBytes -= it->second.memorySize;
            m_cache.erase(it);
        }
    }
    
    return false;
}

void BCTCache::put(const QString& key, const std::vector<CBeeCreationTransactionInfo>& data, bool includeExpired)
{
    QMutexLocker locker(&m_mutex);
    
    size_t entrySize = calculateMemorySize(data);
    
    // Check if we need to evict entries to make room
    while (m_currentMemoryBytes + entrySize > m_maxMemoryBytes && !m_cache.empty()) {
        evictOldest();
    }
    
    // Don't cache if entry is too large or if there are too many entries
    if (entrySize > m_maxMemoryBytes / 2 || data.size() > 1000) {
        qDebug() << "Skipping cache: entry too large (" << entrySize << "bytes) or too many entries (" << data.size() << ")";
        return;
    }
    
    BCTCacheEntry entry;
    entry.data = data;
    entry.lastAccess = std::chrono::steady_clock::now();
    entry.includesExpired = includeExpired;
    entry.memorySize = entrySize;
    
    // Remove existing entry if present
    auto it = m_cache.find(key);
    if (it != m_cache.end()) {
        m_currentMemoryBytes -= it->second.memorySize;
    }
    
    m_cache[key] = entry;
    m_currentMemoryBytes += entrySize;
}

void BCTCache::evictOldest()
{
    if (m_cache.empty()) return;
    
    auto oldest = m_cache.begin();
    for (auto it = m_cache.begin(); it != m_cache.end(); ++it) {
        if (it->second.lastAccess < oldest->second.lastAccess) {
            oldest = it;
        }
    }
    
    m_currentMemoryBytes -= oldest->second.memorySize;
    m_cache.erase(oldest);
}

size_t BCTCache::calculateMemorySize(const std::vector<CBeeCreationTransactionInfo>& data) const
{
    if (data.empty()) return 0;
    
    // Conservative estimate: each BCT entry takes ~512 bytes on average
    size_t totalSize = data.size() * 512;
    
    // Cap the estimate to prevent cache overflow
    if (totalSize > m_maxMemoryBytes / 4) {
        qDebug() << "Warning: BCT cache entry size estimate" << totalSize << "bytes is very large for" << data.size() << "entries";
        // Return a capped size to prevent cache overflow
        return m_maxMemoryBytes / 4;
    }
    
    return totalSize;
}

size_t BCTCache::currentMemoryUsage() const
{
    QMutexLocker locker(&m_mutex);
    return m_currentMemoryBytes;
}

void BCTCache::setMaxMemory(size_t maxMemoryMB)
{
    QMutexLocker locker(&m_mutex);
    m_maxMemoryBytes = maxMemoryMB * 1024 * 1024;
    enforceMemoryLimit();
}

void BCTCache::enforceMemoryLimit()
{
    while (m_currentMemoryBytes > m_maxMemoryBytes && !m_cache.empty()) {
        evictOldest();
    }
}

void BCTCache::clear()
{
    QMutexLocker locker(&m_mutex);
    m_cache.clear();
    m_currentMemoryBytes = 0;
}

#include "moc_memoryoptimizer.cpp"
