#include "vr_context.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "context.h"

#include "util/log.h"

#include "logic/settings/settings.h"

using namespace dy;

#ifndef NDEBUG
static XRAPI_ATTR XrBool32 XRAPI_CALL xrDebugCallback(XrDebugUtilsMessageSeverityFlagsEXT severity, XrDebugUtilsMessageTypeFlagsEXT,
                                                      const XrDebugUtilsMessengerCallbackDataEXT* data, void*) {
    if(severity >= XR_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        dy::log(dy::warning) << "[OpenXR] " << data->message << "\n";
    }
    return XR_FALSE;
}
#endif

template<typename T>
static T xrProc(XrInstance instance, const char* name) {
    PFN_xrVoidFunction fn = nullptr;
    xrCheckResult(xrGetInstanceProcAddr(instance, name, &fn), name);
    return reinterpret_cast<T>(fn);
}

VRContext::VRContext(Context& ctx, entt::registry& reg) : ctx(ctx), reg(reg) {

    auto& settings = reg.ctx().get<Settings>();
    if(settings.vr_mode == 0) {
        return;
    }

    // Extensions and layers

    uint32_t ext_count = 0;
    if(!xrCheckResult(xrEnumerateInstanceExtensionProperties(nullptr, 0, &ext_count, nullptr), "no OpenXR runtime")) {
        disable("no OpenXR runtime found");
        return;
    }
    std::vector<XrExtensionProperties> xr_exts(ext_count, { XR_TYPE_EXTENSION_PROPERTIES });
    xrCheckResult(xrEnumerateInstanceExtensionProperties(nullptr, ext_count, &ext_count, xr_exts.data()));

    std::vector<const char*> extensions { XR_KHR_VULKAN_ENABLE_EXTENSION_NAME };
#ifndef NDEBUG
    extensions.push_back(XR_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif
    extensions = checkXrExtensions(extensions, xr_exts);

    if(std::find_if(extensions.begin(), extensions.end(), [](const char* e) { return strcmp(e, XR_KHR_VULKAN_ENABLE_EXTENSION_NAME) == 0; }) == extensions.end()) {
        disable("runtime does not support XR_KHR_vulkan_enable");
        return;
    }

    uint32_t layer_count = 0;
    xrCheckResult(xrEnumerateApiLayerProperties(0, &layer_count, nullptr));
    std::vector<XrApiLayerProperties> xr_layers(layer_count, { XR_TYPE_API_LAYER_PROPERTIES });
    xrCheckResult(xrEnumerateApiLayerProperties(layer_count, &layer_count, xr_layers.data()));

    std::vector<const char*> layers = checkXrLayers({}, xr_layers);


    // Instance

    XrInstanceCreateInfo createInfo = { XR_TYPE_INSTANCE_CREATE_INFO };
    createInfo.enabledExtensionCount = (uint32_t) extensions.size();
    createInfo.enabledExtensionNames = extensions.data();
    createInfo.enabledApiLayerCount = (uint32_t) layers.size();
    createInfo.enabledApiLayerNames = layers.data();
    createInfo.applicationInfo.apiVersion = XR_API_VERSION_1_0;
    strncpy(createInfo.applicationInfo.applicationName, "Dynamical", XR_MAX_APPLICATION_NAME_SIZE);
    strncpy(createInfo.applicationInfo.engineName, "Dynamical", XR_MAX_ENGINE_NAME_SIZE);

    if(!xrCheckResult(xrCreateInstance(&createInfo, &instance), "xrCreateInstance") || instance == XR_NULL_HANDLE) {
        disable("could not create the OpenXR instance");
        return;
    }


    // Debug messenger

#ifndef NDEBUG
    if(std::find_if(extensions.begin(), extensions.end(), [](const char* e) { return strcmp(e, XR_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0; }) != extensions.end()) {
        XrDebugUtilsMessengerCreateInfoEXT debug_info = { XR_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
        debug_info.messageTypes =
                XR_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT     |
                XR_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT  |
                XR_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT |
                XR_DEBUG_UTILS_MESSAGE_TYPE_CONFORMANCE_BIT_EXT;
        debug_info.messageSeverities =
                XR_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                XR_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debug_info.userCallback = &xrDebugCallback;

        auto create = xrProc<PFN_xrCreateDebugUtilsMessengerEXT>(instance, "xrCreateDebugUtilsMessengerEXT");
        if(create) xrCheckResult(create(instance, &debug_info, &debug));
    }
#endif


    // System

    XrSystemGetInfo systemInfo = { XR_TYPE_SYSTEM_GET_INFO };
    systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    if(!xrCheckResult(xrGetSystem(instance, &systemInfo, &system_id), "xrGetSystem")) {
        disable("no headset connected");
        return;
    }

    // Must be called before creating the session, even if we ignore the result here
    auto getRequirements = xrProc<PFN_xrGetVulkanGraphicsRequirementsKHR>(instance, "xrGetVulkanGraphicsRequirementsKHR");
    XrGraphicsRequirementsVulkanKHR requirements = { XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR };
    xrCheckResult(getRequirements(instance, system_id, &requirements));

    active = true;
    dy::log(dy::info) << "OpenXR runtime ready, VR rendering enabled\n";

}

void VRContext::disable(const char* reason) {

    dy::log(dy::warning) << "VR disabled : " << reason << "\n";

    if(space != XR_NULL_HANDLE) xrDestroySpace(space);
    space = XR_NULL_HANDLE;
    if(session != XR_NULL_HANDLE) xrDestroySession(session);
    session = XR_NULL_HANDLE;

#ifndef NDEBUG
    if(debug != XR_NULL_HANDLE) {
        auto destroy = xrProc<PFN_xrDestroyDebugUtilsMessengerEXT>(instance, "xrDestroyDebugUtilsMessengerEXT");
        if(destroy) destroy(debug);
        debug = XR_NULL_HANDLE;
    }
#endif

    if(instance != XR_NULL_HANDLE) {
        xrDestroyInstance(instance);
        instance = XR_NULL_HANDLE;
    }

    active = false;

}

std::vector<std::string> VRContext::vulkanInstanceExtensions() {
    auto get = xrProc<PFN_xrGetVulkanInstanceExtensionsKHR>(instance, "xrGetVulkanInstanceExtensionsKHR");
    uint32_t size = 0;
    xrCheckResult(get(instance, system_id, 0, &size, nullptr));
    std::string buf(size, '\0');
    xrCheckResult(get(instance, system_id, size, &size, buf.data()));
    return splitExtensionString(buf.c_str());
}

std::vector<std::string> VRContext::vulkanDeviceExtensions() {
    auto get = xrProc<PFN_xrGetVulkanDeviceExtensionsKHR>(instance, "xrGetVulkanDeviceExtensionsKHR");
    uint32_t size = 0;
    xrCheckResult(get(instance, system_id, 0, &size, nullptr));
    std::string buf(size, '\0');
    xrCheckResult(get(instance, system_id, size, &size, buf.data()));
    return splitExtensionString(buf.c_str());
}

uint32_t VRContext::vulkanApiVersion(uint32_t wanted) {

    auto getRequirements = xrProc<PFN_xrGetVulkanGraphicsRequirementsKHR>(instance, "xrGetVulkanGraphicsRequirementsKHR");
    XrGraphicsRequirementsVulkanKHR requirements = { XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR };
    xrCheckResult(getRequirements(instance, system_id, &requirements));

    uint32_t min = VK_MAKE_API_VERSION(0, XR_VERSION_MAJOR(requirements.minApiVersionSupported), XR_VERSION_MINOR(requirements.minApiVersionSupported), 0);
    uint32_t max = VK_MAKE_API_VERSION(0, XR_VERSION_MAJOR(requirements.maxApiVersionSupported), XR_VERSION_MINOR(requirements.maxApiVersionSupported), 0);

    return std::clamp(wanted, min, std::max(min, max));

}

VkPhysicalDevice VRContext::vulkanPhysicalDevice(VkInstance vk_instance) {
    auto get = xrProc<PFN_xrGetVulkanGraphicsDeviceKHR>(instance, "xrGetVulkanGraphicsDeviceKHR");
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    xrCheckResult(get(instance, system_id, vk_instance, &physical));
    return physical;
}

void VRContext::init() {

    if(!active) {
        return;
    }

    // Session

    XrGraphicsBindingVulkanKHR binding = { XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR };
    binding.instance = ctx.instance;
    binding.physicalDevice = static_cast<VkPhysicalDevice>(ctx.device.physical);
    binding.device = static_cast<VkDevice>(ctx.device.logical);
    binding.queueFamilyIndex = ctx.device.g_i;
    binding.queueIndex = 0;
    XrSessionCreateInfo sessionInfo = { XR_TYPE_SESSION_CREATE_INFO };
    sessionInfo.next = &binding;
    sessionInfo.systemId = system_id;
    if(!xrCheckResult(xrCreateSession(instance, &sessionInfo, &session), "xrCreateSession") || session == XR_NULL_HANDLE) {
        disable("could not create the OpenXR session");
        return;
    }


    // Space : stage if available, local otherwise

    {
        uint32_t count = 0;
        xrCheckResult(xrEnumerateReferenceSpaces(session, 0, &count, nullptr));
        std::vector<XrReferenceSpaceType> space_types(count);
        xrCheckResult(xrEnumerateReferenceSpaces(session, count, &count, space_types.data()));

        XrReferenceSpaceType type = XR_REFERENCE_SPACE_TYPE_STAGE;
        if(std::find(space_types.begin(), space_types.end(), XR_REFERENCE_SPACE_TYPE_STAGE) == space_types.end()) {
            dy::log(dy::warning) << "Stage reference space is not supported by OpenXR Runtime, switching to Local\n";
            type = XR_REFERENCE_SPACE_TYPE_LOCAL;
        }

        XrReferenceSpaceCreateInfo ref_space = {XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
        ref_space.poseInReferenceSpace = pose_identity;
        ref_space.referenceSpaceType = type;
        xrCheckResult(xrCreateReferenceSpace(session, &ref_space, &space));
    }


    // Format : prefer sRGB to match the desktop swapchain

    {
        uint32_t count = 0;
        xrCheckResult(xrEnumerateSwapchainFormats(session, 0, &count, nullptr));
        std::vector<int64_t> formats(count);
        xrCheckResult(xrEnumerateSwapchainFormats(session, count, &count, formats.data()));

        const std::vector<VkFormat> preferred { VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_B8G8R8A8_SRGB };
        bool found = false;
        for(VkFormat format : preferred) {
            if(std::find(formats.begin(), formats.end(), (int64_t) format) != formats.end()) {
                swapchain_format = format;
                found = true;
                break;
            }
        }
        if(!found && !formats.empty()) {
            dy::log(dy::warning) << "No sRGB swapchain format supported by OpenXR Runtime, switching to : " << formats[0] << "\n";
            swapchain_format = static_cast<VkFormat>(formats[0]);
        }
    }


    // View Configuration

    std::vector<XrViewConfigurationView> views;
    {
        uint32_t count = 0;
        xrCheckResult(xrEnumerateViewConfigurations(instance, system_id, 0, &count, nullptr));
        std::vector<XrViewConfigurationType> types(count);
        xrCheckResult(xrEnumerateViewConfigurations(instance, system_id, count, &count, types.data()));

        if(std::find(types.begin(), types.end(), XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO) == types.end()) {
            disable("stereo view configuration is not supported by the OpenXR runtime");
            return;
        }

        count = 0;
        xrCheckResult(xrEnumerateViewConfigurationViews(instance, system_id, view_type, 0, &count, nullptr));
        num_views = count;
        swapchains.resize(count);
        views.resize(count, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
        xrCheckResult(xrEnumerateViewConfigurationViews(instance, system_id, view_type, count, &count, views.data()));
    }


    // Swapchains, one per eye

    for(uint32_t v = 0; v < views.size(); v++) {
        VRContext::swapchain& swapchain = swapchains[v];

        XrViewConfigurationView& view = views[v];
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
        xrCheckResult(xrEnumerateSwapchainImages(swapchain.handle, surface_count, &surface_count, reinterpret_cast<XrSwapchainImageBaseHeader*>(surface_images.data())));

        for(uint32_t i = 0; i < surface_count; i++) {
            auto& image = swapchain.images[i];
            image.image = surface_images[i].image;

            vk::ImageViewCreateInfo info({}, image.image, vk::ImageViewType::e2D, vk::Format(swapchain_format),
                                         {}, vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));
            image.view = ctx.device->createImageView(info);
        }

    }

}


void VRContext::finish() {

    if(!active) {
        return;
    }

    for(auto& swapchain : swapchains) {
        for(auto& image : swapchain.images) {
            ctx.device->destroy(image.view);
        }
        xrCheckResult(xrDestroySwapchain(swapchain.handle));
    }
    swapchains.clear();

    if(space != XR_NULL_HANDLE) xrCheckResult(xrDestroySpace(space));
    space = XR_NULL_HANDLE;

    if(session != XR_NULL_HANDLE) xrCheckResult(xrDestroySession(session));
    session = XR_NULL_HANDLE;

}

VRContext::~VRContext() {

    if(instance == XR_NULL_HANDLE) {
        return;
    }

#ifndef NDEBUG
    if(debug != XR_NULL_HANDLE) {
        auto destroy = xrProc<PFN_xrDestroyDebugUtilsMessengerEXT>(instance, "xrDestroyDebugUtilsMessengerEXT");
        if(destroy) destroy(debug);
    }
#endif

    xrCheckResult(xrDestroyInstance(instance));

}
