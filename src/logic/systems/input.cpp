#include "input.h"

#include "logic/components/input.h"

#include "renderer/context/context.h"
#include "util/log.h"

#include <iostream>

#include <unordered_map>

using namespace dy;

std::unordered_map<SDL_Scancode, Input::Action> actionMap = {
    {SDL_SCANCODE_W, Input::FORWARD},
    {SDL_SCANCODE_S, Input::BACKWARD},
    {SDL_SCANCODE_A, Input::LEFT},
    {SDL_SCANCODE_D, Input::RIGHT},
    {SDL_SCANCODE_SPACE, Input::PAUSE},
    {SDL_SCANCODE_M, Input::MENU},
    {SDL_SCANCODE_K, Input::DEBUG}
};

InputSys::InputSys(entt::registry& reg) : System(reg) {
    
    reg.set<Input>();
    
}



void InputSys::tick(float dt) {

    OPTICK_EVENT();
    
    Input& input = reg.ctx<Input>();
    
    input.leftClick = input.rightClick = input.middleClick = false;
    input.mouseWheel = glm::ivec2(0,0);
    
    Context& ctx = *reg.ctx<Context*>();
    
    SDL_Event e;
    while (SDL_PollEvent(&e)) {

        if(e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_RESIZED) {

            input.on.set(Input::RESIZE);
            
        } else if(e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {

            input.focused = true;

        } else if(e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {

            input.focused = false;

        } else if(e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_RESTORED) {

            input.window_showing = true;

        } else if(e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_MINIMIZED) {

            input.window_showing = false;

        } else if(e.type == SDL_QUIT || (e.type == SDL_KEYDOWN && e.key.keysym.scancode == SDL_SCANCODE_ESCAPE)) {
            
            input.on.set(Input::EXIT);
            
        } else if(e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
            
            try {
                input.on.set(actionMap.at(e.key.keysym.scancode), e.type == SDL_KEYDOWN);
            } catch(std::out_of_range&) {}
            
        } else if(e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) {
            
            switch(e.button.button) {
                case SDL_BUTTON_LEFT:
                    input.mouseLeft = input.leftDown = e.type == SDL_MOUSEBUTTONDOWN;
                    if(e.type == SDL_MOUSEBUTTONDOWN) input.leftClick = true;
                    break;
                case SDL_BUTTON_RIGHT:
                    input.mouseRight = input.rightDown = e.type == SDL_MOUSEBUTTONDOWN;
                    if(e.type == SDL_MOUSEBUTTONDOWN) input.rightClick = true;
                    break;
                case SDL_BUTTON_MIDDLE:
                    input.mouseMiddle = input.middleDown = e.type == SDL_MOUSEBUTTONDOWN;
                    if(e.type == SDL_MOUSEBUTTONDOWN) input.middleClick = true;
                    break;
            }
            
        } else if(e.type == SDL_MOUSEWHEEL) {
            
            input.mouseWheel.x = e.wheel.x;
            input.mouseWheel.y = e.wheel.y;
            /*
            if (e.wheel.x > 0) input.mouseWheel.x += 1;
            else if (e.wheel.x < 0) input.mouseWheel.x -= 1;
            if (e.wheel.y > 0) input.mouseWheel.y += 1;
            else if (e.wheel.y < 0) input.mouseWheel.y -= 1;
            */
        }

        if(e.type == SDL_TEXTINPUT) {
            std::array<char, SDL_TEXTINPUTEVENT_TEXT_SIZE> text;
            std::copy(std::begin(e.text.text), std::end(e.text.text), std::begin(text));
            input.text = std::make_optional(text);
        } else {
            input.text = std::nullopt;
        }
        
    }
    
    int w, h;
    SDL_GetWindowSize(ctx.win, &w, &h);
    input.screenSize = glm::ivec2(w, h);
    
    if (input.focused) {

        int x, y;
        SDL_GetMouseState(&x, &y);
        
        input.mousePos = glm::ivec2(x, y);

    }
    
}

InputSys::~InputSys() {
    
}
