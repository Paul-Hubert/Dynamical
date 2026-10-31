#ifndef DY_PATHS_H
#define DY_PATHS_H

#include <filesystem>

namespace dy {

// Directory of the running executable (GetModuleFileNameW on Windows)
std::filesystem::path executableDirectory();

}

#endif
