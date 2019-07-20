#ifndef SWAPCHAIN_H
#define SWAPCHAIN_H

#include <vulkan/vulkan.hpp>

#include "num_frames.h"

class Windu;
class Instance;
class Device;

class Swapchain {
public :
    Swapchain(Windu& win, Instance& instance, Device &device);
    void setup();
    void cleanup();
    ~Swapchain();
    
    uint32_t acquire(vk::Semaphore signal);
    void present(vk::Semaphore wait);
    
    Windu& win;
    Instance& instance;
    Device& device;
    
    vk::SwapchainKHR swapchain;
    std::array<vk::Image, NUM_FRAMES> images;
    std::array<vk::ImageView, NUM_FRAMES> imageViews;
    vk::Format format;
    vk::Extent2D extent;
    vk::ColorSpaceKHR colorSpace;
    vk::PresentModeKHR presentMode;
    
    vk::SurfaceCapabilitiesKHR capabilities;
    std::vector<vk::SurfaceFormatKHR> formats;
    std::vector<vk::PresentModeKHR> presentModes;
    
    vk::Format findSupportedFormat(const std::vector<vk::Format>& candidates, vk::ImageTiling tiling, vk::FormatFeatureFlags features);

    uint32_t current = 1000;
    
    PFN_vkAcquireNextImageKHR vkAcquireNextImageKHR;
    PFN_vkQueuePresentKHR vkQueuePresentKHR;
    
private :
    vk::SurfaceFormatKHR chooseSwapSurfaceFormat(std::vector<vk::SurfaceFormatKHR> &formats, vk::Format wantedFormat, vk::ColorSpaceKHR wantedColorSpace);
    vk::PresentModeKHR chooseSwapPresentMode(std::vector<vk::PresentModeKHR> &presentModes, vk::PresentModeKHR wantedMode);
    vk::Extent2D chooseSwapExtent(vk::SurfaceCapabilitiesKHR &capabilities);
    
    uint64_t last = 0;
    double frametime = 0.0, count = 0.0;
};

#endif

