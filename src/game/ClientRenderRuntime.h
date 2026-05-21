#pragma once

#include "game/ClientFrame.h"
#include "gameplay/PlayerInventory.h"
#include "items/ItemData.h"
#include "world/WorldTypes.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>

struct GLFWwindow;

namespace dolbuto
{
    class Renderer;

    namespace game
    {
        struct ClientRuntimeState;

        class ClientRenderRuntime
        {
        public:
            ClientRenderRuntime(GLFWwindow* window, ClientRuntimeState& clientState);
            ~ClientRenderRuntime();

            ClientRenderRuntime(const ClientRenderRuntime&) = delete;
            ClientRenderRuntime& operator=(const ClientRenderRuntime&) = delete;

            void renderFrame(const ClientFrame& frame);
            void notifyFramebufferResized();

            void loadGameScene(const std::filesystem::path& worldDirectory, uint64_t worldSeed);
            void unloadGameScene();

            void updateBlockBreaking(DVec3 origin, Vec3 direction, bool breaking, DVec3 playerPosition, float deltaSeconds);
            bool editBlockInView(DVec3 origin, Vec3 direction, bool placeBlock, uint16_t placeBlockId, DVec3 playerPosition, double playerHeightScale);
            bool pickupDroppedItemInView(DVec3 origin, Vec3 direction);
            bool dropSelectedHotbarItem(bool wholeStack, DVec3 playerPosition, Vec3 direction);

            void setInventorySnapshot(const std::array<ItemStack, gameplay::PlayerInventory::SlotCount>& slots);
            void uiMouseMove(double x, double y);
            void uiMouseButton(int button, bool pressed, int modifiers);
            void uiMouseWheel(double yOffset);
            void uiTextInput(unsigned int codepoint);
            void uiKey(int key, bool pressed, int modifiers);
            void closeInventoryInteraction();

        private:
            std::unique_ptr<Renderer> renderer_;
        };
    }
}
