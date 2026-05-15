#include "renderer/Renderer.h"

#include "platform/Log.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace dolbuto
{
    namespace
    {
        constexpr int MaxFramesInFlight = 2;
        constexpr std::array<const char*, 1> DeviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        constexpr const char* MemoryBudgetExtension = "VK_EXT_memory_budget";
        constexpr const char* PhysicalDeviceProperties2Extension = "VK_KHR_get_physical_device_properties2";

        bool deviceExtensionAvailable(VkPhysicalDevice device, const char* extensionName)
        {
            uint32_t extensionCount = 0;
            vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
            std::vector<VkExtensionProperties> extensions(extensionCount);
            vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, extensions.data());

            return std::any_of(extensions.begin(), extensions.end(), [extensionName](const VkExtensionProperties& extension)
            {
                return std::strcmp(extension.extensionName, extensionName) == 0;
            });
        }

        bool instanceExtensionAvailable(const char* extensionName)
        {
            uint32_t extensionCount = 0;
            vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
            std::vector<VkExtensionProperties> extensions(extensionCount);
            vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, extensions.data());

            return std::any_of(extensions.begin(), extensions.end(), [extensionName](const VkExtensionProperties& extension)
            {
                return std::strcmp(extension.extensionName, extensionName) == 0;
            });
        }
    }

    void Renderer::createInstance()
    {
        uint32_t extensionCount = 0;
        const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&extensionCount);
        if (glfwExtensions == nullptr || extensionCount == 0)
        {
            throw std::runtime_error("Failed to get required Vulkan instance extensions.");
        }

        std::vector<const char*> extensions(glfwExtensions, glfwExtensions + extensionCount);
        if (instanceExtensionAvailable(PhysicalDeviceProperties2Extension))
        {
            extensions.push_back(PhysicalDeviceProperties2Extension);
        }

        VkInstanceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        createInfo.ppEnabledExtensionNames = extensions.data();

        if (vkCreateInstance(&createInfo, nullptr, &vulkan_.instance) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create Vulkan instance.");
        }
    }



    void Renderer::createSurface()
    {
        if (glfwCreateWindowSurface(vulkan_.instance, window_, nullptr, &vulkan_.surface) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create window surface.");
        }
    }



    void Renderer::pickPhysicalDevice()
    {
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(vulkan_.instance, &deviceCount, nullptr);
        if (deviceCount == 0)
        {
            throw std::runtime_error("No Vulkan physical device found.");
        }

        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(vulkan_.instance, &deviceCount, devices.data());

        for (VkPhysicalDevice device : devices)
        {
            if (isDeviceSuitable(device))
            {
                vulkan_.physicalDevice = device;
                return;
            }
        }

        throw std::runtime_error("No suitable Vulkan physical device found.");
    }



    void Renderer::collectHardwareInfo()
    {
        const std::string cpuText = readCpuName();

        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(vulkan_.physicalDevice, &properties);
        const std::string gpuText = properties.deviceName;
        const std::string vulkanText = "VULKAN: " + formatVersion(properties.apiVersion);
        const std::string driverText = "DRIVER: " + formatVersion(properties.driverVersion);
        debugOverlayText_.setHardwareInfo(cpuText, gpuText, vulkanText, driverText);
        vulkan_.timestampPeriod = properties.limits.timestampPeriod;
        log::info("CPU: " + cpuText);
        log::info("GPU: " + gpuText);
        log::info(vulkanText);
        log::info(driverText);
        const bool memoryProperties2Supported =
            vkGetInstanceProcAddr(vulkan_.instance, "vkGetPhysicalDeviceMemoryProperties2") != nullptr ||
            vkGetInstanceProcAddr(vulkan_.instance, "vkGetPhysicalDeviceMemoryProperties2KHR") != nullptr;
        memoryBudgetSupported_ = memoryProperties2Supported && deviceExtensionAvailable(vulkan_.physicalDevice, MemoryBudgetExtension);

        VkPhysicalDeviceMemoryProperties memoryProperties{};
        vkGetPhysicalDeviceMemoryProperties(vulkan_.physicalDevice, &memoryProperties);
        for (uint32_t i = 0; i < memoryProperties.memoryHeapCount; ++i)
        {
            const VkMemoryHeap& heap = memoryProperties.memoryHeaps[i];
            if ((heap.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0 && heap.size > localMemoryHeapSize_)
            {
                localMemoryHeapIndex_ = i;
                localMemoryHeapSize_ = heap.size;
            }
        }
    }



    void Renderer::createDevice()
    {
        QueueFamilyIndices indices = findQueueFamilies(vulkan_.physicalDevice);
        std::set<uint32_t> uniqueFamilies = {indices.graphics, indices.present};
        float queuePriority = 1.0f;
        std::vector<VkDeviceQueueCreateInfo> queueInfos;

        for (uint32_t family : uniqueFamilies)
        {
            VkDeviceQueueCreateInfo queueInfo{};
            queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueInfo.queueFamilyIndex = family;
            queueInfo.queueCount = 1;
            queueInfo.pQueuePriorities = &queuePriority;
            queueInfos.push_back(queueInfo);
        }

        std::vector<const char*> enabledExtensions(DeviceExtensions.begin(), DeviceExtensions.end());
        if (memoryBudgetSupported_)
        {
            enabledExtensions.push_back(MemoryBudgetExtension);
        }

        VkPhysicalDeviceFeatures features{};
        features.fillModeNonSolid = VK_TRUE;
        VkDeviceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueInfos.size());
        createInfo.pQueueCreateInfos = queueInfos.data();
        createInfo.pEnabledFeatures = &features;
        createInfo.enabledExtensionCount = static_cast<uint32_t>(enabledExtensions.size());
        createInfo.ppEnabledExtensionNames = enabledExtensions.data();

        if (vkCreateDevice(vulkan_.physicalDevice, &createInfo, nullptr, &vulkan_.device) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create Vulkan device.");
        }

        vkGetDeviceQueue(vulkan_.device, indices.graphics, 0, &vulkan_.graphicsQueue);
        vkGetDeviceQueue(vulkan_.device, indices.present, 0, &vulkan_.presentQueue);
    }



    void Renderer::createCommandPool()
    {
        QueueFamilyIndices indices = findQueueFamilies(vulkan_.physicalDevice);

        VkCommandPoolCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        createInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        createInfo.queueFamilyIndex = indices.graphics;

        if (vkCreateCommandPool(vulkan_.device, &createInfo, nullptr, &vulkan_.commandPool) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create command pool.");
        }
    }



    void Renderer::createPerformanceQueries()
    {
        client_.diagnostics.performanceSampleStart = std::chrono::steady_clock::now();
        QueueFamilyIndices indices = findQueueFamilies(vulkan_.physicalDevice);

        uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(vulkan_.physicalDevice, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(vulkan_.physicalDevice, &familyCount, families.data());

        if (indices.graphics >= families.size() || families[indices.graphics].timestampValidBits == 0)
        {
            vulkan_.timestampSupported = false;
            debugOverlayText_.setGpuUnavailable();
            return;
        }

        vulkan_.timestampSupported = true;
        VkQueryPoolCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        createInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
        createInfo.queryCount = MaxFramesInFlight * 2;

        if (vkCreateQueryPool(vulkan_.device, &createInfo, nullptr, &vulkan_.timestampQueryPool) != VK_SUCCESS)
        {
            vulkan_.timestampSupported = false;
            debugOverlayText_.setGpuUnavailable();
            vulkan_.timestampQueryPool = VK_NULL_HANDLE;
            return;
        }
    }



    QueueFamilyIndices Renderer::findQueueFamilies(VkPhysicalDevice device) const
    {
        QueueFamilyIndices indices;

        uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, families.data());

        for (uint32_t i = 0; i < familyCount; ++i)
        {
            if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0)
            {
                indices.graphics = i;
            }

            VkBool32 presentSupport = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, vulkan_.surface, &presentSupport);
            if (presentSupport)
            {
                indices.present = i;
            }

            if (indices.complete())
            {
                break;
            }
        }

        return indices;
    }



    bool Renderer::isDeviceSuitable(VkPhysicalDevice device) const
    {
        QueueFamilyIndices indices = findQueueFamilies(device);
        if (!indices.complete())
        {
            return false;
        }

        uint32_t extensionCount = 0;
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
        std::vector<VkExtensionProperties> available(extensionCount);
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, available.data());

        std::set<std::string> required(DeviceExtensions.begin(), DeviceExtensions.end());
        for (const auto& extension : available)
        {
            required.erase(extension.extensionName);
        }

        if (!required.empty())
        {
            return false;
        }

        VkPhysicalDeviceFeatures features{};
        vkGetPhysicalDeviceFeatures(device, &features);
        if (features.fillModeNonSolid != VK_TRUE)
        {
            return false;
        }

        uint32_t formatCount = 0;
        uint32_t presentModeCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, vulkan_.surface, &formatCount, nullptr);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, vulkan_.surface, &presentModeCount, nullptr);
        return formatCount > 0 && presentModeCount > 0;
    }



    std::string Renderer::readCpuName() const
    {
    #ifdef _WIN32
        HKEY key = nullptr;
        const LONG openResult = RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ, &key);
        if (openResult != ERROR_SUCCESS)
        {
            return "Unknown";
        }

        char name[256]{};
        DWORD nameSize = sizeof(name);
        const LONG queryResult = RegQueryValueExA(key, "ProcessorNameString", nullptr, nullptr, reinterpret_cast<LPBYTE>(name), &nameSize);
        RegCloseKey(key);

        if (queryResult != ERROR_SUCCESS || name[0] == '\0')
        {
            return "Unknown";
        }

        return name;
    #else
        return "Unknown";
    #endif
    }



    std::string Renderer::formatVersion(uint32_t version) const
    {
        return std::to_string(VK_VERSION_MAJOR(version)) + "." +
            std::to_string(VK_VERSION_MINOR(version)) + "." +
            std::to_string(VK_VERSION_PATCH(version));
    }


}
