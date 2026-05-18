#pragma once

#include "game/ClientFrame.h"
#include "game/ClientUiTypes.h"
#include "gameplay/PlayerInventory.h"
#include "items/ItemData.h"
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

namespace dolbuto::game
{
    class ClientRenderRuntime;
    struct ClientRuntimeState;

    class ClientRuntime
    {
    public:
        class RenderAccess
        {
        public:
            explicit RenderAccess(ClientRuntime& owner);

            void frame(const ClientFrame& frame);
            void notifyFramebufferResized();

        private:
            ClientRuntime& owner_;
        };

        class SceneAccess
        {
        public:
            explicit SceneAccess(ClientRuntime& owner);

            void loadGameScene(const std::filesystem::path& worldDirectory, uint64_t worldSeed);
            void unloadGameScene();

        private:
            ClientRuntime& owner_;
        };

        class GameplayAccess
        {
        public:
            explicit GameplayAccess(ClientRuntime& owner);

            bool playerColliderIntersectsTerrain(DVec3 playerPosition) const;
            void updateBlockSelection(DVec3 origin, Vec3 direction);
            void updateBlockBreaking(DVec3 origin, Vec3 direction, bool breaking, DVec3 playerPosition, float deltaSeconds);
            bool editBlockInView(DVec3 origin, Vec3 direction, bool placeRock, DVec3 playerPosition);
            bool pickupDroppedItemInView(DVec3 origin, Vec3 direction);
            bool dropSelectedHotbarItem(bool wholeStack, DVec3 playerPosition, Vec3 direction);
            std::array<ItemStack, gameplay::PlayerInventory::SlotCount> inventorySnapshot() const;
            void setInventorySnapshot(const std::array<ItemStack, gameplay::PlayerInventory::SlotCount>& slots);

        private:
            ClientRuntime& owner_;
        };

        class UiAccess
        {
        public:
            explicit UiAccess(ClientRuntime& owner);

            void setWorldList(const std::vector<WorldListItem>& worlds);
            void setHotbarSelectedSlot(int slot);
            std::string inputValue(std::string_view id) const;
            std::string chatInputValue() const;
            void setChatVisible(bool inputVisible, bool hasMessages);
            void setChatMessages(std::string_view rml);
            void clearChatInput();
            void focusChatInput();
            void setOptionsVolumes(int bgmPercent, int sfxPercent);
            void setOptionsLobbyBackground(bool lobbyBackground);
            void mouseMove(double x, double y);
            void mouseButton(int button, bool pressed, int modifiers);
            void mouseWheel(double yOffset);
            void textInput(unsigned int codepoint);
            void key(int key, bool pressed, int modifiers);
            void closeInventoryInteraction();
            bool available() const;
            std::optional<std::string> consumeAction();

        private:
            ClientRuntime& owner_;
        };

        class DiagnosticsAccess
        {
        public:
            explicit DiagnosticsAccess(ClientRuntime& owner);

            std::string selectedBlockText() const;
            std::string climateText(DVec3 position) const;
            std::string biomeText(DVec3 position) const;
            std::string terrainText(DVec3 position) const;

        private:
            ClientRuntime& owner_;
        };

        class AudioAccess
        {
        public:
            explicit AudioAccess(ClientRuntime& owner);

            void setVolumes(float musicVolume, float sfxVolume);

        private:
            ClientRuntime& owner_;
        };

        explicit ClientRuntime(GLFWwindow* window);
        ~ClientRuntime();

        ClientRuntime(const ClientRuntime&) = delete;
        ClientRuntime& operator=(const ClientRuntime&) = delete;

        RenderAccess& render();
        SceneAccess& scene();
        GameplayAccess& gameplay();
        UiAccess& ui();
        DiagnosticsAccess& diagnostics();
        AudioAccess& audio();

        const RenderAccess& render() const;
        const SceneAccess& scene() const;
        const GameplayAccess& gameplay() const;
        const UiAccess& ui() const;
        const DiagnosticsAccess& diagnostics() const;
        const AudioAccess& audio() const;

    private:
        std::unique_ptr<ClientRuntimeState> state_;
        std::unique_ptr<ClientRenderRuntime> renderRuntime_;
        RenderAccess renderAccess_;
        SceneAccess sceneAccess_;
        GameplayAccess gameplayAccess_;
        UiAccess uiAccess_;
        DiagnosticsAccess diagnosticsAccess_;
        AudioAccess audioAccess_;
    };
}
