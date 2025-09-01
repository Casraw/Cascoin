#ifndef BITCOIN_QT_THREADPOOL_H
#define BITCOIN_QT_THREADPOOL_H

#include <QObject>
#include <QThreadPool>
#include <QRunnable>
#include <QMutex>
#include <functional>

/**
 * A centralized thread pool for background operations to improve GUI responsiveness
 * and distribute heavy computational loads efficiently.
 */
class BackgroundTaskRunner : public QObject, public QRunnable
{
    Q_OBJECT

public:
    BackgroundTaskRunner(std::function<void()> task, QObject* parent = nullptr);
    
    void run() override;
    void setAutoDelete(bool autoDelete);

Q_SIGNALS:
    void finished();
    void error(const QString& message);

private:
    std::function<void()> m_task;
};

/**
 * Centralized thread pool manager for Cascoin GUI operations
 */
class CascoinThreadPool : public QObject
{
    Q_OBJECT

public:
    static CascoinThreadPool& instance();
    
    // Execute a task in the background thread pool
    void executeAsync(std::function<void()> task, QObject* receiver = nullptr, 
                     const char* finishedSlot = nullptr);
    
    // Execute with progress callback
    void executeWithProgress(std::function<void(std::function<void(int)>)> task,
                           QObject* receiver, const char* progressSlot,
                           const char* finishedSlot = nullptr);
    
    // Get optimal thread count based on CPU
    int getOptimalThreadCount() const;
    
    // Set maximum thread count (default is CPU cores)
    void setMaxThreadCount(int maxThreads);
    
    // Wait for all active tasks to complete
    void waitForAllTasks(int timeoutMs = 30000);

private:
    CascoinThreadPool();
    ~CascoinThreadPool();
    
    QThreadPool* m_threadPool;
    QMutex m_mutex;
    
    // Disable copy
    CascoinThreadPool(const CascoinThreadPool&) = delete;
    CascoinThreadPool& operator=(const CascoinThreadPool&) = delete;
};

// Progress tracking task runner
class ProgressTaskRunner : public QObject, public QRunnable
{
    Q_OBJECT

public:
    ProgressTaskRunner(std::function<void(std::function<void(int)>)> task, QObject* parent = nullptr);
    void run() override;

Q_SIGNALS:
    void progress(int percentage);
    void finished();
    void error(const QString& message);

private:
    std::function<void(std::function<void(int)>)> m_task;
};

#endif // BITCOIN_QT_THREADPOOL_H
