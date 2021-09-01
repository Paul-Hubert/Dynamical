#ifndef INPUTC_H
#define INPUTC_H

#include <bitset>
#include "glm/glm.hpp"
#include <SDL.h>

class InputC {
public:
    
    enum Action : char {
        FORWARD,
        BACKWARD,
        LEFT,
        RIGHT,
        PAUSE,
        RESIZE,
        MENU,
        DEBUG,
        EXIT,
        END_ENUM
    };
    
    std::bitset<Action::END_ENUM> on;
    glm::ivec2 mousePos;
    glm::ivec2 mouseWheel;
    glm::ivec2 screenSize;
    
    bool mouseLeft = false;
    bool mouseRight = false;
    bool mouseMiddle = false;
    
    bool leftDown = false;
    bool rightDown = false;
    bool middleDown = false;
    
    bool leftClick = false;
    bool rightClick = false;
    bool middleClick = false;
    
	bool focused = false;
	bool window_showing = true;
};

#endif
