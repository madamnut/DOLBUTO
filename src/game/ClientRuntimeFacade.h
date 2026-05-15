#pragma once

#include "game/ClientUiTypes.h"
#include "gameplay/PlayerInventory.h"
#include "items/ItemData.h"
#include "renderer/RendererFrame.h"
#include "world/WorldTypes.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct GLFWwindow;

namespace dolbuto
{
    class Renderer;

    namespace game
    {
        class ClientRuntimeFacade
        {
        public:
            explicit ClientRuntimeFacade(GLFWwindow* window);
            ~ClientRuntimeFacade();

            ClientRuntimeFacade(const ClientRuntimeFacade&) = delete;
            ClientRuntimeFacade& operator=(const ClientRuntimeFacade&) = delete;

            void drawFrame(const RendererFrame& frame);
            void setFramebufferResized();

            void loadGameScene(const std::filesystem::path& worldDirectory, uint64_t worldSeed);
            void unloadGameScene();

            bool playerColliderIntersectsTerrain(DVec3 playerPosition) const;
            void updateBlockSelection(DVec3 origin, Vec3 direction);
            void updateBlockBreaking(DVec3 origin, Vec3 direction, bool breaking, DVec3 playerPosition, float deltaSeconds);
            bool editBlockInView(DVec3 origin, Vec3 direction, bool placeRock, DVec3 playerPosition);
            bool pickupDroppedItemInView(DVec3 origin, Vec3 direction);
            bool dropSelectedHotbarItem(bool wholeStack, DVec3 playerPosition, Vec3 direction);
            std::string selectedBlockText() const;
            std::string climateText(DVec3 position) const;

            void setWorldList(const std::vector<WorldListItem>& worlds);
            void setHotbarSelectedSlot(int slot);
            std::array<ItemStack, gameplay::PlayerInventory::SlotCount> inventorySnapshot() const;
            void setInventorySnapshot(const std::array<ItemStack, gameplay::PlayerInventory::SlotCount>& slots);
            std::string uiInputValue(std::string_view id) const;
            void uiMouseMove(double x, double y);
            void uiMouseButton(int button, bool pressed, int modifiers);
            void uiMouseWheel(double yOffset);
            void uiTextInput(unsigned int codepoint);
            void uiKey(int key, bool pressed, int modifiers);
            void closeInventoryInteraction();
            bool rmlUiAvailable() const;
            std::optional<std::string> consumeUiAction();

        private:
            std::unique_ptr<Renderer> renderer_;
        };
    }
}
