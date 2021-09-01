#ifndef AIC_H
#define AIC_H

#include "actions/action.h"

namespace dy {

class AIC {
public:
    std::unique_ptr<Action> action;
    float score = 0;
};

}

#endif
