#include "vr_context.h"

#include <vector>
#include "util/util.h"
#include <algorithm>
#include <cstring>
#include <debugapi.h>
#include "context.h"

#define LOG_LEVEL VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT

static XRAPI_ATTR XrBool32 XRAPI_CALL debugCallback(XrDebugUtilsMessageSeverityFlagsEXT messageSeverity, XrDebugUtilsMessageTypeFlagsEXT messageType, const XrDebugUtilsMessengerCallbackDataEXT* msg, void* user_data) {

    if(messageSeverity < LOG_LEVEL) return XR_FALSE;

    if(messageType == XR_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT) {
        std::cerr << "    General ";
    } else if(messageType == XR_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) {
        std::cerr << "Performance ";
    } else if(messageType == XR_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) {
        std::cerr << " Validation ";
    }

    if(messageSeverity == XR_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) {
        std::cerr << "Verbose ";
    } else if(messageSeverity == XR_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
        std::cerr << "Info    ";
    } else if(messageSeverity == XR_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        std::cerr << "Warning ";
    } else if(messageSeverity == XR_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        std::cerr << "Error   ";
    }

    std::cerr << msg->message << std::endl;

    if(messageSeverity >= XR_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        std::cerr << std::endl;
    }

    return XR_FALSE;
}

void xrCheckResult(XrResult result) {
    if(result != XR_SUCCESS) {
        Util::log(Util::error) << "XR Error : " << result << "\n";
    }
}

const XrPosef  pose_identity = { {0,0,0,1}, {0,0,0} };

VRContext::VRContext(Context &ctx) : ctx(ctx) {

    std::vector<const char*> use_extensions {
            XR_KHR_VULKAN_ENABLE_EXTENSION_NAME,
            XR_EXT_DEBUG_UTILS_EXTENSION_NAME,
    };

   


    uint32_t ext_count = 0;
    xrCheckResult(xrEnumerateInstanceExtensionProperties(nullptr, 0, &ext_count, nullptr));
    std::vector<XrExtensionProperties> xr_exts(ext_count, { XR_TYPE_EXTENSION_PROPERTIES });
    xrCheckResult(xrEnumerateInstanceExtensionProperties(nullptr, ext_count, &ext_count, xr_exts.data()));


    uint32_t layer_count = 0;
    xrCheckResult(xrEnumerateApiLayerProperties(0, &layer_count, nullptr));
    std::vector<XrApiLayerProperties> xr_layers(layer_count, {XR_TYPE_API_LAYER_PROPERTIES});
    xrCheckResult(xrEnumerateApiLayerProperties(layer_count, &layer_count, xr_layers.data()));


    XrInstanceCreateInfo createInfo = { XR_TYPE_INSTANCE_CREATE_INFO };
    createInfo.enabledExtensionCount      = use_extensions.size();
    createInfo.enabledExtensionNames      = use_extensions.data();
    createInfo.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
    strcpy_s(createInfo.applicationInfo.applicationName, "Dynamical");
    
    xrCheckResult(xrCreateInstance(&createInfo, &instance));


    if (instance == nullptr)
        throw std::runtime_error("OpenXR instance could not be created\n");



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


    XrSystemGetInfo systemInfo = { XR_TYPE_SYSTEM_GET_INFO };
    systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    xrCheckResult(xrGetSystem(instance, &systemInfo, &system_id));

}

void VRContext::init() {


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



    XrReferenceSpaceCreateInfo ref_space = { XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
    ref_space.poseInReferenceSpace = pose_identity;
    ref_space.referenceSpaceType   = XR_REFERENCE_SPACE_TYPE_STAGE;
    xrCheckResult(xrCreateReferenceSpace(session, &ref_space, &space));


    uint32_t view_count = 0;
    xrCheckResult(xrEnumerateViewConfigurationViews(instance, system_id, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &view_count, nullptr));

    swapchains.resize(view_count);
    std::vector<XrViewConfigurationView> views(view_count, { XR_TYPE_VIEW_CONFIGURATION_VIEW });
    xrCheckResult(xrEnumerateViewConfigurationViews(instance, system_id, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, view_count, &view_count, views.data()));


    for (uint32_t i = 0; i < view_count; i++) {
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

        uint32_t surface_count = 0;
        xrCheckResult(xrEnumerateSwapchainImages(swapchain.handle, 0, &surface_count, nullptr));
        swapchain.images.resize(surface_count);

        std::vector<XrSwapchainImageVulkanKHR> surface_images(surface_count, {XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR});
        xrCheckResult(xrEnumerateSwapchainImages(swapchain.handle, surface_count, &surface_count, reinterpret_cast<XrSwapchainImageBaseHeader*> (surface_images.data())));

        for (uint32_t i = 0; i < surface_count; i++) {

            auto& image = swapchain.images[i];

            image.image = surface_images[i].image;

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
        xrDestroySwapchain(swapchains[i].handle);
    }

    xrDestroySpace(space);

    xrDestroySession(session);

}

VRContext::~VRContext() {

    PFN_xrDestroyDebugUtilsMessengerEXT ext_xrDestroyDebugUtilsMessengerEXT = nullptr;
    xrGetInstanceProcAddr(instance, "xrDestroyDebugUtilsMessengerEXT", (PFN_xrVoidFunction *)(&ext_xrDestroyDebugUtilsMessengerEXT));
    if(ext_xrDestroyDebugUtilsMessengerEXT)
        xrGetInstanceProcAddr(instance, "xrDestroyDebugUtilsMessengerEXT", (PFN_xrVoidFunction *)(&ext_xrDestroyDebugUtilsMessengerEXT));

    xrDestroyInstance(instance);

}