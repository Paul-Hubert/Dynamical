#ifndef WINDU_H
#define WINDU_H

#include <SDL2/SDL.h>

class Windu {
public:
    Windu();
    void init();
    ~Windu();
private:
    SDL_Window* window;
};
    
#endif
