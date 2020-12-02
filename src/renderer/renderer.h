#ifndef RENDERER_H
#define RENDERER_H

#include "entt/entt.hpp"

class Context;
class Input;
class VRInput;
class UI;
class VRRender;

class Renderer {
public:
    Renderer(entt::registry& reg);
    void preinit();
    void init();
    void prepare();
    void render();
    void finish();
    ~Renderer();
    
private:
    entt::registry& reg;
    std::unique_ptr<Context> ctx;
    std::unique_ptr<Input> input;
    std::unique_ptr<VRInput> vr_input;
    std::unique_ptr<UI> ui;
    std::unique_ptr<VRRender> vr_render;
};

#endif
