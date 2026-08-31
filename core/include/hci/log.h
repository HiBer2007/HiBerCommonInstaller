#pragma once

#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace hci {

enum class LogLevel { Trace, Debug, Info, Warn, Error };

class ILogSink {
public:
    virtual ~ILogSink() = default;
    virtual void write(LogLevel level, const std::string& message) = 0;
    virtual const char* sinkName() const = 0;
};

// Null sink: discards everything.
class NullSink : public ILogSink {
public:
    void write(LogLevel, const std::string&) override {}
    const char* sinkName() const override { return "null"; }
};

// Console sink: writes UTF-8 to stdout/stderr (level >= Error -> stderr).
class ConsoleSink : public ILogSink {
public:
    explicit ConsoleSink(LogLevel minLevel = LogLevel::Info);
    void write(LogLevel level, const std::string& message) override;
    const char* sinkName() const override { return "console"; }
private:
    LogLevel minLevel_;
};

// File sink: appends UTF-8 lines to a file.
class FileSink : public ILogSink {
public:
    FileSink(const std::string& path, LogLevel minLevel = LogLevel::Info);
    ~FileSink() override;
    void write(LogLevel level, const std::string& message) override;
    const char* sinkName() const override { return "file"; }
private:
    std::string path_;
    LogLevel minLevel_;
    void* handle_ = nullptr; // FILE*
};

// Log facade: multi-sink, thread-safe. Extension log injection lives in
// hci_ext (SetPluginLogSink-style export symbol).
class Log {
public:
    static Log& instance();

    void addSink(std::shared_ptr<ILogSink> sink);
    void removeSink(ILogSink* sink);
    void setLevel(LogLevel level);
    LogLevel level() const { return level_; }

    void write(LogLevel level, const std::string& message);

    static void Trace(const std::string& m) { instance().write(LogLevel::Trace, m); }
    static void Debug(const std::string& m) { instance().write(LogLevel::Debug, m); }
    static void Info(const std::string& m)  { instance().write(LogLevel::Info, m); }
    static void Warn(const std::string& m)  { instance().write(LogLevel::Warn, m); }
    static void Error(const std::string& m) { instance().write(LogLevel::Error, m); }

private:
    Log() = default;
    std::vector<std::shared_ptr<ILogSink>> sinks_;
    mutable std::mutex mutex_;
    LogLevel level_ = LogLevel::Info;
};

// Simple variadic format helper (C++17): hci::fmt("a={} b={}", 1, "x").
// Unknown "{}" placeholders are left as-is when arguments run out.
namespace detail {
inline void fmtOut(std::ostringstream&, const std::string&, size_t&) {}

template <typename T, typename... Rest>
void fmtOut(std::ostringstream& os, const std::string& pattern, size_t& pos,
            T&& v, Rest&&... rest)
{
    const size_t br = pattern.find("{}", pos);
    if (br == std::string::npos) {
        os << pattern.substr(pos);
        pos = pattern.size();
        return;
    }
    os << pattern.substr(pos, br - pos) << std::forward<T>(v);
    pos = br + 2;
    fmtOut(os, pattern, pos, std::forward<Rest>(rest)...);
}

inline void fmtTail(std::ostringstream& os, const std::string& pattern, size_t pos)
{
    if (pos < pattern.size()) os << pattern.substr(pos);
}
} // namespace detail

template <typename... Args>
inline std::string fmt(const std::string& pattern, Args&&... args)
{
    std::ostringstream os;
    size_t pos = 0;
    detail::fmtOut(os, pattern, pos, std::forward<Args>(args)...);
    detail::fmtTail(os, pattern, pos);
    return os.str();
}

} // namespace hci