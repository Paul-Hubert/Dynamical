#include "log.h"
#include <mutex>

using namespace dy;

// Null stream implementation for discarding filtered messages
class NullStreambuf : public std::streambuf {
public:
    std::streamsize xsputn(const char*, std::streamsize n) override {
        return n;  // Pretend we wrote n characters
    }
    int overflow(int c) override {
        return c;  // Pretend we processed the character
    }
};

static NullStreambuf g_nullBuf;
static std::ostream g_nullStream(&g_nullBuf);

// Global log level (default: info - filters out debug and trace)
static dy::Level g_minLevel = dy::Level::info;
static std::mutex g_logMutex;

std::ostream& dy::log(dy::Level l) {
    std::lock_guard<std::mutex> lock(g_logMutex);

    // Filter: only log if level >= minimum
    if (l < g_minLevel) {
        return g_nullStream;
    }

    // Route to cout/cerr as before
    switch (l) {
    case trace:
    case debug:
    case info:
        return std::cout;
    case warning:
    case error:
    case critical:
        return std::cerr;
    default:
        return std::cout;
    }
}

void dy::set_log_level(dy::Level l) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    g_minLevel = l;
}

dy::Level dy::get_log_level() {
    std::lock_guard<std::mutex> lock(g_logMutex);
    return g_minLevel;
}
