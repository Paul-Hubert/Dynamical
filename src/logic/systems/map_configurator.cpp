#include "system_list.h"
#include "logic/settings/settings.h"
#include "logic/components/input.h"
#include "util/util.h"

#include <imgui/imgui.h>
#include <implot/implot.h>
#include <sstream>

using namespace dy;

void MapConfiguratorSys::preinit() {

}

void MapConfiguratorSys::init() {
    ImPlot::CreateContext();
    ImPlot::SetImGuiContext(ImGui::GetCurrentContext());
}

void MapConfiguratorSys::finish() {

}

struct GuiStatic {
    int octave;
    int prev_current;
    int current;
    double current_x;
    double current_y;
};

void MapConfiguratorSys::tick(float dt) {
    auto& settings = reg.ctx<Settings>();
    auto& configurations = settings.map_configurations;
    auto& input = reg.ctx<Input>();

    static bool open = false;

    if(ImGui::Begin("Map Configurator", &open)) {

        if(ImGui::Button("Add new parametric perlin")) {
            configurations.emplace_back();
        }

        ImGui::SameLine();

        if(ImGui::Button("Save settings")) {
            settings.save();
        }

        //Statics state for imgui, for each tab
        static std::vector<GuiStatic> statics;
        //There are new configurations; we need to create new static data for imgui
        if(statics.size() < configurations.size()) {
            for(int i = statics.size(); i < configurations.size(); ++i) {
                auto& conf = configurations.at(i);
                GuiStatic s;
                s.octave = conf.octave_count;
                s.prev_current = 0;
                s.current = 0;
                s.current_x = conf.points_x.at(0);
                s.current_y = conf.points_y.at(0);
                statics.push_back(s);
            }
        }

        if(ImGui::BeginTabBar("Perlin profiles")) {
            for(int i = 0; i < configurations.size(); ++i) {

                std::ostringstream buf;
                buf << "Perlin " << i;

                if(ImGui::BeginTabItem(buf.str().data())) {
                    auto& conf = configurations.at(i);
                    auto& s = statics.at(i);

                    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255,0,0,255));

                    if(ImGui::Button("Remove perlin")) {
                        configurations.erase(configurations.begin()+i);
                        statics.erase(statics.begin()+i);
                        --i;
                        ImGui::PopStyleColor();
                        continue;
                    }
                    ImGui::PopStyleColor();

                    ImGui::PushItemWidth(100);
                    ImGui::InputInt("Octave count", &s.octave);
                    if(s.octave < 1) {
                        s.octave = 1;
                    }
                    ImGui::SameLine();
                    conf.octave_count = s.octave;

                    ImGui::InputFloat("Frequency", &conf.frequency, 0.01, 0.1);
                    ImGui::SameLine();
                    //seed is an unsigned int, imgui takes a signed integer but they're the same size
                    // and the value doesn't matter so it's okay
                    ImGui::InputInt("Seed", (int*)&conf.seed);
                    ImGui::SameLine();
                    ImGui::InputFloat("Gain", &conf.gain, 0.1);

                    ImGui::InputFloat("Lacunarity", &conf.lacunarity, 0.1);
                    ImGui::SameLine();

                    ImGui::InputFloat("Amplitude", &conf.amplitude, 0.1);
                    ImGui::SameLine();
                    ImGui::InputFloat("Weighted strength", &conf.weighted_strength, 0.1);
                    ImGui::SameLine();

                    if(ImGui::Button("Add point")) {
                        const float INITIAL_X = 0;
                        const float INITIAL_Y = 10;

                        for(auto i = 0; i <= conf.points_x.size(); ++i) {
                            auto x = conf.points_x.at(i);
                            if(x > INITIAL_X) {
                                conf.points_x.insert(conf.points_x.begin()+i, INITIAL_X);
                                conf.points_y.insert(conf.points_y.begin()+i, INITIAL_Y);
                                s.current = i;
                                break;
                            }
                        }
                    }
                    ImGui::SameLine();

                    auto disabled = conf.points_x.size() <= 2;
                    if(disabled) {
                        ImGui::BeginDisabled(true);
                    }

                    if(ImGui::Button("Delete point")) {
                        conf.points_x.erase(conf.points_x.begin()+s.current);
                        conf.points_y.erase(conf.points_y.begin()+s.current);
                        --s.current;
                    }


                    if(disabled) {
                        ImGui::EndDisabled();
                    }
                    ImGui::SameLine();
                    ImGui::InputDouble("Current X", &s.current_x, 0.01, 0.1);
                    ImGui::SameLine();
                    ImGui::InputDouble("Current Y", &s.current_y, 0.5, 5);

                    ImGui::Separator();
                    ImGui::PopItemWidth();
                    if(ImPlot::BeginPlot("Terrain height configurator")) {
                        ImPlot::SetupAxes("Perlin variation", "Height");
                        ImPlot::SetupAxisLimits(ImAxis_X1, -1.05, 1.05);
                        ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, -1.05, 1.05);

                        ImPlot::SetupAxisLimits(ImAxis_Y1, -5, 20);
                        ImPlot::SetupFinish();

                        ImPlot::DragPoint(0, &s.current_x, &s.current_y, ImVec4(1,0.6,0.2,1), 7);
                        ImPlot::PlotLine("Curve", conf.points_x.data(), conf.points_y.data(), conf.points_x.size());

                        if(input.mouseLeft) {

                            const double MAX_DISTANCE = 7*7; //magic number, do not change or it will segfault

                            double min_distance = -1;
                            int min_index = -1;
                            for(int i = 0; i < conf.points_x.size(); ++i) {
                                auto p = ImPlot::PlotToPixels(conf.points_x.at(i), conf.points_y.at(i));
                                double dist = sq(p.x - input.mousePos.x) + sq(p.y - input.mousePos.y);

                                if(dist < MAX_DISTANCE && (min_distance == -1 || dist < min_distance)) {
                                    min_distance = dist;
                                    min_index = i;
                                }
                            }

                            if(min_index != -1) {
                                s.current = min_index;
                            }
                        }

                        ImPlot::EndPlot();
                    }

                    if(s.current_x < -1 || s.current == 0) {
                        s.current_x = -1;
                    } else if(s.current_x > 1 || s.current == conf.points_x.size()-1) {
                        s.current_x = 1;
                    }

                    for(auto i = 0; i < conf.points_x.size(); ++i) {
                        auto x = conf.points_x.at(i);
                        auto y = conf.points_y.at(i);
                        std::ostringstream buf;
                        buf << "Point n°" << i << " | " << x << "x" << y;
                        ImGui::RadioButton(buf.str().data(), &s.current, i);
                    }
                    //If the user clicked on another point, we load new data
                    if(s.prev_current != s.current) {
                        s.current_x = conf.points_x.at(s.current);
                        s.current_y = conf.points_y.at(s.current);
                        s.prev_current = s.current;
                    } else {
                        //If not, we update the values, but not before the magic sort^tm
                        if(conf.points_x.at(s.current) < s.current_x) {
                            //the new value is bigger, so we will swap it forward until we find someone bigger than us
                            for(int i = s.current+1; i < conf.points_x.size(); ++i) {
                                float x = conf.points_x.at(i);
                                if(x < s.current_x) { //next one is too small; let's swap it
                                    std::iter_swap(conf.points_x.begin()+i-1, conf.points_x.begin()+i);
                                    std::iter_swap(conf.points_y.begin()+i-1, conf.points_y.begin()+i);
                                } else { //we found someone bigger than us; we stop here
                                    s.current = i-1;
                                    s.prev_current = s.current;
                                    break;
                                }
                            }
                        } else {
                            for(int i = s.current-1; i >= 0; --i) {
                                float x = conf.points_x.at(i);
                                if(x > s.current_x) {
                                    std::iter_swap(conf.points_x.begin()+i+1, conf.points_x.begin()+i);
                                    std::iter_swap(conf.points_y.begin()+i+1, conf.points_y.begin()+i);
                                } else {
                                    s.current = i+1;
                                    s.prev_current = s.current;
                                    break;
                                }
                            }
                        }
                        //current has been updated with new value, let's actually update it
                        conf.points_x.at(s.current) = s.current_x;
                        conf.points_y.at(s.current) = s.current_y;
                    }
                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}
