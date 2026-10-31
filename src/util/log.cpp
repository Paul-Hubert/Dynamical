#include "log.h"

#include <filesystem>
#include <fstream>
#include <memory>

using namespace dy;

namespace {

// Writes to the console stream and to the log file, flushing the file at each end of line
class TeeBuffer : public std::streambuf {
public:
    TeeBuffer(std::streambuf* console, std::ofstream& file) : console(console), file(file) {}

protected:
    int overflow(int c) override {
        if(c == EOF) return !EOF;
        console->sputc((char) c);
        file.put((char) c);
        if(c == '\n') {
            console->pubsync();
            file.flush();
        }
        return c;
    }

    std::streamsize xsputn(const char* s, std::streamsize n) override {
        for(std::streamsize i = 0; i < n; i++) overflow((unsigned char) s[i]);
        return n;
    }

    int sync() override {
        console->pubsync();
        file.flush();
        return 0;
    }

private:
    std::streambuf* console;
    std::ofstream& file;
};

struct FileSink {
    std::ofstream file;
    std::unique_ptr<TeeBuffer> out_buffer, err_buffer;
    std::unique_ptr<std::ostream> out, err;
};

std::unique_ptr<FileSink>& sink() {
    static std::unique_ptr<FileSink> s;
    return s;
}

}

bool dy::setLogFile(const std::string& path) {
    auto s = std::make_unique<FileSink>();
    std::error_code ec;
    std::filesystem::path p(path);
    if(p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);
    s->file.open(path, std::ios::binary | std::ios::app);
    if(!s->file) return false;
    s->out_buffer = std::make_unique<TeeBuffer>(std::cout.rdbuf(), s->file);
    s->err_buffer = std::make_unique<TeeBuffer>(std::cerr.rdbuf(), s->file);
    s->out = std::make_unique<std::ostream>(s->out_buffer.get());
    s->err = std::make_unique<std::ostream>(s->err_buffer.get());
    sink() = std::move(s);
    return true;
}

std::ostream& dy::log(dy::Level l) {
    auto& s = sink();
    switch (l) {
    case trace:
    case debug:
    case info:
        return s ? *s->out : std::cout;
    case warning:
    case error:
    case critical:
        return s ? *s->err : std::cerr;
    default:
        return s ? *s->out : std::cout;
    }
}
