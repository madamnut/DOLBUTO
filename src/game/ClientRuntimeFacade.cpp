#include "game/ClientRuntimeFacade.h"

#include "renderer/Renderer.h"

namespace dolbuto::game
{
    ClientRuntimeFacade::ClientRuntimeFacade(GLFWwindow* window) :
        renderer_(new Renderer(window))
    {
    }

    ClientRuntimeFacade::~ClientRuntimeFacade() = default;

    void ClientRuntimeFacade::drawFrame(const RendererFrame& frame)
    {
        renderer_->drawFrame(frame);
    }

    void ClientRuntimeFacade::setFramebufferResized()
    {
        renderer_->setFramebufferResized();
    }

    void ClientRuntimeFacade::loadGameScene(const std::filesystem::path& worldDirectory, uint64_t worldSeed)
    {
        renderer_->loadGameScene(worldDirectory, worldSeed);
    }

    void ClientRuntimeFacade::unloadGameScene()
    {
        renderer_->unloadGameScene();
    }

    bool ClientRuntimeFacade::playerColliderIntersectsTerrain(DVec3 playerPosition) const
    {
        return renderer_->playerColliderIntersectsTerrain(playerPosition);
    }

    void ClientRuntimeFacade::updateBlockSelection(DVec3 origin, Vec3 direction)
    {
        renderer_->updateBlockSelection(origin, direction);
    }

    void ClientRuntimeFacade::updateBlockBreaking(DVec3 origin, Vec3 direction, bool breaking, DVec3 playerPosition, float deltaSeconds)
    {
        renderer_->updateBlockBreaking(origin, direction, breaking, playerPosition, deltaSeconds);
    }

    bool ClientRuntimeFacade::editBlockInView(DVec3 origin, Vec3 direction, bool placeRock, DVec3 playerPosition)
    {
        return renderer_->editBlockInView(origin, direction, placeRock, playerPosition);
    }

    bool ClientRuntimeFacade::pickupDroppedItemInView(DVec3 origin, Vec3 direction)
    {
        return renderer_->pickupDroppedItemInView(origin, direction);
    }

    bool ClientRuntimeFacade::dropSelectedHotbarItem(bool wholeStack, DVec3 playerPosition, Vec3 direction)
    {
        return renderer_->dropSelectedHotbarItem(wholeStack, playerPosition, direction);
    }

    std::string ClientRuntimeFacade::selectedBlockText() const
    {
        return renderer_->selectedBlockText();
    }

    std::string ClientRuntimeFacade::climateText(DVec3 position) const
    {
        return renderer_->climateText(position);
    }

    void ClientRuntimeFacade::setWorldList(const std::vector<WorldListItem>& worlds)
    {
        renderer_->setWorldList(worlds);
    }

    void ClientRuntimeFacade::setHotbarSelectedSlot(int slot)
    {
        renderer_->setHotbarSelectedSlot(slot);
    }

    std::array<ItemStack, gameplay::PlayerInventory::SlotCount> ClientRuntimeFacade::inventorySnapshot() const
    {
        return renderer_->inventorySnapshot();
    }

    void ClientRuntimeFacade::setInventorySnapshot(const std::array<ItemStack, gameplay::PlayerInventory::SlotCount>& slots)
    {
        renderer_->setInventorySnapshot(slots);
    }

    std::string ClientRuntimeFacade::uiInputValue(std::string_view id) const
    {
        return renderer_->uiInputValue(id);
    }

    void ClientRuntimeFacade::uiMouseMove(double x, double y)
    {
        renderer_->uiMouseMove(x, y);
    }

    void ClientRuntimeFacade::uiMouseButton(int button, bool pressed, int modifiers)
    {
        renderer_->uiMouseButton(button, pressed, modifiers);
    }

    void ClientRuntimeFacade::uiMouseWheel(double yOffset)
    {
        renderer_->uiMouseWheel(yOffset);
    }

    void ClientRuntimeFacade::uiTextInput(unsigned int codepoint)
    {
        renderer_->uiTextInput(codepoint);
    }

    void ClientRuntimeFacade::uiKey(int key, bool pressed, int modifiers)
    {
        renderer_->uiKey(key, pressed, modifiers);
    }

    void ClientRuntimeFacade::closeInventoryInteraction()
    {
        renderer_->closeInventoryInteraction();
    }

    bool ClientRuntimeFacade::rmlUiAvailable() const
    {
        return renderer_->rmlUiAvailable();
    }

    std::optional<std::string> ClientRuntimeFacade::consumeUiAction()
    {
        return renderer_->consumeUiAction();
    }
}
