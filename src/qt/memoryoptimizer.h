#ifndef BITCOIN_QT_MEMORYOPTIMIZER_H
#define BITCOIN_QT_MEMORYOPTIMIZER_H

#include <QObject>
#include <QTimer>
#include <QCache>
#include <QMutex>
#include <memory>
#include <chrono>

// Forward declarations
struct CBeeCreationTransactionInfo;

/**
 * Memory optimization utilities for Cascoin GUI
 * Provides caching, garbage collection, and memory-efficient data structures
 */
class MemoryOptimizer : public QObject
{
    Q_OBJECT

public:
    static MemoryOptimizer& instance();

    // Cache management
    template<typename Key, typename Value>
    class SmartCache {
    public:
        SmartCache(int maxCost = 50, int ttlSeconds = 300);
        
        bool insert(const Key& key, const Value& value, int cost = 1);
        Value* object(const Key& key);
        bool contains(const Key& key) const;
        void clear();
        void setMaxCost(int maxCost);
        
        // Memory-aware operations
        void compactCache();
        int memoryUsage() const;
        
    private:
        struct CacheEntry {
            Value value;
            std::chrono::steady_clock::time_point timestamp;
            int cost;
        };
        
        mutable QMutex m_mutex;
        QCache<Key, CacheEntry> m_cache;
        int m_ttlSeconds;
        
        void cleanup();
    };

    // Memory monitoring
    struct MemoryStats {
        qint64 totalRAM;
        qint64 availableRAM;
        qint64 processRAM;
        double ramUsagePercent;
    };
    
    MemoryStats getMemoryStats() const;
    
    // Memory optimization strategies
    void enableLowMemoryMode(bool enable);
    bool isLowMemoryMode() const;
    void requestGarbageCollection();
    
    // Adaptive cache sizing based on available memory
    int getRecommendedCacheSize() const;
    void adjustCacheSizes();

public Q_SLOTS:
    void performPeriodicCleanup();
    void handleLowMemoryWarning();

Q_SIGNALS:
    void lowMemoryWarning(qint64 availableRAM);
    void memoryStatsChanged(const MemoryStats& stats);

private:
    MemoryOptimizer();
    ~MemoryOptimizer() = default;
    
    QTimer* m_cleanupTimer;
    bool m_lowMemoryMode;
    mutable QMutex m_mutex;
    
    // System memory detection
    qint64 getSystemRAM() const;
    qint64 getAvailableRAM() const;
    qint64 getProcessRAM() const;
    
    // Disable copy
    MemoryOptimizer(const MemoryOptimizer&) = delete;
    MemoryOptimizer& operator=(const MemoryOptimizer&) = delete;
};

// Specialized cache for BCT data with memory-aware eviction
class BCTCache
{
public:
    struct BCTCacheEntry {
        std::vector<CBeeCreationTransactionInfo> data;
        std::chrono::steady_clock::time_point lastAccess;
        bool includesExpired;
        size_t memorySize;
    };
    
    BCTCache(size_t maxMemoryMB = 50);
    
    bool get(const QString& key, std::vector<CBeeCreationTransactionInfo>& result, bool includeExpired);
    void put(const QString& key, const std::vector<CBeeCreationTransactionInfo>& data, bool includeExpired);
    void clear();
    void evictOldest();
    
    size_t currentMemoryUsage() const;
    void setMaxMemory(size_t maxMemoryMB);

private:
    mutable QMutex m_mutex;
    std::map<QString, BCTCacheEntry> m_cache;
    size_t m_maxMemoryBytes;
    size_t m_currentMemoryBytes;
    
    size_t calculateMemorySize(const std::vector<CBeeCreationTransactionInfo>& data) const;
    void enforceMemoryLimit();
};

#endif // BITCOIN_QT_MEMORYOPTIMIZER_H
