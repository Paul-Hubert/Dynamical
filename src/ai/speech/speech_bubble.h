#pragma once
#include <string>

namespace dy {

struct SpeechBubble {
    std::string thought;
    std::string dialogue;
    float lifetime = 10.0f;
    float elapsed  = 0.0f;
};

}
