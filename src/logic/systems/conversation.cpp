#include "conversation.h"

#include "ai/conversation/conversation_manager.h"
#include "extra/optick/optick.h"

using namespace dy;

void ConversationSys::tick(double dt) {
    OPTICK_EVENT();

    auto& conversation_manager = reg.ctx<ConversationManager>();
    conversation_manager.tick(reg, static_cast<float>(dt));
}
