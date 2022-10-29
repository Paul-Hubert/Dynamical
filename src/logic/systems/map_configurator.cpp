#include "system_list.h"
#include "logic/settings/settings.h"
#include "logic/components/input.h"

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

void MapConfiguratorSys::tick(float dt) {
    auto& settings = reg.ctx<Settings>();
    auto& conf = settings.map_configuration;
    auto& input = reg.ctx<Input>();

    static bool open = false;

    if(ImGui::Begin("Map Configurator", &open)) {
        static int octave = conf.octave_count;
        ImGui::InputInt("Octave count", &octave);
        if(octave < 1) {
            octave = 1;
        }
        conf.octave_count = octave;

        ImGui::InputFloat("Frequency", &conf.frequency);

        //seed is an unsigned int, imgui takes a signed integer but they're the same size
        // and the value doesn't matter so it's okay
        ImGui::InputInt("Seed", (int*)&conf.seed);
        ImGui::InputFloat("Gain", &conf.gain, 0.1);
        ImGui::InputFloat("Lacunarity", &conf.lacunarity, 0.1);
        ImGui::InputFloat("Amplitude", &conf.amplitude, 0.1);
        ImGui::InputFloat("Weighted strength", &conf.weighted_strength, 0.1);

        if(ImGui::Button("Save settings")) {
            settings.save();
        }

        static int prev_current = 0;
        static int current = 0;

        if(ImGui::Button("Add point")) {
            const float INITIAL_X = 0;
            const float INITIAL_Y = 10;

            for(auto i = 0; i <= conf.points_x.size(); ++i) {
                auto x = conf.points_x.at(i);
                if(x > INITIAL_X) {
                    conf.points_x.insert(conf.points_x.begin()+i, INITIAL_X);
                    conf.points_y.insert(conf.points_y.begin()+i, INITIAL_Y);
                    current = i;
                    break;
                }
            }
        }

        auto disabled = conf.points_x.size() <= 2;
        if(disabled) {
            ImGui::BeginDisabled(true);
        }

        if(ImGui::Button("Delete point")) {
            conf.points_x.erase(conf.points_x.begin()+current);
            conf.points_y.erase(conf.points_y.begin()+current);
            --current;
        }

        if(disabled) {
            ImGui::EndDisabled();
        }

        static double current_x = conf.points_x.at(current);
        ImGui::InputDouble("Current X", &current_x, 0.01, 0.1);

        static double current_y = conf.points_y.at(current);
        ImGui::InputDouble("Current Y", &current_y, 0.5, 5);

        ImGui::Separator();
        if(ImPlot::BeginPlot("Terrain height configurator")) {
            ImPlot::SetupAxes("Perlin variation", "Height");
            ImPlot::SetupAxisLimits(ImAxis_X1, -1.05, 1.05);
            ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, -1.05, 1.05);

            ImPlot::SetupAxisLimits(ImAxis_Y1, -5, 20);
            ImPlot::SetupFinish();

            ImPlot::DragPoint(0, &current_x, &current_y, ImVec4(1,0.6,0.2,1), 7);
            ImPlot::PlotLine("Curve", conf.points_x.data(), conf.points_y.data(), conf.points_x.size());

            if(input.mouseLeft) {
                auto pos = ImPlot::GetPlotMousePos();

                const double MAX_DISTANCE_X = 0.1;
                const double MAX_DISTANCE_Y = 2;

                for(int i = 0; i < conf.points_x.size(); ++i) {
                    if(abs(pos.x - conf.points_x.at(i)) < MAX_DISTANCE_X && abs(pos.y - conf.points_y.at(i)) < MAX_DISTANCE_Y) {
                        current = i;
                    }
                }
            }

            ImPlot::EndPlot();
        }

        if(current_x < -1 || current == 0) {
            current_x = -1;
        } else if(current_x > 1 || current == conf.points_x.size()-1) {
            current_x = 1;
        }

        for(auto i = 0; i < conf.points_x.size(); ++i) {
            auto x = conf.points_x.at(i);
            auto y = conf.points_y.at(i);
            std::ostringstream buf;
            buf << "Point n°" << i << " | " << x << "x" << y;
            ImGui::RadioButton(buf.str().data(), &current, i);
        }
        //If the user clicked on another point, we load new data
        if(prev_current != current) {
            current_x = conf.points_x.at(current);
            current_y = conf.points_y.at(current);
            prev_current = current;
        } else {
            //If not, we update the values, but not before the magic sort^tm
            if(conf.points_x.at(current) < current_x) {
                //the new value is bigger, so we will swap it forward until we find someone bigger than us
                for(int i = current+1; i < conf.points_x.size(); ++i) {
                    float x = conf.points_x.at(i);
                    if(x < current_x) { //next one is too small; let's swap it
                        std::iter_swap(conf.points_x.begin()+i-1, conf.points_x.begin()+i);
                        std::iter_swap(conf.points_y.begin()+i-1, conf.points_y.begin()+i);
                    } else { //we found someone bigger than us; we stop here
                        current = i-1;
                        prev_current = current;
                        break;
                    }
                }
            } else {
                for(int i = current-1; i >= 0; --i) {
                    float x = conf.points_x.at(i);
                    if(x > current_x) {
                        std::iter_swap(conf.points_x.begin()+i+1, conf.points_x.begin()+i);
                        std::iter_swap(conf.points_y.begin()+i+1, conf.points_y.begin()+i);
                    } else {
                        current = i+1;
                        prev_current = current;
                        break;
                    }
                }
            }
            //current has been updated with new value, let's actually update it
            conf.points_x.at(current) = current_x;
            conf.points_y.at(current) = current_y;
        }
    }
    ImGui::End();
}
