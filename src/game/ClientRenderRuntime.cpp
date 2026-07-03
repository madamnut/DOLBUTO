#include "game/ClientRenderRuntime.h"

#include "renderer/Renderer.h"
#include "renderer/RendererFrame.h"
#include "renderer/RendererGameplayBridge.h"
#include "renderer/RendererSceneLifecycleBridge.h"
#include "renderer/RendererTerrainRuntimeBridge.h"
#include "renderer/RendererUiRuntimeBridge.h"

namespace dolbuto::game
{
    ClientRenderRuntime::ClientRenderRuntime(GLFWwindow* window, ClientRuntimeState& clientState) :
        renderer_(new Renderer(window, clientState))
    {
    }

    ClientRenderRuntime::~ClientRenderRuntime() = default;

    void ClientRenderRuntime::renderFrame(const ClientFrame& frame)
    {
        renderer_->drawFrame(RendererFrame{
            frame.camera,
            frame.cameraPosition,
            frame.fovRadians,
            frame.skyBrightness,
            frame.fpsText,
            frame.perfText,
            frame.debugTextVisible,
            frame.screenshotRequested,
            frame.showPlayer,
            frame.playerPosition,
            frame.playerYaw,
            frame.playerHeadYaw,
            frame.playerHeadPitch,
            frame.playerWalkPhase,
            frame.playerWalkAmount,
            frame.playerWalkReverse,
            frame.playerCrouching,
            frame.playerSprinting,
            frame.playerProne,
            frame.playerHurtFlash,
            frame.oxygenEffect,
            frame.showFirstPersonHand,
            frame.viewmodelMotion,
            frame.heldItemId,
            frame.offhandItemId,
            frame.heldItemStack,
            frame.offhandItemStack,
            frame.heldPortableLightEmission,
            frame.terrainWireframe,
            frame.climateOverlayMode,
            frame.menuOverlayMode,
            frame.hudVisible,
            frame.worldUpdateEnabled,
            frame.gameSceneRenderEnabled,
            frame.worldTicks,
            frame.radialMenu
        });
    }

    void ClientRenderRuntime::notifyFramebufferResized()
    {
        renderer_->setFramebufferResized();
    }

    void ClientRenderRuntime::loadGameScene(const std::filesystem::path& worldDirectory, uint64_t worldSeed)
    {
        renderer_->sceneLifecycleBridge_->loadGameScene(worldDirectory, worldSeed);
    }

    void ClientRenderRuntime::unloadGameScene()
    {
        renderer_->sceneLifecycleBridge_->unloadGameScene();
    }

    bool ClientRenderRuntime::updateBlockBreaking(DVec3 origin, Vec3 direction, bool breaking, DVec3, float deltaSeconds, bool sandboxMode)
    {
        return renderer_->gameplayBridge_->updateBlockBreaking(origin, direction, breaking, deltaSeconds, sandboxMode);
    }

    bool ClientRenderRuntime::editBlockInView(DVec3 origin, Vec3 direction, bool placeBlock, uint16_t placeBlockId, DVec3 playerPosition, double playerHeightScale)
    {
        return renderer_->gameplayBridge_->editBlockInView(origin, direction, placeBlock, placeBlockId, playerPosition, playerHeightScale);
    }

    bool ClientRenderRuntime::placeSelectedItemBlockInView(DVec3 origin, Vec3 direction, DVec3 playerPosition, double playerHeightScale)
    {
        return renderer_->gameplayBridge_->placeSelectedItemBlockInView(origin, direction, playerPosition, playerHeightScale);
    }

    bool ClientRenderRuntime::pickupDroppedItemInView(DVec3 origin, Vec3 direction)
    {
        return renderer_->gameplayBridge_->pickupDroppedItemInView(origin, direction);
    }

    bool ClientRenderRuntime::dropSelectedHotbarItem(bool wholeStack, DVec3 sourcePosition, Vec3 direction)
    {
        return renderer_->gameplayBridge_->dropSelectedHotbarItem(wholeStack, sourcePosition, direction);
    }

    gameplay::ItemInteractionMenu ClientRenderRuntime::beginItemInteractionInView(
        DVec3 origin,
        Vec3 direction,
        bool preferHeldItemBlockActions)
    {
        return renderer_->gameplayBridge_->beginItemInteractionInView(origin, direction, preferHeldItemBlockActions);
    }

    bool ClientRenderRuntime::executePendingItemInteraction(std::size_t actionIndex, std::size_t candidateIndex, bool repeat)
    {
        return renderer_->gameplayBridge_->executePendingItemInteraction(actionIndex, candidateIndex, repeat);
    }

    void ClientRenderRuntime::cancelPendingItemInteraction()
    {
        renderer_->gameplayBridge_->cancelPendingItemInteraction();
    }

    void ClientRenderRuntime::tickBlockUpdates()
    {
        renderer_->gameplayBridge_->tickBlockUpdates();
    }

    void ClientRenderRuntime::tickFluidSimulation()
    {
        renderer_->terrainRuntimeBridge_->tickFluidSimulation();
    }

    bool ClientRenderRuntime::tickHeldBurningItems(bool extinguishHeldBurnableLights)
    {
        return renderer_->gameplayBridge_->tickHeldBurningItems(extinguishHeldBurnableLights);
    }

    void ClientRenderRuntime::setInventorySnapshot(const std::array<ItemStack, gameplay::PlayerInventory::SlotCount>& slots)
    {
        renderer_->gameplayBridge_->setInventorySnapshot(slots);
    }

    void ClientRenderRuntime::setOffhandSlot(ItemStack stack)
    {
        renderer_->gameplayBridge_->setOffhandSlot(stack);
    }

    bool ClientRenderRuntime::swapSelectedHotbarWithOffhand()
    {
        return renderer_->gameplayBridge_->swapSelectedHotbarWithOffhand();
    }

    void ClientRenderRuntime::uiMouseMove(double x, double y)
    {
        renderer_->uiRuntimeBridge_->mouseMove(x, y);
    }

    void ClientRenderRuntime::uiMouseButton(int button, bool pressed, int modifiers)
    {
        renderer_->uiRuntimeBridge_->mouseButton(button, pressed, modifiers);
    }

    void ClientRenderRuntime::uiMouseWheel(double yOffset)
    {
        renderer_->uiRuntimeBridge_->mouseWheel(yOffset);
    }

    void ClientRenderRuntime::uiTextInput(unsigned int codepoint)
    {
        renderer_->uiRuntimeBridge_->textInput(codepoint);
    }

    void ClientRenderRuntime::uiKey(int key, bool pressed, int modifiers)
    {
        renderer_->uiRuntimeBridge_->key(key, pressed, modifiers);
    }

    void ClientRenderRuntime::closeInventoryInteraction()
    {
        renderer_->uiRuntimeBridge_->closeInventoryInteraction();
    }
}
