#pragma once

#include "Logging.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace ProjectIE4k {

class LogWriter {
public:
    std::atomic<LogLevel> level;

    explicit LogWriter(LogLevel level)
        : level(level) {}
    virtual ~LogWriter() noexcept = default;

    void WriteLogMessage(LogLevel logLevel, const char* owner, const char* message)
    {
        WriteLogMessage(LogMessage(logLevel, owner, message));
    }
    virtual void WriteLogMessage(const struct LogMessage& msg) = 0;
    virtual void Flush() {};
};



class Logger {
public:
    using WriterPtr = std::shared_ptr<LogWriter>;

private:
    using QueueType = std::deque<LogMessage>;
    QueueType messageQueue;
    std::deque<WriterPtr> writers;

    std::atomic_bool running { true };
    std::condition_variable cv;
    std::mutex queueLock;
    std::mutex writerLock;
    std::thread loggingThread;

    void threadLoop();
    void ProcessMessages(QueueType queue);
    void StartProcessingThread();

public:
    explicit Logger(std::deque<WriterPtr> initialWriters);
    ~Logger();

    void AddLogWriter(WriterPtr writer);
    void LogMsg(LogLevel level, const char* owner, const char* message);
    void LogMsg(LogMessage&& msg);
    void Flush();
};

} // namespace ProjectIE4k 