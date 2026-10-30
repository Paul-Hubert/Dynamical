#include "xr.h"

#include "util/log.h"

#include <cstring>
#include <sstream>

using namespace dy;

bool dy::xrCheckResult(XrResult result, const char* what) {
    if(XR_FAILED(result)) {
        dy::log(dy::error) << "XR Error : " << result;
        if(what) dy::log(dy::error) << " (" << what << ")";
        dy::log(dy::error) << "\n";
        return false;
    }
    return true;
}

std::vector<const char*> dy::checkXrLayers(std::vector<const char*> layers, const std::vector<XrApiLayerProperties>& available) {

    std::vector<const char*> out;

    for(const char* name : layers) {
        bool found = false;
        for(const auto& props : available) {
            if(strcmp(name, props.layerName) == 0) {
                out.push_back(name);
                found = true;
                break;
            }
        }
        if(!found) {
            dy::log(dy::warning) << "OpenXR Layer requested, but not available : " << name << "\n";
        }
    }

    return out;

}

std::vector<const char*> dy::checkXrExtensions(std::vector<const char*> extensions, const std::vector<XrExtensionProperties>& available) {

    std::vector<const char*> out;

    for(const char* name : extensions) {
        bool found = false;
        for(const auto& props : available) {
            if(strcmp(name, props.extensionName) == 0) {
                out.push_back(name);
                found = true;
                break;
            }
        }
        if(!found) {
            dy::log(dy::warning) << "OpenXR Extension requested, but not available : " << name << "\n";
        }
    }

    return out;

}

std::vector<std::string> dy::splitExtensionString(const std::string& list) {
    std::vector<std::string> out;
    std::stringstream ss(list);
    std::string name;
    while(ss >> name) {
        out.push_back(name);
    }
    return out;
}
