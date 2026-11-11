#ifndef DY_PATHS_H
#define DY_PATHS_H

#include <filesystem>

namespace dy {

// Directory of the running executable (GetModuleFileNameW on Windows)
std::filesystem::path executableDirectory();

// A file under resources/: next to the working directory, else the path as given when it is a file, else next to
// the executable, else in the source tree
// (the generated libraries, resources/animals and resources/vegetation, are not copied to every build folder).
// An absolute path is returned as it is
std::filesystem::path libraryResourceFile(const std::filesystem::path& relative);

}

#endif
