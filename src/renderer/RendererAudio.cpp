#include "renderer/RendererAudioBridge.h"

#include "audio/AudioSystem.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

namespace dolbuto
{
    RendererAudioBridge::RendererAudioBridge(audio::AudioSystem& audio, const std::filesystem::path& assetDirectory) :
        audio_(audio)
    {
        audio_.initialize(assetDirectory);
    }

    void RendererAudioBridge::shutdown()
    {
        audio_.shutdown();
    }

    void RendererAudioBridge::updateListener(const Camera& camera, Vec3 cameraPosition)
    {
        audio_.updateListener(cameraPosition, camera.forward(), camera.up());
    }

    void RendererAudioBridge::updateMusicPlayback(int menuOverlayMode, bool gameSceneRenderEnabled)
    {
        audio::MusicScene scene = audio::MusicScene::None;
        if (gameSceneRenderEnabled)
        {
            scene = audio::MusicScene::Game;
        }
        else if (menuOverlayMode == 1 || menuOverlayMode == 3 || menuOverlayMode == 4)
        {
            scene = audio::MusicScene::Lobby;
        }
        audio_.updateMusicPlayback(scene, glfwGetTime());
    }

    void RendererAudioBridge::playButtonClick()
    {
        audio_.playButtonClick();
    }

    void RendererAudioBridge::playBlockBreak(int x, int y, int z)
    {
        audio_.playBlockBreak(Vec3{
            static_cast<float>(x),
            static_cast<float>(y) + 0.5f,
            static_cast<float>(z)
        });
    }

    void RendererAudioBridge::playBlockPlace(int x, int y, int z)
    {
        audio_.playBlockPlace(Vec3{
            static_cast<float>(x),
            static_cast<float>(y) + 0.5f,
            static_cast<float>(z)
        });
    }

    void RendererAudioBridge::playItemPickup()
    {
        audio_.playItemPickup();
    }
}
