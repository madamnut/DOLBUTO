#pragma once

#include "camera/Camera.h"

#include <filesystem>

namespace dolbuto
{
    namespace audio
    {
        class AudioSystem;
    }

    class RendererAudioBridge
    {
    public:
        RendererAudioBridge(audio::AudioSystem& audio, const std::filesystem::path& assetDirectory);

        void shutdown();
        void updateListener(const Camera& camera, Vec3 cameraPosition);
        void updateMusicPlayback(int menuOverlayMode, bool gameSceneRenderEnabled);
        void playButtonClick();
        void playBlockBreak(int x, int y, int z);
        void playBlockPlace(int x, int y, int z);
        void playItemPickup();

    private:
        audio::AudioSystem& audio_;
    };
}
