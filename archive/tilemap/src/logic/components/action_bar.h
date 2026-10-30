#ifndef ACTION_BAR_H
#define ACTION_BAR_H

#include "time.h"

namespace dy {

class ActionBar {
public:
    ActionBar(time_t start, time_t end) : start(start), end(end) {}
    time_t start;
    time_t end;
};

}

#endif
