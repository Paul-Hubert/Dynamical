#ifndef INPUTC_H
#define INPUTC_H

#include <bitset>
#include "glm/glm.hpp"
#include "SDL.h"

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
    END_ENUM
};

class InputC {
public:
    std::bitset<Action::END_ENUM> on;
    bool mouseFree = false;
    glm::ivec2 mousePos;
    glm::ivec2 mouseDiff;
    glm::ivec2 mouseWheel;
    bool mouseLeft = false;
    bool mouseRight = false;
    bool mouseMiddle = false;
	bool focused = false;
	bool window_showing = true;
	bool vr_showing = false;
};

#endif
