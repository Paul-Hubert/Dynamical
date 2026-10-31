#include "paths.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include <vector>

std::filesystem::path dy::executableDirectory() {
#ifdef _WIN32
    std::vector<wchar_t> buffer(MAX_PATH);
    while(true) {
        DWORD length = GetModuleFileNameW(nullptr, buffer.data(), (DWORD) buffer.size());
        if(length == 0) return std::filesystem::current_path();
        if(length < buffer.size()) return std::filesystem::path(std::wstring(buffer.data(), length)).parent_path();
        buffer.resize(buffer.size() * 2);
    }
#else
    std::error_code ec;
    auto exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    return ec ? std::filesystem::current_path() : exe.parent_path();
#endif
}
