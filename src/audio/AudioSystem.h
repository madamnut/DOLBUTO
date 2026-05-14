#pragma once

#include "camera/Camera.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <random>
#include <vector>

namespace dolbuto::audio
{
    enum class MusicScene : uint8_t
    {
        None = 0,
        Lobby = 1,
        Game = 2
    };

    class AudioSystem
    {
    public:
        void initialize(const std::filesystem::path& assetDirectory);
        void shutdown();

        void updateListener(Vec3 position, Vec3 forward, Vec3 up);
        void updateMusicPlayback(MusicScene scene, double now);

        void playButtonClick();
        void playBlockBreak(Vec3 position);
        void playBlockPlace(Vec3 position);
        void playItemPickup();

    private:
        enum class MusicTrackType : uint8_t
        {
            Ogg = 0,
            Wav = 1
        };

        struct MusicTrack
        {
            std::filesystem::path path;
            MusicTrackType type = MusicTrackType::Ogg;
        };

        void loadAssets(const std::filesystem::path& assetDirectory);
        void loadMusicAssets(const std::filesystem::path& assetDirectory);
        uint32_t loadWavSound(const std::filesystem::path& path, bool forceMono = false);
        uint32_t acquireSource();
        bool startMusicTrack(size_t trackIndex);
        bool fillMusicStreamBuffer(uint32_t buffer);
        bool updateMusicStream(double now);
        void resetMusicPlayback(MusicScene scene, double now);
        void stopMusicPlayback();
        void closeMusicStream();
        void scheduleNextMusic(double now);
        void playSfx2D(uint32_t buffer, float gain = 1.0f);
        void playSfx3D(uint32_t buffer, Vec3 position, float gain = 1.0f);

        void* device_ = nullptr;
        void* context_ = nullptr;
        bool available_ = false;
        std::array<uint32_t, 16> sources_{};
        size_t nextSource_ = 0;
        uint32_t blockBreakSound_ = 0;
        uint32_t buttonClickSound_ = 0;
        uint32_t blockPlaceSound_ = 0;
        uint32_t itemPickupSound_ = 0;
        uint32_t musicSource_ = 0;
        std::vector<MusicTrack> musicTracks_;
        std::array<uint32_t, 3> musicStreamBuffers_{};
        void* musicDecoder_ = nullptr;
        int musicStreamChannels_ = 0;
        int musicStreamSampleRate_ = 0;
        int musicStreamFormat_ = 0;
        std::vector<int16_t> musicStreamPcm_;
        bool musicStreamActive_ = false;
        bool musicStreamFinished_ = false;
        uint32_t musicLazyBuffer_ = 0;
        MusicScene activeMusicScene_ = MusicScene::None;
        double nextMusicStartTime_ = 0.0;
        size_t lastMusicTrackIndex_ = static_cast<size_t>(-1);
        std::mt19937 musicRandom_{std::random_device{}()};
    };
}
