#ifndef HARVESTED_H
#define HARVESTED_H

#include "time.h"

namespace dy {

class Harvested {
public:
    Harvested(time_t end) : end(end) {}
    time_t end;
};

}

#endif
