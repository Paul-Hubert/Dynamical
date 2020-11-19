#include "vr_context.h"

#include <vector>
#include "util/util.h"
#include <algorithm>
#include <cstring>
#include <debugapi.h>

#define LOG_LEVEL VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT

static void debugCallback(XrDebugUtilsMessageSeverityFlagsEXT messageSeverity, XrDebugUtilsMessageTypeFlagsEXT messageType, const XrDebugUtilsMessengerCallbackDataEXT* msg, void* user_data) {

    if(messageSeverity < LOG_LEVEL) return;

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
}

VRContext::VRContext(Context &ctx) : ctx(ctx) {

    // OpenXR will fail to initialize if we ask for an extension that OpenXR
    // can't provide! So we need to check our all extensions before
    // initializing OpenXR with them. Note that even if the extension is
    // present, it's still possible you may not be able to use it. For
    // example: the hand tracking extension may be present, but the hand
    // sensor might not be plugged in or turned on. There are often
    // additional checks that should be made before using certain features!
    std::vector<const char*> use_extensions;
    const char         *ask_extensions[] = {
            XR_KHR_VULKAN_ENABLE_EXTENSION_NAME, // Use Direct3D11 for rendering
            XR_EXT_DEBUG_UTILS_EXTENSION_NAME,  // Debug utils for extra info
    };

    // We'll get a list of extensions that OpenXR provides using this
    // enumerate pattern. OpenXR often uses a two-call enumeration pattern
    // where the first call will tell you how much memory to allocate, and
    // the second call will provide you with the actual data!
    uint32_t ext_count = 0;
    xrEnumerateInstanceExtensionProperties(nullptr, 0, &ext_count, nullptr);
    std::vector<XrExtensionProperties> xr_exts(ext_count, { XR_TYPE_EXTENSION_PROPERTIES });
    xrEnumerateInstanceExtensionProperties(nullptr, ext_count, &ext_count, xr_exts.data());

    Util::log(Util::trace) << "OpenXR extensions available:\n";
    for (size_t i = 0; i < xr_exts.size(); i++) {
        Util::log(Util::trace) << "- " << xr_exts[i].extensionName << "\n";

        // Check if we're asking for this extensions, and add it to our use
        // list!
        for (int32_t ask = 0; ask < _countof(ask_extensions); ask++) {
            if (strcmp(ask_extensions[ask], xr_exts[i].extensionName) == 0) {
                use_extensions.push_back(ask_extensions[ask]);
                break;
            }
        }
    }
    // If a required extension isn't present, you want to ditch out here!
    // It's possible something like your rendering API might not be provided
    // by the active runtime. APIs like OpenGL don't have universal support.
    if (!std::any_of( use_extensions.begin(), use_extensions.end(),
                      [] (const char *ext) {
                          return strcmp(ext, XR_KHR_VULKAN_ENABLE_EXTENSION_NAME)==0;
                      }))
        throw std::runtime_error("OpenXR extensions not available\n");


    // Initialize OpenXR with the extensions we've found!
    XrInstanceCreateInfo createInfo = { XR_TYPE_INSTANCE_CREATE_INFO };
    createInfo.enabledExtensionCount      = use_extensions.size();
    createInfo.enabledExtensionNames      = use_extensions.data();
    createInfo.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
    strcpy_s(createInfo.applicationInfo.applicationName, "Dynamical");
    xrCreateInstance(&createInfo, &instance);

    // Check if OpenXR is on this system, if this is null here, the user
    // needs to install an OpenXR runtime and ensure it's active!
    if (instance == nullptr)
        throw std::runtime_error("OpenXR instance could not be created\n");


    // Load extension methods that we'll need for this application! There's a
    // couple ways to do this, and this is a fairly manual one. Chek out this
    // file for another way to do it:
    // https://github.com/maluoi/StereoKit/blob/master/StereoKitC/systems/platform/openxr_extensions.h

    // Function pointers for some OpenXR extension methods we'll use.
    PFN_xrGetVulkanGraphicsRequirementsKHR ext_xrGetVulkanGraphicsRequirementsKHR = nullptr;
    PFN_xrCreateDebugUtilsMessengerEXT    ext_xrCreateDebugUtilsMessengerEXT    = nullptr;
    PFN_xrDestroyDebugUtilsMessengerEXT   ext_xrDestroyDebugUtilsMessengerEXT   = nullptr;

    xrGetInstanceProcAddr(instance, "xrCreateDebugUtilsMessengerEXT", (PFN_xrVoidFunction *)(&ext_xrCreateDebugUtilsMessengerEXT));
    xrGetInstanceProcAddr(instance, "xrDestroyDebugUtilsMessengerEXT", (PFN_xrVoidFunction *)(&ext_xrDestroyDebugUtilsMessengerEXT));
    xrGetInstanceProcAddr(instance, "xrGetVulkanGraphicsRequirementsKHR", (PFN_xrVoidFunction *)(&ext_xrGetVulkanGraphicsRequirementsKHR));

    // Set up a really verbose debug log! Great for dev, but turn this off or
    // down for final builds. WMR doesn't produce much output here, but it
    // may be more useful for other runtimes?
    // Here's some extra information about the message types and severities:
    // https://www.khronos.org/registry/OpenXR/specs/1.0/html/xrspec.html#debug-message-categorization
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
    debug_info.userCallback = [](XrDebugUtilsMessageSeverityFlagsEXT severity, XrDebugUtilsMessageTypeFlagsEXT types, const XrDebugUtilsMessengerCallbackDataEXT *msg, void* user_data) {
        // Print the debug message we got! There's a bunch more info we could
        // add here too, but this is a pretty good start, and you can always
        // add a breakpoint this line!
        printf("%s: %s\n", msg->functionName, msg->message);

        // Output to debug window
        char text[512];
        sprintf_s(text, "%s: %s", msg->functionName, msg->message);

        // Returning XR_TRUE here will force the calling function to fail
        return (XrBool32)XR_FALSE;
    };



}

VRContext::~VRContext() {

}