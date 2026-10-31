#ifndef DY_LOG_H
#define DY_LOG_H

#include <vector>
#include <mutex>
#include <iostream>
#include <string>

namespace dy {

enum Level { trace, debug, info, warning, error, critical };

std::ostream& log(Level l = Level::trace);

// Copies everything logged to a file as well (dynamical_train's -logFile, grepped live by the trainer's health
// check), flushed at every line. stdout and stderr keep their output. Returns false if the file cannot be opened
bool setLogFile(const std::string& path);

}

#endif
