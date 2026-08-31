#include "hci/log.h"

#include <cstdio>
#include <cstring>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace hci {

namespace {
const char* levelTag(LogLevel lv)
{
    switch (lv) {
    case LogLevel::Trace: return "TRACE";
    case LogLevel::Debug: return "DEBUG";
    case LogLevel::Info:  return "INFO";
    case LogLevel::Warn:  return "WARN";
    case LogLevel::Error: return "ERROR";
    }
    return "?";
}
} // namespace

// ------------------------------------------------------------------
ConsoleSink::ConsoleSink(LogLevel minLevel) : minLevel_(minLevel) {}

void ConsoleSink::write(LogLevel level, const std::string& message)
{
    if (level < minLevel_) return;
    FILE* out = (level >= LogLevel::Error) ? stderr : stdout;
    std::fprintf(out, "[%s] %s\n", levelTag(level), message.c_str());
    std::fflush(out);
}

// ------------------------------------------------------------------
FileSink::FileSink(const std::string& path, LogLevel minLevel)
    : path_(path), minLevel_(minLevel)
{
#ifdef _WIN32
    // UTF-8 path -> wide for fopen
    int wlen = ::MultiByteToWideChar(CP_UTF8, 0, path_.c_str(), -1, nullptr, 0);
    if (wlen > 0) {
        std::wstring wp(wlen, L'\0');
        ::MultiByteToWideChar(CP_UTF8, 0, path_.c_str(), -1, &wp[0], wlen);
        handle_ = ::_wfopen(wp.c_str(), L"ab");
    }
#else
    handle_ = std::fopen(path_.c_str(), "ab");
#endif
}

FileSink::~FileSink()
{
    if (handle_) std::fclose(static_cast<FILE*>(handle_));
}

void FileSink::write(LogLevel level, const std::string& message)
{
    if (level < minLevel_ || !handle_) return;
    FILE* f = static_cast<FILE*>(handle_);
    std::fprintf(f, "[%s] %s\n", levelTag(level), message.c_str());
    std::fflush(f);
}

// ------------------------------------------------------------------
Log& Log::instance()
{
    static Log log;
    return log;
}

void Log::addSink(std::shared_ptr<ILogSink> sink)
{
    if (!sink) return;
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& s : sinks_) {
        if (s.get() == sink.get()) return;
    }
    sinks_.push_back(std::move(sink));
}

void Log::removeSink(ILogSink* sink)
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = sinks_.begin(); it != sinks_.end(); ++it) {
        if (it->get() == sink) {
            sinks_.erase(it);
            return;
        }
    }
}

void Log::setLevel(LogLevel level)
{
    std::lock_guard<std::mutex> lock(mutex_);
    level_ = level;
}

void Log::write(LogLevel level, const std::string& message)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (level < level_) return;
    for (auto& sink : sinks_) sink->write(level, message);
}

} // namespace hci