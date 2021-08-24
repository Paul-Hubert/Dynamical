#ifndef RENDERER_H
#define RENDERER_H

#include "entt/entt.hpp"

#include "context/context.h"
#include "input.h"
#include "ui/ui.h"
#include "classic_render/classic_render.h"
#include "ui/ui_render.h"

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
    Context ctx;
    Input input;
    UI ui;
    std::unique_ptr<ClassicRender>  classic_render;
    std::unique_ptr<UIRender> ui_render;

    uint32_t frame_index = 0;
    vk::DescriptorSetLayout view_layout;
};

#endif
