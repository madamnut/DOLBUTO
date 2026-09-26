#include "render/renderer.hpp"
#include "world/profiling.hpp"
#include <SDL3/SDL_vulkan.h>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stb_image_write.h>
#include <stdexcept>
#include <tracy/Tracy.hpp>

namespace sandbox {
void vk_check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(operation) + ": VkResult=" + std::to_string(result));
}
namespace {
VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                              VkDebugUtilsMessageTypeFlagsEXT,
                                              const VkDebugUtilsMessengerCallbackDataEXT* data, void* user) {
    auto& renderer = *static_cast<Renderer*>(user);
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        ++renderer.validation_errors;
    else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        ++renderer.validation_warnings;
    std::cerr << "[Vulkan] " << data->pMessage << '\n';
    return VK_FALSE;
}
void barrier(VkCommandBuffer cmd, VkImage image, VkImageLayout old_layout, VkImageLayout new_layout,
             VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access, VkPipelineStageFlags2 dst_stage,
             VkAccessFlags2 dst_access, VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT) {
    VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    b.srcStageMask = src_stage;
    b.srcAccessMask = src_access;
    b.dstStageMask = dst_stage;
    b.dstAccessMask = dst_access;
    b.oldLayout = old_layout;
    b.newLayout = new_layout;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange = {aspect, 0, 1, 0, 1};
    VkDependencyInfo info{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    info.imageMemoryBarrierCount = 1;
    info.pImageMemoryBarriers = &b;
    vkCmdPipelineBarrier2(cmd, &info);
}
} // namespace
Renderer::~Renderer() { shutdown(); }
void Renderer::initialize(SDL_Window* window, bool validation) {
    window_ = window;
    vk_check(volkInitialize(), "volkInitialize");
    uint32_t version = VK_API_VERSION_1_0;
    if (vkEnumerateInstanceVersion)
        vk_check(vkEnumerateInstanceVersion(&version), "instance version");
    if (version < VK_API_VERSION_1_4)
        throw std::runtime_error("Vulkan 1.4 is required.");
    uint32_t count{};
    const auto sdl_extensions = SDL_Vulkan_GetInstanceExtensions(&count);
    if (!sdl_extensions)
        throw std::runtime_error(SDL_GetError());
    std::vector<const char*> extensions(sdl_extensions, sdl_extensions + count);
    const char* validation_layer = "VK_LAYER_KHRONOS_validation";
    if (validation) {
        vk_check(vkEnumerateInstanceLayerProperties(&count, nullptr), "enumerate layers");
        std::vector<VkLayerProperties> layers(count);
        vk_check(vkEnumerateInstanceLayerProperties(&count, layers.data()), "enumerate layers");
        validation_enabled = std::any_of(layers.begin(), layers.end(), [&](const auto& layer) {
            return std::strcmp(layer.layerName, validation_layer) == 0;
        });
        if (!validation_enabled)
            throw std::runtime_error("Validation requested but Khronos validation layer is missing.");
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
    VkDebugUtilsMessengerCreateInfoEXT debug{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    debug.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debug.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debug.pfnUserCallback = debug_callback;
    debug.pUserData = this;
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "Sandbox";
    app.pEngineName = "Sandbox";
    app.apiVersion = VK_API_VERSION_1_4;
    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    info.pApplicationInfo = &app;
    info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    info.ppEnabledExtensionNames = extensions.data();
    if (validation_enabled) {
        info.enabledLayerCount = 1;
        info.ppEnabledLayerNames = &validation_layer;
        info.pNext = &debug;
    }
    vk_check(vkCreateInstance(&info, nullptr, &instance), "create instance");
    volkLoadInstance(instance);
    if (validation_enabled)
        vk_check(vkCreateDebugUtilsMessengerEXT(instance, &debug, nullptr, &messenger_), "debug messenger");
    if (!SDL_Vulkan_CreateSurface(window_, instance, nullptr, &surface_))
        throw std::runtime_error(SDL_GetError());
    vk_check(vkEnumeratePhysicalDevices(instance, &count, nullptr), "enumerate devices");
    std::vector<VkPhysicalDevice> devices(count);
    vk_check(vkEnumeratePhysicalDevices(instance, &count, devices.data()), "enumerate devices");
    int best_score = -1;
    for (auto candidate : devices) {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(candidate, &properties);
        if (properties.apiVersion < VK_API_VERSION_1_4)
            continue;
        VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        features.pNext = &f13;
        vkGetPhysicalDeviceFeatures2(candidate, &features);
        if (!f13.dynamicRendering || !f13.synchronization2 || !f13.shaderDemoteToHelperInvocation)
            continue;
        uint32_t extension_count{};
        vk_check(vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extension_count, nullptr),
                 "device extensions");
        std::vector<VkExtensionProperties> device_extensions(extension_count);
        vk_check(vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extension_count,
                                                      device_extensions.data()),
                 "device extensions");
        if (std::none_of(device_extensions.begin(), device_extensions.end(), [](const auto& e) {
                return std::strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0;
            }))
            continue;
        uint32_t family_count{};
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &family_count, nullptr);
        std::vector<VkQueueFamilyProperties> families(family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &family_count, families.data());
        for (uint32_t i = 0; i < family_count; ++i) {
            VkBool32 present{};
            vk_check(vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, surface_, &present),
                     "present support");
            if (!(families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) || !present)
                continue;
            const int score = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 100 : 10;
            if (score <= best_score)
                continue;
            best_score = score;
            physical_device = candidate;
            queue_family = i;
            gpu_name_ = properties.deviceName;
            timestamp_period_ = properties.limits.timestampPeriod;
            timestamp_bits_ = families[i].timestampValidBits;
        }
    }
    if (!physical_device)
        throw std::runtime_error("No Vulkan 1.4 GPU with graphics/presentation, dynamic rendering, "
                                 "synchronization2 and shader demote.");
    const float priority = 1;
    VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qi.queueFamilyIndex = queue_family;
    qi.queueCount = 1;
    qi.pQueuePriorities = &priority;
    VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    f13.dynamicRendering = VK_TRUE;
    f13.synchronization2 = VK_TRUE;
    // glslc's Vulkan 1.4 discard uses helper invocations so derivatives remain
    // valid at the water exit mask. Core availability still requires enablement.
    f13.shaderDemoteToHelperInvocation = VK_TRUE;
    const char* swapchain_extension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    di.pNext = &f13;
    di.queueCreateInfoCount = 1;
    di.pQueueCreateInfos = &qi;
    di.enabledExtensionCount = 1;
    di.ppEnabledExtensionNames = &swapchain_extension;
    vk_check(vkCreateDevice(physical_device, &di, nullptr, &device), "create device");
    volkLoadDevice(device);
    vkGetDeviceQueue(device, queue_family, 0, &queue);
    VmaVulkanFunctions functions{};
    functions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    functions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
    VmaAllocatorCreateInfo ai{};
    ai.physicalDevice = physical_device;
    ai.device = device;
    ai.instance = instance;
    ai.vulkanApiVersion = VK_API_VERSION_1_4;
    ai.pVulkanFunctions = &functions;
    vk_check(vmaCreateAllocator(&ai, &allocator), "VMA allocator");
    VkDescriptorPoolSize pool_size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1024};
    VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pi.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pi.maxSets = 1024;
    pi.poolSizeCount = 1;
    pi.pPoolSizes = &pool_size;
    vk_check(vkCreateDescriptorPool(device, &pi, nullptr, &descriptor_pool), "descriptor pool");
    VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                                         VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    li.bindingCount = 1;
    li.pBindings = &binding;
    vk_check(vkCreateDescriptorSetLayout(device, &li, nullptr, &texture_layout), "texture layout");
    VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    pool.queueFamilyIndex = queue_family;
    vk_check(vkCreateCommandPool(device, &pool, nullptr, &upload_pool_), "upload pool");
    for (auto& frame : frames_) {
        pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        vk_check(vkCreateCommandPool(device, &pool, nullptr, &frame.pool), "frame pool");
        VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ca.commandPool = frame.pool;
        ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ca.commandBufferCount = 1;
        vk_check(vkAllocateCommandBuffers(device, &ca, &frame.command), "frame command");
        VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        vk_check(vkCreateFence(device, &fi, nullptr, &frame.fence), "frame fence");
        VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        vk_check(vkCreateSemaphore(device, &si, nullptr, &frame.acquired), "acquire semaphore");
        if (timestamp_bits_) {
            VkQueryPoolCreateInfo query{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
            query.queryType = VK_QUERY_TYPE_TIMESTAMP;
            query.queryCount = 4;
            vk_check(vkCreateQueryPool(device, &query, nullptr, &frame.queries), "timestamp queries");
            if (gpu_profile_enabled_) {
                query.queryCount = profile_capacity;
                vk_check(vkCreateQueryPool(device, &query, nullptr, &frame.profile_queries),
                         "profile queries");
            }
        }
    }
    create_swapchain();
    resize_requested_ = false;
    std::cout << "GPU: " << gpu_name_ << " | Vulkan 1.4 | validation=" << validation_enabled << '\n';
}
const char* Renderer::present_mode_description() const {
    if (present_mode_ == VK_PRESENT_MODE_IMMEDIATE_KHR)
        return "IMMEDIATE (VSync off)";
    if (present_mode_ == VK_PRESENT_MODE_MAILBOX_KHR)
        return "MAILBOX (synchronized fallback)";
    return vsync_requested_ ? "FIFO (VSync on)" : "FIFO (synchronized fallback)";
}
void Renderer::create_swapchain() {
    VkSurfaceCapabilitiesKHR caps{};
    vk_check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface_, &caps),
             "surface capabilities");
    uint32_t count{};
    vk_check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface_, &count, nullptr),
             "surface formats");
    std::vector<VkSurfaceFormatKHR> formats(count);
    vk_check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface_, &count, formats.data()),
             "surface formats");
    auto selected = std::find_if(formats.begin(), formats.end(), [](const auto& f) {
        return f.format == VK_FORMAT_B8G8R8A8_UNORM && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    });
    if (selected == formats.end())
        selected = std::find_if(formats.begin(), formats.end(), [](const auto& f) {
            return f.format == VK_FORMAT_R8G8B8A8_UNORM && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        });
    if (selected == formats.end())
        throw std::runtime_error("RGBA8/BGRA8 UNORM swapchain format unavailable.");
    if (colour_format && colour_format != selected->format)
        throw std::runtime_error("Surface format changed; restart required.");
    colour_format = selected->format;
    int w{}, h{};
    SDL_GetWindowSizeInPixels(window_, &w, &h);
    extent = caps.currentExtent.width != UINT32_MAX
                 ? caps.currentExtent
                 : VkExtent2D{std::clamp(static_cast<uint32_t>(w), caps.minImageExtent.width,
                                         caps.maxImageExtent.width),
                              std::clamp(static_cast<uint32_t>(h), caps.minImageExtent.height,
                                         caps.maxImageExtent.height)};
    uint32_t desired = std::max(3u, caps.minImageCount);
    if (caps.maxImageCount)
        desired = std::min(desired, caps.maxImageCount);
    if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT))
        throw std::runtime_error("Swapchain readback not supported.");
    VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    ci.surface = surface_;
    ci.minImageCount = desired;
    ci.imageFormat = colour_format;
    ci.imageColorSpace = selected->colorSpace;
    ci.imageExtent = extent;
    ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.preTransform = caps.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (!(caps.supportedCompositeAlpha & ci.compositeAlpha)) {
        for (auto flag : {VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                          VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR})
            if (caps.supportedCompositeAlpha & flag) {
                ci.compositeAlpha = flag;
                break;
            }
    }
    uint32_t mode_count{};
    vk_check(vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface_, &mode_count, nullptr),
             "surface present modes");
    std::vector<VkPresentModeKHR> modes(mode_count);
    vk_check(vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface_, &mode_count, modes.data()),
             "surface present modes");
    modes.resize(mode_count);
    // FIFO paces presentation to refresh; off retains the existing compatibility fallbacks.
    present_mode_ = VK_PRESENT_MODE_FIFO_KHR;
    if (!vsync_requested_) {
        for (auto preferred : {VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_MAILBOX_KHR}) {
            if (std::find(modes.begin(), modes.end(), preferred) != modes.end()) {
                present_mode_ = preferred;
                break;
            }
        }
    }
    ci.presentMode = present_mode_;
    ci.clipped = VK_TRUE;
    vk_check(vkCreateSwapchainKHR(device, &ci, nullptr, &swapchain_), "swapchain");
    std::cout << "Presentation: " << present_mode_description() << '\n';
    vk_check(vkGetSwapchainImagesKHR(device, swapchain_, &count, nullptr), "swapchain images");
    images_.resize(count);
    views_.resize(count);
    presented_.resize(count);
    vk_check(vkGetSwapchainImagesKHR(device, swapchain_, &count, images_.data()), "swapchain images");
    for (uint32_t i = 0; i < count; ++i) {
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = images_[i];
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = colour_format;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vk_check(vkCreateImageView(device, &vi, nullptr, &views_[i]), "swapchain image view");
        VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        vk_check(vkCreateSemaphore(device, &si, nullptr, &presented_[i]), "present semaphore");
    }
}
void Renderer::destroy_swapchain() {
    for (auto view : views_)
        vkDestroyImageView(device, view, nullptr);
    for (auto semaphore : presented_)
        vkDestroySemaphore(device, semaphore, nullptr);
    views_.clear();
    presented_.clear();
    images_.clear();
    if (swapchain_)
        vkDestroySwapchainKHR(device, swapchain_, nullptr);
    swapchain_ = VK_NULL_HANDLE;
}
bool Renderer::begin_frame() {
    ZoneScoped;
    int w{}, h{};
    SDL_GetWindowSizeInPixels(window_, &w, &h);
    if (w <= 0 || h <= 0 || (SDL_GetWindowFlags(window_) & SDL_WINDOW_MINIMIZED))
        return false;
    if (resize_requested_) {
        wait_idle();
        destroy_swapchain();
        create_swapchain();
        resize_requested_ = false;
    }
    auto& frame = frames_[frame_index_];
    vk_check(vkWaitForFences(device, 1, &frame.fence, VK_TRUE, UINT64_MAX), "frame wait");
    collect_gpu_profile(frame);
    completed_serial_ = std::max(completed_serial_, frame.serial);
    collect();
    // This frame slot's fence has completed: its staging pages can be reused safely.
    for (auto& page : frame.uploads)
        page.used = 0;
    if (frame.has_timestamps) {
        uint64_t timestamps[4]{};
        vk_check(vkGetQueryPoolResults(device, frame.queries, 0, 4, sizeof(timestamps), timestamps,
                                       sizeof(uint64_t), VK_QUERY_RESULT_64_BIT),
                 "GPU timings");
        const uint64_t mask = timestamp_bits_ == 64 ? UINT64_MAX : (uint64_t{1} << timestamp_bits_) - 1;
        gpu_ms_ = static_cast<double>((timestamps[1] - timestamps[0]) & mask) * timestamp_period_ / 1e6;
        upload_gpu_ms_ =
            static_cast<double>((timestamps[3] - timestamps[2]) & mask) * timestamp_period_ / 1e6;
    }
    if (frame.has_timestamps) {
        profiling::event(profiling::Stage::gpu_upload, 0, 0, -1, upload_gpu_ms_);
        profiling::event(profiling::Stage::gpu_frame, 0, 0, -1, gpu_ms_);
    }
    const auto acquired =
        vkAcquireNextImageKHR(device, swapchain_, UINT64_MAX, frame.acquired, VK_NULL_HANDLE, &image_index_);
    if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
        resize_requested_ = true;
        return false;
    }
    if (acquired != VK_SUBOPTIMAL_KHR)
        vk_check(acquired, "acquire image");
    else
        resize_requested_ = true;
    vk_check(vkResetCommandPool(device, frame.pool, 0), "reset commands");
    command = frame.command;
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vk_check(vkBeginCommandBuffer(command, &begin), "begin commands");
    recording_ = true;
    if (frame.profile_queries) {
        frame.profile = {};
        frame.profile.width = extent.width;
        frame.profile.height = extent.height;
        vkCmdResetQueryPool(command, frame.profile_queries, 0, profile_capacity);
        vkCmdWriteTimestamp2(command, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, frame.profile_queries, 0);
        frame.profile.count = 1;
    }
    rendering_started_ = false;
    set_world_target(VK_NULL_HANDLE, VK_NULL_HANDLE);
    if (frame.queries) {
        vkCmdResetQueryPool(command, frame.queries, 0, 4);
        vkCmdWriteTimestamp2(command, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, frame.queries, 0);
        vkCmdWriteTimestamp2(command, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, frame.queries, 2);
    }
    barrier(command, images_[image_index_], VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    return true;
}
void Renderer::finish_uploads() {
    if (!rendering_started_ && frames_[frame_index_].queries)
        vkCmdWriteTimestamp2(command, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, frames_[frame_index_].queries, 3);
    if (!rendering_started_)
        gpu_mark("uploads");
    rendering_started_ = true;
}
void Renderer::gpu_mark(const char* completed_stage) {
    auto& frame = frames_[frame_index_];
    if (!frame.profile_queries)
        return;
    if (frame.profile.count >= profile_capacity)
        throw std::runtime_error("GPU profile marker capacity exceeded.");
    const uint32_t index = frame.profile.count++;
    frame.profile.labels[index] = completed_stage;
    vkCmdWriteTimestamp2(command, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, frame.profile_queries, index);
}
void Renderer::gpu_profile_frame(bool steady, uint32_t columns, uint32_t chunks, uint32_t triangles) {
    auto& p = frames_[frame_index_].profile;
    p.steady = steady;
    p.columns = columns;
    p.chunks = chunks;
    p.triangles = triangles;
}
void Renderer::collect_gpu_profile(Frame& frame) {
    if (!frame.profile_pending)
        return;
    std::array<uint64_t, profile_capacity> values{};
    auto& p = frame.profile;
    vk_check(vkGetQueryPoolResults(device, frame.profile_queries, 0, p.count, sizeof(values), values.data(),
                                   sizeof(uint64_t), VK_QUERY_RESULT_64_BIT),
             "GPU pass timings");
    const uint64_t mask = timestamp_bits_ == 64 ? UINT64_MAX : (uint64_t{1} << timestamp_bits_) - 1;
    for (uint32_t i = 1; i < p.count; ++i)
        p.milliseconds[i] = double((values[i] - values[i - 1]) & mask) * timestamp_period_ / 1e6;
    p.milliseconds[0] = double((values[p.count - 1] - values[0]) & mask) * timestamp_period_ / 1e6;
    p.serial = frame.serial;
    gpu_profiles_.push_back(p);
    frame.profile_pending = false;
}
void Renderer::save_gpu_profile(const std::filesystem::path& path) {
    if (!gpu_profile_enabled_ || !timestamp_bits_)
        throw std::runtime_error("GPU pass timestamps are unavailable.");
    wait_idle();
    for (auto& frame : frames_)
        collect_gpu_profile(frame);
    std::sort(gpu_profiles_.begin(), gpu_profiles_.end(),
              [](const auto& a, const auto& b) { return a.serial < b.serial; });
    if (!path.parent_path().empty())
        std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path);
    out << "frame,steady,width,height,columns,chunks,triangles,stage,ms\n"
        << std::fixed << std::setprecision(6);
    for (const auto& p : gpu_profiles_) {
        for (uint32_t i = 0; i < p.count; ++i)
            out << p.serial << ',' << p.steady << ',' << p.width << ',' << p.height << ',' << p.columns << ','
                << p.chunks << ',' << p.triangles << ',' << (i == 0 ? "total" : p.labels[i]) << ','
                << p.milliseconds[i] << '\n';
    }
    out.flush();
    if (!out)
        throw std::runtime_error("Cannot save GPU pass timings.");
    std::cout << "GPU PROFILE: " << gpu_profiles_.size() << " frames, " << path << '\n';
}
void Renderer::begin_rendering(VkImageView depth, std::array<float, 4> sky, bool preserve_depth) {
    finish_uploads();
    VkRenderingAttachmentInfo attachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    attachment.imageView = (world_view_ ? world_view_ : views_[image_index_]);
    attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.clearValue.color = {{0.027f, 0.043f, 0.043f, 1.0f}};
    if (depth)
        attachment.clearValue.color = {{sky[0], sky[1], sky[2], sky[3]}};
    VkRenderingAttachmentInfo depth_attachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    depth_attachment.imageView = depth;
    depth_attachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth_attachment.storeOp =
        preserve_depth ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_attachment.clearValue.depthStencil = {1.0f, 0};
    VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
    rendering.renderArea.extent = extent;
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &attachment;
    rendering.pDepthAttachment = depth ? &depth_attachment : nullptr;
    vkCmdBeginRendering(command, &rendering);
}
void Renderer::snapshot_scene(VkImage colour, VkImage depth, VkImage depth_copy) {
    barrier(command, (world_image_ ? world_image_ : images_[image_index_]),
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
    barrier(command, depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COPY_BIT,
            VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
    VkImageCopy copy{};
    copy.srcSubresource = copy.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.extent = {extent.width, extent.height, 1};
    vkCmdCopyImage(command, (world_image_ ? world_image_ : images_[image_index_]),
                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, colour, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                   &copy);
    copy.srcSubresource.aspectMask = copy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    vkCmdCopyImage(command, depth, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, depth_copy,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    barrier(command, (world_image_ ? world_image_ : images_[image_index_]),
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    barrier(command, depth, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
            VK_ACCESS_2_TRANSFER_READ_BIT,
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            VK_IMAGE_ASPECT_DEPTH_BIT);
}
void Renderer::resume_world(VkImageView depth) {
    barrier(command, (world_image_ ? world_image_ : images_[image_index_]),
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    VkRenderingAttachmentInfo colour{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    colour.imageView = (world_view_ ? world_view_ : views_[image_index_]);
    colour.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colour.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colour.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingAttachmentInfo z{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    z.imageView = depth;
    z.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    z.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    z.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo info{VK_STRUCTURE_TYPE_RENDERING_INFO};
    info.renderArea.extent = extent;
    info.layerCount = info.colorAttachmentCount = 1;
    info.pColorAttachments = &colour;
    info.pDepthAttachment = &z;
    vkCmdBeginRendering(command, &info);
}
void Renderer::begin_ui() {
    vkCmdEndRendering(command);
    barrier(command, images_[image_index_], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    VkRenderingAttachmentInfo attachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    attachment.imageView = views_[image_index_];
    attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
    rendering.renderArea.extent = extent;
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &attachment;
    vkCmdBeginRendering(command, &rendering);
}
bool Renderer::end_frame(const std::filesystem::path& capture) {
    ZoneScoped;
    auto& frame = frames_[frame_index_];
    vkCmdEndRendering(command);
    gpu_mark("ui");
    if (frame.queries)
        vkCmdWriteTimestamp2(command, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, frame.queries, 1);
    Buffer readback{};
    if (!capture.empty()) {
        readback = create_buffer(static_cast<VkDeviceSize>(extent.width) * extent.height * 4,
                                 VK_BUFFER_USAGE_TRANSFER_DST_BIT, true);
        barrier(command, images_[image_index_], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COPY_BIT,
                VK_ACCESS_2_TRANSFER_READ_BIT);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {extent.width, extent.height, 1};
        vkCmdCopyImageToBuffer(command, images_[image_index_], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               readback.handle, 1, &copy);
        barrier(command, images_[image_index_], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                VK_PIPELINE_STAGE_2_NONE, 0);
    } else {
        barrier(command, images_[image_index_], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_NONE, 0);
    }
    vk_check(vkEndCommandBuffer(command), "end commands");
    VkSemaphoreSubmitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    wait.semaphore = frame.acquired;
    wait.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSemaphoreSubmitInfo signal{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    signal.semaphore = presented_[image_index_];
    signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkCommandBufferSubmitInfo buffer{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    buffer.commandBuffer = command;
    VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.waitSemaphoreInfoCount = 1;
    submit.pWaitSemaphoreInfos = &wait;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &buffer;
    submit.signalSemaphoreInfoCount = 1;
    submit.pSignalSemaphoreInfos = &signal;
    vk_check(vkResetFences(device, 1, &frame.fence), "reset frame fence");
    vk_check(vkQueueSubmit2(queue, 1, &submit, frame.fence), "submit frame");
    frame.serial = ++submitted_serial_;
    frame.profile_pending = frame.profile_queries != VK_NULL_HANDLE;
    frame.has_timestamps = frame.queries != VK_NULL_HANDLE;
    recording_ = false;
    VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &presented_[image_index_];
    present.swapchainCount = 1;
    present.pSwapchains = &swapchain_;
    present.pImageIndices = &image_index_;
    const auto result = vkQueuePresentKHR(queue, &present);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
        resize_requested_ = true;
    else
        vk_check(result, "present");
    bool captured = false;
    if (readback.handle) {
        wait_idle();
        vk_check(vmaInvalidateAllocation(allocator, readback.allocation, 0, VK_WHOLE_SIZE),
                 "readback invalidate");
        auto* pixels = static_cast<unsigned char*>(readback.mapped);
        if (colour_format == VK_FORMAT_B8G8R8A8_UNORM)
            for (size_t i = 0; i < static_cast<size_t>(extent.width) * extent.height * 4; i += 4)
                std::swap(pixels[i], pixels[i + 2]);
        // The screenshot is the displayed, opaque image, even if UI blending left fractional alpha.
        for (size_t i = 3; i < static_cast<size_t>(extent.width) * extent.height * 4; i += 4)
            pixels[i] = 255;
        try {
            if (!capture.parent_path().empty())
                std::filesystem::create_directories(capture.parent_path());
            // A filesystem path stream handles non-ASCII Windows directories; stb's narrow fopen does not.
            std::ofstream output(capture, std::ios::binary);
            const auto write = [](void* context, void* data, int size) {
                static_cast<std::ofstream*>(context)->write(static_cast<const char*>(data), size);
            };
            const int success = output
                                    ? stbi_write_png_to_func(write, &output, static_cast<int>(extent.width),
                                                             static_cast<int>(extent.height), 4, pixels,
                                                             static_cast<int>(extent.width * 4))
                                    : 0;
            output.close();
            captured = success != 0 && !output.fail();
        } catch (const std::exception& error) {
            std::cerr << "Screenshot: " << error.what() << '\n';
        }
        destroy_buffer(readback);
        if (captured)
            std::cout << "Captured: " << capture << '\n';
        else
            std::cerr << "Screenshot write failed: " << capture << '\n';
    }
    frame_index_ = (frame_index_ + 1) % static_cast<uint32_t>(frames_.size());
    return captured;
}
Buffer Renderer::create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, bool readback) {
    Buffer buffer;
    VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    ci.size = size;
    ci.usage = usage;
    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_AUTO;
    ai.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT |
               (readback ? VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT
                         : VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
    VmaAllocationInfo allocation{};
    vk_check(vmaCreateBuffer(allocator, &ci, &ai, &buffer.handle, &buffer.allocation, &allocation),
             "buffer allocation");
    buffer.mapped = allocation.pMappedData;
    if (!buffer.mapped) {
        destroy_buffer(buffer);
        throw std::runtime_error("VMA did not provide a mapped CPU-visible buffer.");
    }
    return buffer;
}
Renderer::UploadSlice Renderer::stage_buffer(const void* data, VkDeviceSize size) {
    auto& pages = frames_[frame_index_].uploads;
    if (!upload_alignment_) {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(physical_device, &properties);
        upload_alignment_ = std::max<VkDeviceSize>(4, properties.limits.nonCoherentAtomSize);
    }
    const VkDeviceSize alignment = upload_alignment_;
    for (auto& page : pages) {
        const auto offset = (page.used + alignment - 1) / alignment * alignment;
        if (offset <= page.capacity && size <= page.capacity - offset) {
            std::memcpy(static_cast<unsigned char*>(page.buffer.mapped) + offset, data,
                        static_cast<size_t>(size));
            vk_check(vmaFlushAllocation(allocator, page.buffer.allocation, offset, size),
                     "staging page flush");
            page.used = offset + size;
            return {page.buffer, offset};
        }
    }
    const VkDeviceSize capacity =
        std::max<VkDeviceSize>(2 * 1024 * 1024, (size + alignment - 1) / alignment * alignment);
    auto buffer = create_buffer(capacity, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    try {
        pages.push_back({buffer, capacity, size});
    } catch (...) {
        destroy_buffer(buffer);
        throw;
    }
    std::memcpy(buffer.mapped, data, static_cast<size_t>(size));
    vk_check(vmaFlushAllocation(allocator, buffer.allocation, 0, size), "staging page flush");
    return {buffer, 0};
}
Buffer Renderer::upload_buffer(const void* data, VkDeviceSize size, VkBufferUsageFlags usage) {
    if (!recording_ || !size)
        throw std::runtime_error("World upload requires an active frame and nonempty data.");
    Buffer destination{};
    UploadSlice staging{};
    try {
        VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        ci.size = size;
        ci.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        vk_check(vmaCreateBuffer(allocator, &ci, &ai, &destination.handle, &destination.allocation, nullptr),
                 "world buffer");
        staging = stage_buffer(data, size);
    } catch (...) {
        destroy_buffer(destination);
        throw;
    }
    VkBufferCopy copy{staging.offset, 0, size};
    vkCmdCopyBuffer(command, staging.buffer.handle, destination.handle, 1, &copy);
    VkBufferMemoryBarrier2 b{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
    b.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    b.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    b.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
    b.dstAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_2_INDEX_READ_BIT;
    if (usage & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) {
        b.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;
        b.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
    }
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.buffer = destination.handle;
    b.size = VK_WHOLE_SIZE;
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.bufferMemoryBarrierCount = 1;
    dependency.pBufferMemoryBarriers = &b;
    vkCmdPipelineBarrier2(command, &dependency);
    return destination;
}
void Renderer::destroy_buffer(Buffer buffer) {
    if (buffer.handle)
        vmaDestroyBuffer(allocator, buffer.handle, buffer.allocation);
}
void Renderer::immediate(const std::function<void(VkCommandBuffer)>& record) {
    VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocate.commandPool = upload_pool_;
    allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate.commandBufferCount = 1;
    VkCommandBuffer cmd{};
    vk_check(vkAllocateCommandBuffers(device, &allocate, &cmd), "upload command");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vk_check(vkBeginCommandBuffer(cmd, &begin), "begin upload");
    record(cmd);
    vk_check(vkEndCommandBuffer(cmd), "end upload");
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    vk_check(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE), "submit upload");
    vk_check(vkQueueWaitIdle(queue), "upload completion");
    vkFreeCommandBuffers(device, upload_pool_, 1, &cmd);
}
Texture Renderer::create_texture(const unsigned char* pixels, int width, int height, bool nearest,
                                 bool staged) {
    if (width <= 0 || height <= 0)
        throw std::runtime_error("Invalid texture dimensions.");
    if (staged && !recording_)
        throw std::runtime_error("Staged texture upload requires an active frame before rendering.");
    Texture texture;
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height) * 4;
    Buffer staging{};
    try {
        staging = create_buffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        std::memcpy(staging.mapped, pixels, static_cast<size_t>(bytes));
        vk_check(vmaFlushAllocation(allocator, staging.allocation, 0, VK_WHOLE_SIZE), "texture flush");
        VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ci.imageType = VK_IMAGE_TYPE_2D;
        ci.format = VK_FORMAT_R8G8B8A8_UNORM;
        ci.extent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
        ci.mipLevels = 1;
        ci.arrayLayers = 1;
        ci.samples = VK_SAMPLE_COUNT_1_BIT;
        ci.tiling = VK_IMAGE_TILING_OPTIMAL;
        ci.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        vk_check(vmaCreateImage(allocator, &ci, &ai, &texture.image, &texture.allocation, nullptr),
                 "texture allocation");
        // Allocate all fallible resources before recording commands which refer to the image.
        const auto record = [&](VkCommandBuffer cmd) {
            barrier(cmd, texture.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_PIPELINE_STAGE_2_NONE, 0, VK_PIPELINE_STAGE_2_COPY_BIT,
                    VK_ACCESS_2_TRANSFER_WRITE_BIT);
            VkBufferImageCopy copy{};
            copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copy.imageExtent = ci.extent;
            vkCmdCopyBufferToImage(cmd, staging.handle, texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                   1, &copy);
            barrier(cmd, texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                    VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                    VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        };
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = texture.image;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = ci.format;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vk_check(vkCreateImageView(device, &vi, nullptr, &texture.view), "texture view");
        VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sampler.magFilter = sampler.minFilter = nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
        sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampler.addressModeU = sampler.addressModeV = sampler.addressModeW =
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        vk_check(vkCreateSampler(device, &sampler, nullptr, &texture.sampler), "texture sampler");
        VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocate.descriptorPool = descriptor_pool;
        allocate.descriptorSetCount = 1;
        allocate.pSetLayouts = &texture_layout;
        vk_check(vkAllocateDescriptorSets(device, &allocate, &texture.descriptor), "texture descriptor");
        VkDescriptorImageInfo image{texture.sampler, texture.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = texture.descriptor;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &image;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
        if (staged) {
            defer([this, staging] { destroy_buffer(staging); });
            record(command);
            staging = {};
        } else {
            immediate(record);
            destroy_buffer(staging);
            staging = {};
        }
        return texture;
    } catch (...) {
        destroy_buffer(staging);
        destroy_texture(texture);
        throw;
    }
}
void Renderer::destroy_texture(Texture texture) {
    if (texture.descriptor)
        vkFreeDescriptorSets(device, descriptor_pool, 1, &texture.descriptor);
    if (texture.sampler)
        vkDestroySampler(device, texture.sampler, nullptr);
    if (texture.view)
        vkDestroyImageView(device, texture.view, nullptr);
    if (texture.image)
        vmaDestroyImage(allocator, texture.image, texture.allocation);
}
void Renderer::defer(std::function<void()> destroy) {
    deferred_.push_back({submitted_serial_ + (recording_ ? 1 : 0), std::move(destroy)});
}
void Renderer::collect() {
    std::erase_if(deferred_, [&](auto& item) {
        if (item.serial > completed_serial_)
            return false;
        item.destroy();
        return true;
    });
}
void Renderer::wait_idle() {
    if (device) {
        vk_check(vkDeviceWaitIdle(device), "device idle");
        completed_serial_ = submitted_serial_;
        collect();
    }
}
VkShaderModule Renderer::shader(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
        throw std::runtime_error("Cannot read shader: " + path.string());
    const auto size = static_cast<size_t>(file.tellg());
    if (size == 0 || size % 4 != 0)
        throw std::runtime_error("Invalid SPIR-V file.");
    std::vector<uint32_t> code(size / 4);
    file.seekg(0);
    file.read(reinterpret_cast<char*>(code.data()), static_cast<std::streamsize>(size));
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = size;
    ci.pCode = code.data();
    VkShaderModule module{};
    vk_check(vkCreateShaderModule(device, &ci, nullptr, &module), "shader module");
    return module;
}
void Renderer::memory_usage(uint64_t& used, uint64_t& allocated) const {
    VmaBudget budgets[VK_MAX_MEMORY_HEAPS]{};
    vmaGetHeapBudgets(allocator, budgets);
    used = allocated = 0;
    const VkPhysicalDeviceMemoryProperties* memory{};
    vmaGetMemoryProperties(allocator, &memory);
    for (uint32_t heap = 0; heap < memory->memoryHeapCount; ++heap) {
        used += budgets[heap].statistics.allocationBytes;
        allocated += budgets[heap].statistics.blockBytes;
    }
}
void Renderer::shutdown() noexcept {
    if (device) {
        vkDeviceWaitIdle(device);
        completed_serial_ = UINT64_MAX;
        collect();
        destroy_swapchain();
        for (auto& frame : frames_) {
            for (auto& page : frame.uploads)
                destroy_buffer(page.buffer);
            frame.uploads.clear();
            if (frame.queries)
                vkDestroyQueryPool(device, frame.queries, nullptr);
            if (frame.profile_queries)
                vkDestroyQueryPool(device, frame.profile_queries, nullptr);
            if (frame.acquired)
                vkDestroySemaphore(device, frame.acquired, nullptr);
            if (frame.fence)
                vkDestroyFence(device, frame.fence, nullptr);
            if (frame.pool)
                vkDestroyCommandPool(device, frame.pool, nullptr);
        }
        if (upload_pool_)
            vkDestroyCommandPool(device, upload_pool_, nullptr);
        if (texture_layout)
            vkDestroyDescriptorSetLayout(device, texture_layout, nullptr);
        if (descriptor_pool)
            vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
        if (allocator)
            vmaDestroyAllocator(allocator);
        vkDestroyDevice(device, nullptr);
    }
    if (surface_)
        vkDestroySurfaceKHR(instance, surface_, nullptr);
    if (messenger_)
        vkDestroyDebugUtilsMessengerEXT(instance, messenger_, nullptr);
    if (instance)
        vkDestroyInstance(instance, nullptr);
    device = VK_NULL_HANDLE;
    instance = VK_NULL_HANDLE;
    surface_ = VK_NULL_HANDLE;
    messenger_ = VK_NULL_HANDLE;
}
} // namespace sandbox
