#include "system_list.h"

#include "logic/components/input.h"
#include "logic/components/position.h"

#include "util/log.h"
#include "util/util.h"

#include "logic/map/map_manager.h"

#include <imgui/imgui.h>

using namespace dy;

void MapEditorSys::preinit() {}

void MapEditorSys::init() {}

void MapEditorSys::finish() {}

void MapEditorSys::tick(float dt) {

    OPTICK_EVENT();
    static bool open = false;

    static float radius = 50;
    static float power = 0.01;
    static bool remove = false;

    static float sq_radius = radius*radius;
    if(ImGui::Begin("Brush configurator", &open)) {
        ImGui::SetWindowCollapsed(true, ImGuiCond_FirstUseEver);
        ImGui::PushItemWidth(100);
        if(ImGui::InputFloat("Radius", &radius, 1, 5)) {
            sq_radius = radius*radius;
        }
        ImGui::SameLine();
        ImGui::InputFloat("Power", &power, 0.001, 0.1);

        ImGui::Checkbox("Remove", &remove);
        ImGui::PopItemWidth();
    }
    ImGui::End();

    auto& input = reg.ctx<Input>();

    if(input.rightDown) {
        auto& map = reg.ctx<MapManager>();

        glm::vec2 pos = map.getMousePosition();

        for(int i = pos.x-radius; i < pos.x+radius; ++i) {
            for(int j = pos.y-radius; j < pos.y+radius; ++j) {
                float sq_distance = sq(pos.x-i) + sq(pos.y-j);
                if(sq_distance < sq_radius) {
                    auto v = glm::vec2(i,j);
                    auto chunk = map.getTileChunk(v);
                    if(!chunk) chunk = map.generateChunk(map.getChunkPos(v));
                    auto& tile = chunk->get(map.getTilePos(v));
                    tile.level += (remove ? -1 : 1) * power * sqrt(sq_radius-sq_distance);
                    map.updateTile(v);
                }
            }
        }


    }
}
