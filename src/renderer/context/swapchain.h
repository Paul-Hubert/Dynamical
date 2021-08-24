#ifndef SWAPCHAIN_H
#define SWAPCHAIN_H

#include "renderer/util/vk.h"

#include "num_frames.h"

#include <entt/entt.hpp>

class Context;

class Swapchain {
public :
    Swapchain(Context& ctx, entt::registry& reg);
    void setup();
    void cleanup();
    ~Swapchain();
    
    uint32_t acquire(vk::Semaphore signal);
    void present(vk::Semaphore wait);
    
    
    vk::SwapchainKHR swapchain;
    std::vector<vk::Image> images;
    std::vector<vk::ImageView> imageViews;
    vk::Format format;
    vk::Extent2D extent;
    vk::ColorSpaceKHR colorSpace;
    vk::PresentModeKHR presentMode;
    
    vk::SurfaceCapabilitiesKHR capabilities;
    std::vector<vk::SurfaceFormatKHR> formats;
    std::vector<vk::PresentModeKHR> presentModes;
    
    uint32_t num_frames = NUM_FRAMES;
    
    vk::Format findSupportedFormat(const std::vector<vk::Format>& candidates, vk::ImageTiling tiling, vk::FormatFeatureFlags features);

    uint32_t current = 1000;
    
    
private :
    Context& ctx;
    entt::registry& reg;

    vk::SurfaceFormatKHR chooseSwapSurfaceFormat(std::vector<vk::SurfaceFormatKHR> &formats, vk::Format wantedFormat, vk::ColorSpaceKHR wantedColorSpace);
    vk::PresentModeKHR chooseSwapPresentMode(std::vector<vk::PresentModeKHR> &presentModes, vk::PresentModeKHR wantedMode);
    vk::Extent2D chooseSwapExtent(vk::SurfaceCapabilitiesKHR &capabilities);
    
    uint64_t last = 0;
    double frametime = 0.0, count = 0.0;
};

#endif

