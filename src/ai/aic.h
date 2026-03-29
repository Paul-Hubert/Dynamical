#ifndef AIC_H
#define AIC_H

#include <ai/action_id.h>
#include <ai/actions/action.h>

namespace dy {

class AIC {
public:
    std::unique_ptr<Action> action;
    float score = 0;
    ActionParams current_params;  // For future use in Phase 2+
};

}

#endif
