#include "Logger.h"

namespace ProjectIE4k {

Logger::Logger(std::deque<WriterPtr> initialWriters)
    : writers(std::move(initialWriters))
{
    StartProcessingThread();
}

Logger::~Logger() {
    {
        std::lock_guard<std::mutex> lock(queueLock);
        running = false;
    }
    cv.notify_all();
    
    if (loggingThread.joinable()) {
        loggingThread.join();
    }
}

void Logger::AddLogWriter(WriterPtr writer) {
    std::lock_guard<std::mutex> lock(writerLock);
    writers.push_back(std::move(writer));
}

void Logger::LogMsg(LogLevel level, const char* owner, const char* message) {
    LogMsg(LogMessage(level, owner, message));
}

void Logger::LogMsg(LogMessage&& msg) {
    {
        std::lock_guard<std::mutex> lock(queueLock);
        messageQueue.push_back(std::move(msg));
    }
    cv.notify_one();
}

void Logger::Flush() {
    QueueType queue;
    {
        std::lock_guard<std::mutex> lock(queueLock);
        queue = std::move(messageQueue);
    }
    
    if (!queue.empty()) {
        ProcessMessages(std::move(queue));
    }
}

void Logger::StartProcessingThread() {
    loggingThread = std::thread(&Logger::threadLoop, this);
}

void Logger::threadLoop() {
    while (running) {
        QueueType queue;
        
        {
            std::unique_lock<std::mutex> lock(queueLock);
            cv.wait(lock, [this] { return !messageQueue.empty() || !running; });
            
            if (!running && messageQueue.empty()) {
                break;
            }
            
            queue = std::move(messageQueue);
        }
        
        ProcessMessages(std::move(queue));
    }
}

void Logger::ProcessMessages(QueueType queue) {
    std::lock_guard<std::mutex> lock(writerLock);
    
    for (const auto& msg : queue) {
        for (const auto& writer : writers) {
            if (msg.level <= writer->level.load()) {
                writer->WriteLogMessage(msg);
            }
        }
    }
}

} // namespace ProjectIE4k 