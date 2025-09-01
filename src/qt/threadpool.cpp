#include <qt/threadpool.h>

#include <QThread>
#include <QDebug>
#include <thread>
#include <stdexcept>

BackgroundTaskRunner::BackgroundTaskRunner(std::function<void()> task, QObject* parent)
    : QObject(parent), m_task(std::move(task))
{
    setAutoDelete(true);
}

void BackgroundTaskRunner::run()
{
    try {
        if (m_task) {
            m_task();
        }
        Q_EMIT finished();
    } catch (const std::exception& e) {
        Q_EMIT error(QString::fromStdString(e.what()));
    } catch (...) {
        Q_EMIT error("Unknown error occurred in background task");
    }
}

void BackgroundTaskRunner::setAutoDelete(bool autoDelete)
{
    QRunnable::setAutoDelete(autoDelete);
}

ProgressTaskRunner::ProgressTaskRunner(std::function<void(std::function<void(int)>)> task, QObject* parent)
    : QObject(parent), m_task(std::move(task))
{
    setAutoDelete(true);
}

void ProgressTaskRunner::run()
{
    try {
        if (m_task) {
            // Create progress callback
            auto progressCallback = [this](int percentage) {
                Q_EMIT progress(percentage);
            };
            
            m_task(progressCallback);
        }
        Q_EMIT finished();
    } catch (const std::exception& e) {
        Q_EMIT error(QString::fromStdString(e.what()));
    } catch (...) {
        Q_EMIT error("Unknown error occurred in progress task");
    }
}

CascoinThreadPool& CascoinThreadPool::instance()
{
    static CascoinThreadPool instance;
    return instance;
}

CascoinThreadPool::CascoinThreadPool()
    : m_threadPool(new QThreadPool(this))
{
    // Set optimal thread count based on CPU cores, but cap it for memory reasons
    int optimalThreads = getOptimalThreadCount();
    m_threadPool->setMaxThreadCount(optimalThreads);
    
    qDebug() << "CascoinThreadPool initialized with" << optimalThreads << "threads";
}

CascoinThreadPool::~CascoinThreadPool()
{
    waitForAllTasks(5000); // Wait 5 seconds for cleanup
}

void CascoinThreadPool::executeAsync(std::function<void()> task, QObject* receiver, 
                                   const char* finishedSlot)
{
    BackgroundTaskRunner* runner = new BackgroundTaskRunner(std::move(task));
    
    if (receiver && finishedSlot) {
        connect(runner, SIGNAL(finished()), receiver, finishedSlot, Qt::QueuedConnection);
    }
    
    connect(runner, &BackgroundTaskRunner::error, this, [](const QString& error) {
        qDebug() << "Background task error:" << error;
    });
    
    // Limit concurrent tasks to prevent memory overflow
    if (m_threadPool->activeThreadCount() > m_threadPool->maxThreadCount() * 2) {
        qWarning() << "ThreadPool overloaded (" << m_threadPool->activeThreadCount() 
                   << " active), skipping new task to prevent memory issues";
        runner->deleteLater();
        return;
    }
    
    m_threadPool->start(runner);
}

void CascoinThreadPool::executeWithProgress(std::function<void(std::function<void(int)>)> task,
                                          QObject* receiver, const char* progressSlot,
                                          const char* finishedSlot)
{
    ProgressTaskRunner* runner = new ProgressTaskRunner(std::move(task));
    
    if (receiver) {
        if (progressSlot) {
            connect(runner, SIGNAL(progress(int)), receiver, progressSlot, Qt::QueuedConnection);
        }
        if (finishedSlot) {
            connect(runner, SIGNAL(finished()), receiver, finishedSlot, Qt::QueuedConnection);
        }
    }
    
    connect(runner, &ProgressTaskRunner::error, this, [](const QString& error) {
        qDebug() << "Progress task error:" << error;
    });
    
    m_threadPool->start(runner);
}

int CascoinThreadPool::getOptimalThreadCount() const
{
    // Use hardware concurrency but cap at 8 to avoid excessive memory usage
    int hwThreads = std::thread::hardware_concurrency();
    if (hwThreads == 0) hwThreads = 4; // fallback
    
    // Cap at 8 threads for memory efficiency, min 2 threads
    return std::max(2, std::min(8, hwThreads));
}

void CascoinThreadPool::setMaxThreadCount(int maxThreads)
{
    QMutexLocker locker(&m_mutex);
    m_threadPool->setMaxThreadCount(std::max(1, maxThreads));
}

void CascoinThreadPool::waitForAllTasks(int timeoutMs)
{
    m_threadPool->waitForDone(timeoutMs);
}

#include "moc_threadpool.cpp"
