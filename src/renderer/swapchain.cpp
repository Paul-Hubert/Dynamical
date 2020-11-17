#include "swapchain.h"

#include "context.h"
#include "loader.inl"
#include "num_frames.h"

#include <SDL_vulkan.h>
#include <iostream>
#include <time.h>

Swapchain::Swapchain(Context& ctx) : ctx(ctx) {
    
    INST_LOAD(vkGetPhysicalDeviceSurfaceCapabilitiesKHR)
    INST_LOAD(vkGetPhysicalDeviceSurfaceFormatsKHR)
    INST_LOAD(vkGetPhysicalDeviceSurfacePresentModesKHR)
    
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(static_cast<VkPhysicalDevice> (ctx.device), ctx.win.surface, reinterpret_cast<VkSurfaceCapabilitiesKHR*> (&capabilities));
    
    uint32_t num;
    vkGetPhysicalDeviceSurfaceFormatsKHR(static_cast<VkPhysicalDevice> (ctx.device), ctx.win.surface, &num, nullptr);
    formats.resize(num);
    vkGetPhysicalDeviceSurfaceFormatsKHR(static_cast<VkPhysicalDevice> (ctx.device), ctx.win.surface, &num, reinterpret_cast<VkSurfaceFormatKHR*> (formats.data()));
    
    vkGetPhysicalDeviceSurfacePresentModesKHR(static_cast<VkPhysicalDevice> (ctx.device), ctx.win.surface, &num, nullptr);
    presentModes.resize(num);
    vkGetPhysicalDeviceSurfacePresentModesKHR(static_cast<VkPhysicalDevice> (ctx.device), ctx.win.surface, &num, reinterpret_cast<VkPresentModeKHR*> (presentModes.data()));
    
    vk::SurfaceFormatKHR surfaceformat = chooseSwapSurfaceFormat(formats, vk::Format::eB8G8R8A8Unorm, vk::ColorSpaceKHR::eSrgbNonlinear);
    presentMode = chooseSwapPresentMode(presentModes, vk::PresentModeKHR::eFifo);
    extent = chooseSwapExtent(capabilities);
    format = surfaceformat.format;
    colorSpace = surfaceformat.colorSpace;
    
    if(num_frames < capabilities.minImageCount || (capabilities.maxImageCount != 0 && num_frames > capabilities.maxImageCount)) {
        num_frames = std::max(capabilities.minImageCount, std::min(num_frames, capabilities.maxImageCount == 0 ? 10 : capabilities.maxImageCount));
    }
    
    setup();
    
}

void Swapchain::setup() {
    
    vk::SwapchainCreateInfoKHR createInfo({}, ctx.win.surface, num_frames, format, colorSpace, extent, 1, vk::ImageUsageFlagBits::eColorAttachment, vk::SharingMode::eExclusive,
        1, &ctx.device.g_i, capabilities.currentTransform, vk::CompositeAlphaFlagBitsKHR::eOpaque, presentMode, VK_TRUE, swapchain
    );
    
    DEV_LOAD(vkCreateSwapchainKHR)
    
    auto info = static_cast<VkSwapchainCreateInfoKHR> (createInfo);
    
    VkSwapchainKHR newSwapchain;
    vkCreateSwapchainKHR(ctx.device, &info, nullptr, &newSwapchain);
    
    if(swapchain) {
        
        for (auto imageView : imageViews) {
            ctx.device->destroy(imageView);
        }
        DEV_LOAD(vkDestroySwapchainKHR)
        vkDestroySwapchainKHR(ctx.device, static_cast<VkSwapchainKHR> (swapchain), nullptr);
        
    }
    
    swapchain = newSwapchain;
    
    DEV_LOAD(vkGetSwapchainImagesKHR)
    
    uint32_t num;
    vkGetSwapchainImagesKHR(ctx.device, static_cast<VkSwapchainKHR> (swapchain), &num, nullptr);
    
    if(num_frames != num) std::cout << "number of frames changed to : " << num << std::endl;
    
    images.resize(num_frames);
    vkGetSwapchainImagesKHR(ctx.device, static_cast<VkSwapchainKHR> (swapchain), &num, reinterpret_cast<VkImage*> (images.data()));
    
    imageViews.resize(num_frames);
    for(uint32_t i = 0; i < imageViews.size(); i++) {
        
        imageViews[i] = ctx.device->createImageView(vk::ImageViewCreateInfo({}, images[i], vk::ImageViewType::e2D, format,
                                    vk::ComponentMapping(), vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1)
        ));
        
    }
    
    DEV_LOAD(vkAcquireNextImageKHR)
    this->vkAcquireNextImageKHR = vkAcquireNextImageKHR;
    DEV_LOAD(vkQueuePresentKHR)
    this->vkQueuePresentKHR = vkQueuePresentKHR;
    
}

void Swapchain::cleanup() {
    
    for (auto imageView : imageViews) {
        ctx.device->destroy(imageView);
    }
    
    DEV_LOAD(vkDestroySwapchainKHR)
    vkDestroySwapchainKHR(ctx.device, swapchain, nullptr);
    swapchain = nullptr;
    
}

Swapchain::~Swapchain() {
    cleanup();
}




/////////////
// RUNTIME //
/////////////

uint32_t Swapchain::acquire(vk::Semaphore signal) {
    
    auto resultvalue = ctx.device->acquireNextImageKHR(swapchain, std::numeric_limits<uint64_t>::max(), signal, nullptr, *this);
    current = resultvalue.value;
    
    if(resultvalue.result == vk::Result::eSuboptimalKHR) {
        throw vk::OutOfDateKHRError("Suboptimal swapchain");
    }

    return current;
    
}

void Swapchain::present(vk::Semaphore wait) {
    
    // This will display the image
    auto result = ctx.device.graphics.presentKHR(vk::PresentInfoKHR((wait ? 1 : 0), &wait, 1, &swapchain, &current), *this);
    
    if(result == vk::Result::eSuboptimalKHR) {
        throw vk::OutOfDateKHRError("Suboptimal swapchain");
    }
    
}






vk::SurfaceFormatKHR Swapchain::chooseSwapSurfaceFormat(std::vector<vk::SurfaceFormatKHR> &formats, vk::Format wantedFormat, vk::ColorSpaceKHR wantedColorSpace) {
    if (formats.size() == 1 && formats[0].format == vk::Format::eUndefined) {
        vk::SurfaceFormatKHR sf;
        sf.format = wantedFormat;
        sf.colorSpace = wantedColorSpace;
        return sf; // Just give the format you want
    }

    for (const auto& availableFormat : formats) {
        if (availableFormat.format == wantedFormat && availableFormat.colorSpace == wantedColorSpace) { // Look for the wanted format
            return availableFormat;
        }
    }

    return formats[0];
}

vk::PresentModeKHR Swapchain::chooseSwapPresentMode(std::vector<vk::PresentModeKHR> &presentModes, vk::PresentModeKHR wantedMode) {

    for (const auto& availablePresentMode : presentModes) {
        if (availablePresentMode == wantedMode) {
            return availablePresentMode;
        }
    }

    return vk::PresentModeKHR::eFifo;
}

vk::Extent2D Swapchain::chooseSwapExtent(vk::SurfaceCapabilitiesKHR &capabilities) {

    vk::Extent2D actualExtent((uint32_t) ctx.win.getWidth(), (uint32_t) ctx.win.getHeight());

    actualExtent.width = std::max(capabilities.minImageExtent.width, std::min(capabilities.maxImageExtent.width, actualExtent.width));
    actualExtent.height = std::max(capabilities.minImageExtent.height, std::min(capabilities.maxImageExtent.height, actualExtent.height));

    return actualExtent;

}

vk::Format Swapchain::findSupportedFormat(const std::vector<vk::Format>& candidates, vk::ImageTiling tiling, vk::FormatFeatureFlags features) {
    for (vk::Format format : candidates) {
        vk::FormatProperties props = ctx.device.physical.getFormatProperties(format);

        if (tiling == vk::ImageTiling::eLinear && (props.linearTilingFeatures & features) == features) {
            return format;
        } else if (tiling == vk::ImageTiling::eOptimal && (props.optimalTilingFeatures & features) == features) {
            return format;
        }
    }

    throw std::runtime_error("failed to find supported format!");
}
