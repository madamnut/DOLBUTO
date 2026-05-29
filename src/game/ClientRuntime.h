#pragma once

#include "game/ClientFrame.h"
#include "game/ClientUiTypes.h"
#include "gameplay/ClientGameplayRuntime.h"
#include "gameplay/PlayerInventory.h"
#include "items/ItemData.h"
#include "world/TerrainBuilder.h"
#include "world/WorldTypes.h"

#include <array>
#include <cstddef>
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
    enum class ClientPerfCounter;

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

            bool playerColliderIntersectsTerrain(DVec3 playerPosition, double heightScale) const;
            bool playerColliderHasSupportBelow(DVec3 playerPosition) const;
            bool playerColliderIntersectsWater(DVec3 playerPosition, double heightScale) const;
            void updateBlockSelection(DVec3 origin, Vec3 direction);
            void updateBlockBreaking(DVec3 origin, Vec3 direction, bool breaking, DVec3 playerPosition, float deltaSeconds, bool sandboxMode);
            bool editBlockInView(DVec3 origin, Vec3 direction, bool placeBlock, uint16_t placeBlockId, DVec3 playerPosition, double playerHeightScale);
            bool placeSelectedItemBlockInView(DVec3 origin, Vec3 direction, DVec3 playerPosition, double playerHeightScale);
            bool pickupDroppedItemInView(DVec3 origin, Vec3 direction);
            bool dropSelectedHotbarItem(bool wholeStack, DVec3 playerPosition, Vec3 direction);
            gameplay::ItemInteractionMenu beginItemInteractionInView(DVec3 origin, Vec3 direction);
            bool executePendingItemInteraction(std::size_t actionIndex, std::size_t candidateIndex, bool repeat);
            void cancelPendingItemInteraction();
            void tickBlockUpdates();
            void tickFluidSimulation();
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
            void setOptionsFov(int fovDegrees);
            void setOptionsViewBobbing(bool enabled);
            void setOptionsControls(bool toggleSprint, bool toggleSneak, bool toggleProne);
            void setOptionsLobbyBackground(bool lobbyBackground);
            void setWorldCreateGameMode(bool sandbox);
            void setPlayerStats(int hp, int maxHp, int hunger, int maxHunger, int thirst, int maxThirst);
            void setRadialMenu(
                const std::vector<gameplay::ItemInteractionActionMenu>& actions,
                std::optional<std::size_t> selectedActionIndex,
                std::optional<std::size_t> selectedCandidateIndex);
            void hideRadialMenu();
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
            std::string performanceMaxText() const;
            void recordPerformanceMax(ClientPerfCounter counter, double milliseconds);
            void resetPerformanceMax();

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
        world::TerrainBuilderConfig terrainConfigForWorldSeed(uint64_t worldSeed) const;

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
