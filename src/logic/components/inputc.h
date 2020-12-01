#ifndef INPUTC_H
#define INPUTC_H

#include <bitset>
#include "glm/glm.hpp"
#include "sdl2/SDL.h"

enum Action : char {
    FORWARD,
    BACKWARD,
    LEFT,
    RIGHT,
    UP,
    DOWN,
    SPRINT,
    PRIMARY,
    SECONDARY,
    TERTIARY,
    RESIZE,
    MENU,
    DEBUG,
    EXIT,
    MOUSE,
    LAG,
    END_ENUM
};

class InputC {
public:
    std::bitset<Action::END_ENUM> on;
    glm::ivec2 mousePos;
    glm::ivec2 mouseWheel;
    bool mouseLeft = false;
    bool mouseRight = false;
    bool mouseMiddle = false;
	bool focused = false;
	bool window_showing = true;
};

#endif
