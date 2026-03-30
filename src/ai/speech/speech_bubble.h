#pragma once
#include <string>

namespace dy {

struct SpeechBubble {
    std::string thought;
    std::string dialogue;
    float lifetime = 4.0f;
    float elapsed  = 0.0f;
};

}
