#ifndef DY_LOG_H
#define DY_LOG_H

#include <vector>
#include <mutex>
#include <iostream>

namespace dy {

enum Level { trace, debug, info, warning, error, critical };

std::ostream& log(Level l = Level::trace);

void set_log_level(Level l);
Level get_log_level();

}

#endif
