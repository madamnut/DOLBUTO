#include "renderer/Renderer.h"

#include "platform/RuntimePaths.h"
#include "renderer/RendererAudioBridge.h"
#include "renderer/RendererConfigBridge.h"
#include "renderer/RendererDiagnosticsBridge.h"
#include "renderer/RendererGameplayBridge.h"
#include "renderer/RendererRmlUiBackend.h"
#include "renderer/RendererSceneLifecycleBridge.h"
#include "renderer/RendererTerrainRuntimeBridge.h"
#include "renderer/RendererUiRuntimeBridge.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>

namespace dolbuto
{
    bool QueueFamilyIndices::complete() const
    {
        return graphics != UINT32_MAX && present != UINT32_MAX;
    }

    Renderer::Renderer(GLFWwindow* window, game::ClientRuntimeState& client)
        : window_(window),
        client_(client),
        gpuResources_(&vulkan_.physicalDevice, &vulkan_.device, &vulkan_.graphicsQueue, &vulkan_.commandPool, &vulkan_.descriptorPool, &vulkan_.descriptorSetLayout, &vulkan_.sampler),
        terrainRenderPath_(&vulkan_.device, &vulkan_.descriptorPool, &vulkan_.terrainVertexDescriptorSetLayout, &gpuResources_),
        textRenderPath_(&vulkan_.device, &gpuResources_),
        playerMeshRenderPath_(&vulkan_.device, &gpuResources_),
        particleRenderPath_(&vulkan_.device, &gpuResources_),
        droppedItemRenderPath_(&vulkan_.device, &gpuResources_)
    {
        createInstance();
        createSurface();
        pickPhysicalDevice();
        collectHardwareInfo();
        createDevice();
        createSwapchain();
        createImageViews();
        createRenderPass();
        createSceneRenderPass();
        createDepthResources();
        createDescriptorSetLayout();
        createTerrainVertexDescriptorSetLayout();
        createPipeline();
        createUiPipeline();
        createTerrainPipeline();
        createParticlePipeline();
        createSelectionPipeline();
        createCommandPool();
        createPerformanceQueries();
        createSampler();
        createDescriptorPool();
        createSceneTargets();
        createFramebuffers();
        configBridge_ = std::make_unique<RendererConfigBridge>(client_, rendererAssets_, gpuResources_);
        configBridge_->loadContentAndAssets(assetDirectory());
        audioBridge_ = std::make_unique<RendererAudioBridge>(client_.audio, assetDirectory());
        terrainRuntimeBridge_ = std::make_unique<RendererTerrainRuntimeBridge>(client_, rendererAssets_, terrainRenderPath_, debugOverlayText_);
        gameplayBridge_ = std::make_unique<RendererGameplayBridge>(
            client_,
            vulkan_,
            particleRenderPath_,
            RendererGameplayBridge::Hooks{
                [this]()
                {
                    uiRuntimeBridge_->updateInventoryUi();
                },
                [this](RuntimeChunk& chunk)
                {
                    terrainRuntimeBridge_->markRuntimeChunkDataDirty(chunk);
                },
                [this](int x, int y, int z, uint16_t block)
                {
                    return terrainRuntimeBridge_->setBlockAtWorld(x, y, z, block);
                },
                [this](int x, int y, int z)
                {
                    terrainRuntimeBridge_->rebuildEditedChunkMeshes(x, y, z);
                },
                [this](int x, int y, int z)
                {
                    audioBridge_->playBlockBreak(x, y, z);
                },
                [this](int x, int y, int z)
                {
                    audioBridge_->playBlockPlace(x, y, z);
                }
            });
        diagnosticsBridge_ = std::make_unique<RendererDiagnosticsBridge>(
            client_,
            vulkan_,
            debugOverlayText_,
            textRenderPath_,
            *terrainRuntimeBridge_,
            localMemoryHeapSize_,
            localMemoryHeapIndex_,
            memoryBudgetSupported_);
        diagnosticsBridge_->updateVramText();
        rmlUiBackend_ = std::make_unique<RendererRmlUiBackend>(vulkan_, gpuResources_, rendererAssets_, assetDirectory());
        uiRuntimeBridge_ = std::make_unique<RendererUiRuntimeBridge>(
            window_,
            client_,
            vulkan_,
            *rmlUiBackend_,
            *audioBridge_,
            assetDirectory());
        sceneLifecycleBridge_ = std::make_unique<RendererSceneLifecycleBridge>(
            client_,
            vulkan_,
            particleRenderPath_,
            debugOverlayText_,
            *terrainRuntimeBridge_,
            *gameplayBridge_,
            *uiRuntimeBridge_);
        createTextRenderPath();
        createUiBuffers();
        createRenderPathBuffers();
        createSelectionLineBuffer();
        createPlayerMesh();
        uiRuntimeBridge_->initialize();
        configBridge_->loadWorldConfig(configDirectory());
        configBridge_->loadRenderConfig(configDirectory());
        configBridge_->loadTerrainLuts(assetDirectory());
        createCommandBuffers();
        createSyncObjects();
    }

    Renderer::~Renderer()
    {
        sceneLifecycleBridge_->unloadGameScene();
        vkDeviceWaitIdle(vulkan_.device);
        uiRuntimeBridge_->shutdown();
        audioBridge_->shutdown();

        cleanupSwapchain();
        rendererAssets_.destroy(gpuResources_);

        terrainRuntimeBridge_->destroyAllTerrainChunks();
        playerMeshRenderPath_.destroy();
        textRenderPath_.destroy();
        if (vulkan_.uiVertexBuffer != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(vulkan_.device, vulkan_.uiVertexBuffer, nullptr);
        }
        if (vulkan_.uiVertexMemory != VK_NULL_HANDLE)
        {
            vkFreeMemory(vulkan_.device, vulkan_.uiVertexMemory, nullptr);
        }
        if (vulkan_.uiIndexBuffer != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(vulkan_.device, vulkan_.uiIndexBuffer, nullptr);
        }
        if (vulkan_.uiIndexMemory != VK_NULL_HANDLE)
        {
            vkFreeMemory(vulkan_.device, vulkan_.uiIndexMemory, nullptr);
        }
        particleRenderPath_.destroy();
        droppedItemRenderPath_.destroy();
        if (vulkan_.selectionLineVertexBuffer != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(vulkan_.device, vulkan_.selectionLineVertexBuffer, nullptr);
        }
        if (vulkan_.selectionLineVertexMemory != VK_NULL_HANDLE)
        {
            vkFreeMemory(vulkan_.device, vulkan_.selectionLineVertexMemory, nullptr);
        }

        if (vulkan_.descriptorPool != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorPool(vulkan_.device, vulkan_.descriptorPool, nullptr);
        }
        if (vulkan_.sampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(vulkan_.device, vulkan_.sampler, nullptr);
        }

        for (size_t i = 0; i < vulkan_.imageAvailableSemaphores.size(); ++i)
        {
            vkDestroySemaphore(vulkan_.device, vulkan_.imageAvailableSemaphores[i], nullptr);
            vkDestroySemaphore(vulkan_.device, vulkan_.renderFinishedSemaphores[i], nullptr);
            vkDestroyFence(vulkan_.device, vulkan_.inFlightFences[i], nullptr);
        }

        if (vulkan_.commandPool != VK_NULL_HANDLE)
        {
            vkDestroyCommandPool(vulkan_.device, vulkan_.commandPool, nullptr);
        }
        if (vulkan_.timestampQueryPool != VK_NULL_HANDLE)
        {
            vkDestroyQueryPool(vulkan_.device, vulkan_.timestampQueryPool, nullptr);
        }
        if (vulkan_.terrainWireframePipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(vulkan_.device, vulkan_.terrainWireframePipeline, nullptr);
        }
        if (vulkan_.fluidPipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(vulkan_.device, vulkan_.fluidPipeline, nullptr);
        }
        if (vulkan_.playerPipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(vulkan_.device, vulkan_.playerPipeline, nullptr);
        }
        if (vulkan_.particlePipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(vulkan_.device, vulkan_.particlePipeline, nullptr);
        }
        if (vulkan_.itemPipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(vulkan_.device, vulkan_.itemPipeline, nullptr);
        }
        if (vulkan_.particlePipelineLayout != VK_NULL_HANDLE)
        {
            vkDestroyPipelineLayout(vulkan_.device, vulkan_.particlePipelineLayout, nullptr);
        }
        if (vulkan_.selectionPipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(vulkan_.device, vulkan_.selectionPipeline, nullptr);
        }
        if (vulkan_.selectionPipelineLayout != VK_NULL_HANDLE)
        {
            vkDestroyPipelineLayout(vulkan_.device, vulkan_.selectionPipelineLayout, nullptr);
        }
        if (vulkan_.terrainPipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(vulkan_.device, vulkan_.terrainPipeline, nullptr);
        }
        if (vulkan_.terrainPipelineLayout != VK_NULL_HANDLE)
        {
            vkDestroyPipelineLayout(vulkan_.device, vulkan_.terrainPipelineLayout, nullptr);
        }
        if (vulkan_.pipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(vulkan_.device, vulkan_.pipeline, nullptr);
        }
        if (vulkan_.uiPipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(vulkan_.device, vulkan_.uiPipeline, nullptr);
        }
        if (vulkan_.pipelineLayout != VK_NULL_HANDLE)
        {
            vkDestroyPipelineLayout(vulkan_.device, vulkan_.pipelineLayout, nullptr);
        }
        if (vulkan_.uiPipelineLayout != VK_NULL_HANDLE)
        {
            vkDestroyPipelineLayout(vulkan_.device, vulkan_.uiPipelineLayout, nullptr);
        }
        if (vulkan_.descriptorSetLayout != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(vulkan_.device, vulkan_.descriptorSetLayout, nullptr);
        }
        if (vulkan_.terrainVertexDescriptorSetLayout != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(vulkan_.device, vulkan_.terrainVertexDescriptorSetLayout, nullptr);
        }
        if (vulkan_.renderPass != VK_NULL_HANDLE)
        {
            vkDestroyRenderPass(vulkan_.device, vulkan_.renderPass, nullptr);
        }
        if (vulkan_.sceneRenderPass != VK_NULL_HANDLE)
        {
            vkDestroyRenderPass(vulkan_.device, vulkan_.sceneRenderPass, nullptr);
        }
        if (vulkan_.device != VK_NULL_HANDLE)
        {
            vkDestroyDevice(vulkan_.device, nullptr);
        }
        if (vulkan_.surface != VK_NULL_HANDLE)
        {
            vkDestroySurfaceKHR(vulkan_.instance, vulkan_.surface, nullptr);
        }
        if (vulkan_.instance != VK_NULL_HANDLE)
        {
            vkDestroyInstance(vulkan_.instance, nullptr);
        }
    }


}
