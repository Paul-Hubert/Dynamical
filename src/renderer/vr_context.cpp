#include "vr_context.h"

#include <vector>
#include "util/util.h"
#include <cstring>
#include <debugapi.h>
#include "context.h"

#include "vk_util.h"

const XrPosef  pose_identity = { {0,0,0,1}, {0,0,0} };

VRContext::VRContext(Context &ctx) : ctx(ctx) {

    // Extensions and layers

    std::vector<const char*> extensions;
    std::vector<const char*> layers;

    extensions.push_back(XR_KHR_VULKAN_ENABLE_EXTENSION_NAME);
    //extensions.push_back("XR_KHR_convert_timespec_time");

#ifndef NDEBUG
    extensions.push_back(XR_EXT_DEBUG_UTILS_EXTENSION_NAME);
    //layers.push_back("XR_APILAYER_LUNARG_core_validation");
#endif

    uint32_t ext_count = 0;
    xrCheckResult(xrEnumerateInstanceExtensionProperties(nullptr, 0, &ext_count, nullptr));
    std::vector<XrExtensionProperties> xr_exts(ext_count, { XR_TYPE_EXTENSION_PROPERTIES });
    xrCheckResult(xrEnumerateInstanceExtensionProperties(nullptr, ext_count, &ext_count, xr_exts.data()));

    extensions = checkExtensions(extensions, xr_exts);

    uint32_t layer_count = 0;
    xrCheckResult(xrEnumerateApiLayerProperties(0, &layer_count, nullptr));
    std::vector<XrApiLayerProperties> xr_layers(layer_count, {XR_TYPE_API_LAYER_PROPERTIES});
    xrCheckResult(xrEnumerateApiLayerProperties(layer_count, &layer_count, xr_layers.data()));

    layers = checkLayers(layers, xr_layers);

    

    // Instance

    XrInstanceCreateInfo createInfo = { XR_TYPE_INSTANCE_CREATE_INFO };
    createInfo.enabledExtensionCount = (uint32_t) extensions.size();
    createInfo.enabledExtensionNames = extensions.data();
    createInfo.enabledApiLayerCount = (uint32_t) layers.size();
    createInfo.enabledApiLayerNames = layers.data();
    createInfo.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
    strcpy_s(createInfo.applicationInfo.applicationName, "Dynamical");
    
    xrCheckResult(xrCreateInstance(&createInfo, &instance));


    if (instance == nullptr)
        throw std::runtime_error("OpenXR instance could not be created\n");


    // Debug messenger

#ifndef NDEBUG
    XrDebugUtilsMessengerCreateInfoEXT debug_info = { XR_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
    debug_info.messageTypes =
            XR_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT     |
            XR_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT  |
            XR_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT |
            XR_DEBUG_UTILS_MESSAGE_TYPE_CONFORMANCE_BIT_EXT;
    debug_info.messageSeverities =
            XR_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
            XR_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT    |
            XR_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            XR_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debug_info.userCallback = &debugCallback;

    PFN_xrCreateDebugUtilsMessengerEXT ext_xrCreateDebugUtilsMessengerEXT = nullptr;
    xrCheckResult(xrGetInstanceProcAddr(instance, "xrCreateDebugUtilsMessengerEXT", (PFN_xrVoidFunction *)(&ext_xrCreateDebugUtilsMessengerEXT)));
    if (ext_xrCreateDebugUtilsMessengerEXT)
        xrCheckResult(ext_xrCreateDebugUtilsMessengerEXT(instance, &debug_info, &debug));
#endif


    // System

    XrSystemGetInfo systemInfo = { XR_TYPE_SYSTEM_GET_INFO };
    systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    xrCheckResult(xrGetSystem(instance, &systemInfo, &system_id));

    blend = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;

}

void VRContext::init() {

    // Session

    XrGraphicsBindingVulkanKHR binding = { XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR };
    binding.instance = ctx.instance;
    binding.physicalDevice = ctx.device.physical;
    binding.device = ctx.device.logical;
    binding.queueFamilyIndex = ctx.device.g_i;
    binding.queueIndex = 0;
    XrSessionCreateInfo sessionInfo = { XR_TYPE_SESSION_CREATE_INFO };
    sessionInfo.next = &binding;
    sessionInfo.systemId = system_id;
    xrCheckResult(xrCreateSession(instance, &sessionInfo, &session));

    if (session == nullptr)
        throw std::runtime_error("OpenXR session could not be created\n");


    // Space

    {
        uint32_t count = 0;
        xrCheckResult(xrEnumerateReferenceSpaces(session, 0, &count, nullptr));
        std::vector<XrReferenceSpaceType> space_types(count);
        xrCheckResult(xrEnumerateReferenceSpaces(session, count, &count, space_types.data()));

        bool found = false;
        for(uint32_t i = 0; i < count; i++) {
            if(space_types[i] == XR_REFERENCE_SPACE_TYPE_STAGE) {
                found = true;
                break;
            }
        }
        if(!found) {
            Util::log(Util::warning) << "Stage reference space is not supported by OpenXR Runtime, switching to Local\n";
        }

        XrReferenceSpaceCreateInfo ref_space = {XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
        ref_space.poseInReferenceSpace = pose_identity;
        ref_space.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
        xrCheckResult(xrCreateReferenceSpace(session, &ref_space, &space));
    }


    // Format

    {
        uint32_t count = 0;
        xrCheckResult(xrEnumerateSwapchainFormats(session, 0, &count, nullptr));
        std::vector<int64_t> formats(count);
        xrCheckResult(xrEnumerateSwapchainFormats(session, count, &count, formats.data()));
        bool found = false;
        for(uint32_t i = 0; i < count; i++) {
            if(swapchain_format == formats[i]) {
                found = true;
                break;
            }
        }
        if(!found) {
            Util::log(Util::warning) << "Required VkFormat is not supported by OpenXR Runtime, switching to : " << formats[0] << "\n";
            swapchain_format = static_cast<VkFormat> (formats[0]);
        }
    }

    // View Configuration

    std::vector<XrViewConfigurationView> views;
    {
        uint32_t count = 0;
        xrCheckResult(xrEnumerateViewConfigurations(instance, system_id, 0, &count, nullptr));
        std::vector<XrViewConfigurationType> types(count);
        xrCheckResult(xrEnumerateViewConfigurations(instance, system_id, count, &count, types.data()));


        bool found = false;
        for(uint32_t i = 0; i < count; i++) {
            if(types[i] == XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO) {
                found = true;
                break;
            }
        }
        if(!found) {
            throw std::runtime_error("Required View Configuration Stereo is not supported by OpenXR Runtime\n");
        }


        count = 0;
        xrCheckResult(xrEnumerateViewConfigurationViews(instance, system_id, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &count, nullptr));

        swapchains.resize(count);
        views.resize(count, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
        xrCheckResult(xrEnumerateViewConfigurationViews(instance, system_id, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, count, &count, views.data()));

    }


    // Swapchains

    for (uint32_t i = 0; i < views.size(); i++) {
        VRContext::swapchain& swapchain = swapchains[i];

        XrViewConfigurationView& view = views[i];
        XrSwapchainCreateInfo swapchain_info = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
        swapchain_info.arraySize = 1;
        swapchain_info.mipCount = 1;
        swapchain_info.faceCount = 1;
        swapchain_info.format = swapchain_format;
        swapchain_info.width = view.recommendedImageRectWidth;
        swapchain_info.height = view.recommendedImageRectHeight;
        swapchain_info.sampleCount = 1;
        swapchain_info.usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
        xrCheckResult(xrCreateSwapchain(session, &swapchain_info, &swapchain.handle));

        swapchain.width  = swapchain_info.width;
        swapchain.height = swapchain_info.height;

        // Swapchain images

        uint32_t surface_count = 0;
        xrCheckResult(xrEnumerateSwapchainImages(swapchain.handle, 0, &surface_count, nullptr));
        swapchain.images.resize(surface_count);

        std::vector<XrSwapchainImageVulkanKHR> surface_images(surface_count, {XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR});
        xrCheckResult(xrEnumerateSwapchainImages(swapchain.handle, surface_count, &surface_count, reinterpret_cast<XrSwapchainImageBaseHeader*> (surface_images.data())));

        for (uint32_t i = 0; i < surface_count; i++) {

            auto& image = swapchain.images[i];

            image.image = surface_images[i].image;


            // Swapchain image views

            vk::ImageViewCreateInfo info({}, image.image, vk::ImageViewType::e2D, vk::Format(swapchain_format),
                                         {}, vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));
            image.view = ctx.device->createImageView(info);

        }

    }

}


void VRContext::finish() {

    for(int i = 0; i<swapchains.size(); i++) {
        for(int j = 0; j<swapchains[i].images.size(); j++) {
            ctx.device->destroy(swapchains[i].images[j].view);
        }
        xrCheckResult(xrDestroySwapchain(swapchains[i].handle));
    }

    xrCheckResult(xrDestroySpace(space));

    xrCheckResult(xrDestroySession(session));

}

VRContext::~VRContext() {

#ifndef NDEBUG
    PFN_xrDestroyDebugUtilsMessengerEXT ext_xrDestroyDebugUtilsMessengerEXT = nullptr;
    xrCheckResult(xrGetInstanceProcAddr(instance, "xrDestroyDebugUtilsMessengerEXT", (PFN_xrVoidFunction *)(&ext_xrDestroyDebugUtilsMessengerEXT)));
    if(ext_xrDestroyDebugUtilsMessengerEXT)
        xrCheckResult(xrGetInstanceProcAddr(instance, "xrDestroyDebugUtilsMessengerEXT", (PFN_xrVoidFunction *)(&ext_xrDestroyDebugUtilsMessengerEXT)));
#endif

    xrCheckResult(xrDestroyInstance(instance));

}