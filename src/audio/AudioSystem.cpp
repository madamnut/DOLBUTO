#include "audio/AudioSystem.h"

#include "platform/Log.h"

#include <AL/al.h>
#include <AL/alc.h>

#define STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>
#undef STB_VORBIS_HEADER_ONLY

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace dolbuto::audio
{
    namespace
    {
        constexpr double MusicMinDelaySeconds = 10.0;
        constexpr double MusicMaxDelaySeconds = 60.0;
        constexpr float MusicStreamBufferSeconds = 2.0f;

        struct WavSoundData
        {
            std::vector<char> pcm;
            ALenum format = 0;
            ALsizei sampleRate = 0;
            uint16_t channels = 0;
        };

        std::vector<char> readFile(const std::string& path)
        {
            std::ifstream file(path, std::ios::ate | std::ios::binary);
            if (!file.is_open())
            {
                throw std::runtime_error("Failed to open file: " + path);
            }

            const auto size = static_cast<size_t>(file.tellg());
            std::vector<char> buffer(size);
            file.seekg(0);
            file.read(buffer.data(), static_cast<std::streamsize>(size));
            return buffer;
        }

        uint16_t readLe16(const std::vector<char>& data, size_t offset)
        {
            if (offset + 2u > data.size())
            {
                throw std::runtime_error("WAV read overflow.");
            }
            return static_cast<uint16_t>(
                static_cast<uint8_t>(data[offset]) |
                (static_cast<uint16_t>(static_cast<uint8_t>(data[offset + 1u])) << 8u));
        }

        uint32_t readLe32(const std::vector<char>& data, size_t offset)
        {
            if (offset + 4u > data.size())
            {
                throw std::runtime_error("WAV read overflow.");
            }
            return static_cast<uint32_t>(static_cast<uint8_t>(data[offset])) |
                (static_cast<uint32_t>(static_cast<uint8_t>(data[offset + 1u])) << 8u) |
                (static_cast<uint32_t>(static_cast<uint8_t>(data[offset + 2u])) << 16u) |
                (static_cast<uint32_t>(static_cast<uint8_t>(data[offset + 3u])) << 24u);
        }

        bool chunkIdEquals(const std::vector<char>& data, size_t offset, std::string_view id)
        {
            return offset + id.size() <= data.size() &&
                std::equal(id.begin(), id.end(), data.begin() + static_cast<std::ptrdiff_t>(offset));
        }

        WavSoundData decodeWavPcm16(const std::filesystem::path& path)
        {
            const std::vector<char> data = readFile(path.string());
            if (data.size() < 12u ||
                !chunkIdEquals(data, 0, "RIFF") ||
                !chunkIdEquals(data, 8, "WAVE"))
            {
                throw std::runtime_error("Invalid WAV file: " + path.string());
            }

            uint16_t audioFormat = 0;
            uint16_t channels = 0;
            uint32_t sampleRate = 0;
            uint16_t bitsPerSample = 0;
            size_t pcmOffset = 0;
            size_t pcmSize = 0;

            size_t offset = 12u;
            while (offset + 8u <= data.size())
            {
                const size_t chunkDataOffset = offset + 8u;
                const uint32_t chunkSize = readLe32(data, offset + 4u);
                if (chunkDataOffset + chunkSize > data.size())
                {
                    throw std::runtime_error("Invalid WAV chunk size: " + path.string());
                }

                if (chunkIdEquals(data, offset, "fmt "))
                {
                    if (chunkSize < 16u)
                    {
                        throw std::runtime_error("Invalid WAV fmt chunk: " + path.string());
                    }
                    audioFormat = readLe16(data, chunkDataOffset + 0u);
                    channels = readLe16(data, chunkDataOffset + 2u);
                    sampleRate = readLe32(data, chunkDataOffset + 4u);
                    bitsPerSample = readLe16(data, chunkDataOffset + 14u);
                }
                else if (chunkIdEquals(data, offset, "data"))
                {
                    pcmOffset = chunkDataOffset;
                    pcmSize = chunkSize;
                }

                offset = chunkDataOffset + chunkSize + (chunkSize & 1u);
            }

            if (audioFormat != 1u || bitsPerSample != 16u || pcmOffset == 0u || pcmSize == 0u)
            {
                throw std::runtime_error("Only PCM 16-bit WAV files are supported: " + path.string());
            }

            WavSoundData sound{};
            if (channels == 1u)
            {
                sound.format = AL_FORMAT_MONO16;
            }
            else if (channels == 2u)
            {
                sound.format = AL_FORMAT_STEREO16;
            }
            else
            {
                throw std::runtime_error("Unsupported WAV channel count: " + path.string());
            }

            sound.sampleRate = static_cast<ALsizei>(sampleRate);
            sound.channels = channels;
            sound.pcm.assign(data.begin() + static_cast<std::ptrdiff_t>(pcmOffset), data.begin() + static_cast<std::ptrdiff_t>(pcmOffset + pcmSize));
            return sound;
        }
    }

    void AudioSystem::initialize(const std::filesystem::path& assetDirectory)
    {
        auto* device = alcOpenDevice(nullptr);
        if (device == nullptr)
        {
            log::warn("OpenAL device open failed.");
            return;
        }

        auto* context = alcCreateContext(device, nullptr);
        if (context == nullptr)
        {
            log::warn("OpenAL context creation failed.");
            alcCloseDevice(device);
            return;
        }

        if (alcMakeContextCurrent(context) == ALC_FALSE)
        {
            log::warn("OpenAL context activation failed.");
            alcDestroyContext(context);
            alcCloseDevice(device);
            return;
        }

        device_ = device;
        context_ = context;
        available_ = true;

        alGenSources(static_cast<ALsizei>(sources_.size()), reinterpret_cast<ALuint*>(sources_.data()));
        if (alGetError() != AL_NO_ERROR)
        {
            log::warn("OpenAL source creation failed.");
            shutdown();
            return;
        }

        ALuint musicSource = 0;
        alGenSources(1, &musicSource);
        if (alGetError() != AL_NO_ERROR || musicSource == 0)
        {
            log::warn("OpenAL music source creation failed.");
            shutdown();
            return;
        }
        musicSource_ = static_cast<uint32_t>(musicSource);

        loadAssets(assetDirectory);
    }

    void AudioSystem::shutdown()
    {
        if (!available_)
        {
            return;
        }

        alSourceStopv(static_cast<ALsizei>(sources_.size()), reinterpret_cast<const ALuint*>(sources_.data()));
        alDeleteSources(static_cast<ALsizei>(sources_.size()), reinterpret_cast<const ALuint*>(sources_.data()));
        sources_.fill(0);
        if (musicSource_ != 0)
        {
            closeMusicStream();
            const ALuint source = static_cast<ALuint>(musicSource_);
            alSourceStop(source);
            alSourcei(source, AL_BUFFER, 0);
            alDeleteSources(1, &source);
            musicSource_ = 0;
        }
        if (blockBreakSound_ != 0)
        {
            const ALuint buffer = static_cast<ALuint>(blockBreakSound_);
            alDeleteBuffers(1, &buffer);
            blockBreakSound_ = 0;
        }
        if (buttonClickSound_ != 0)
        {
            const ALuint buffer = static_cast<ALuint>(buttonClickSound_);
            alDeleteBuffers(1, &buffer);
            buttonClickSound_ = 0;
        }
        if (blockPlaceSound_ != 0)
        {
            const ALuint buffer = static_cast<ALuint>(blockPlaceSound_);
            alDeleteBuffers(1, &buffer);
            blockPlaceSound_ = 0;
        }
        if (itemPickupSound_ != 0)
        {
            const ALuint buffer = static_cast<ALuint>(itemPickupSound_);
            alDeleteBuffers(1, &buffer);
            itemPickupSound_ = 0;
        }
        musicTracks_.clear();

        alcMakeContextCurrent(nullptr);
        if (context_ != nullptr)
        {
            alcDestroyContext(static_cast<ALCcontext*>(context_));
            context_ = nullptr;
        }
        if (device_ != nullptr)
        {
            alcCloseDevice(static_cast<ALCdevice*>(device_));
            device_ = nullptr;
        }
        available_ = false;
        nextSource_ = 0;
        activeMusicScene_ = MusicScene::None;
        nextMusicStartTime_ = 0.0;
        lastMusicTrackIndex_ = static_cast<size_t>(-1);
    }

    void AudioSystem::loadAssets(const std::filesystem::path& assetDirectory)
    {
        const std::filesystem::path sfxDir = assetDirectory / "audio" / "sfx";
        blockBreakSound_ = loadWavSound(sfxDir / "Break.wav", true);
        buttonClickSound_ = loadWavSound(sfxDir / "Button_Click.wav");
        blockPlaceSound_ = loadWavSound(sfxDir / "Place.wav", true);
        itemPickupSound_ = loadWavSound(sfxDir / "Pop.wav");
        loadMusicAssets(assetDirectory);
    }

    void AudioSystem::loadMusicAssets(const std::filesystem::path& assetDirectory)
    {
        const std::filesystem::path musicDir = assetDirectory / "audio" / "music";
        try
        {
            if (!std::filesystem::exists(musicDir))
            {
                return;
            }

            for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(musicDir))
            {
                if (!entry.is_regular_file())
                {
                    continue;
                }

                std::string extension = entry.path().extension().string();
                std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char value)
                {
                    return static_cast<char>(std::tolower(value));
                });

                if (extension == ".ogg")
                {
                    musicTracks_.push_back({entry.path(), MusicTrackType::Ogg});
                }
                else if (extension == ".wav")
                {
                    musicTracks_.push_back({entry.path(), MusicTrackType::Wav});
                }
            }

            std::sort(musicTracks_.begin(), musicTracks_.end(), [](const MusicTrack& left, const MusicTrack& right)
            {
                return left.path.string() < right.path.string();
            });

            if (musicTracks_.empty())
            {
                log::warn("No playable music files found: " + musicDir.string());
            }
        }
        catch (const std::exception& error)
        {
            log::warn(std::string("Music directory scan failed: ") + error.what());
        }
    }

    uint32_t AudioSystem::loadWavSound(const std::filesystem::path& path, bool forceMono)
    {
        if (!available_)
        {
            return 0;
        }

        try
        {
            WavSoundData sound = decodeWavPcm16(path);
            if (forceMono && sound.channels == 2u)
            {
                std::vector<char> mono;
                mono.resize(sound.pcm.size() / 2u);
                const size_t stereoSampleCount = sound.pcm.size() / sizeof(int16_t);
                for (size_t stereoIndex = 0, monoIndex = 0; stereoIndex + 1u < stereoSampleCount; stereoIndex += 2u, ++monoIndex)
                {
                    int16_t left = 0;
                    int16_t right = 0;
                    std::memcpy(&left, sound.pcm.data() + stereoIndex * sizeof(int16_t), sizeof(left));
                    std::memcpy(&right, sound.pcm.data() + (stereoIndex + 1u) * sizeof(int16_t), sizeof(right));
                    const int16_t mixed = static_cast<int16_t>((static_cast<int>(left) + static_cast<int>(right)) / 2);
                    std::memcpy(mono.data() + monoIndex * sizeof(int16_t), &mixed, sizeof(mixed));
                }
                sound.pcm = std::move(mono);
                sound.channels = 1u;
                sound.format = AL_FORMAT_MONO16;
            }
            ALuint buffer = 0;
            alGenBuffers(1, &buffer);
            alBufferData(buffer, sound.format, sound.pcm.data(), static_cast<ALsizei>(sound.pcm.size()), sound.sampleRate);
            if (alGetError() != AL_NO_ERROR)
            {
                if (buffer != 0)
                {
                    alDeleteBuffers(1, &buffer);
                }
                log::warn("OpenAL buffer upload failed: " + path.string());
                return 0;
            }
            return static_cast<uint32_t>(buffer);
        }
        catch (const std::exception& error)
        {
            log::warn(error.what());
            return 0;
        }
    }

    uint32_t AudioSystem::acquireSource()
    {
        if (!available_ || sources_.empty())
        {
            return 0;
        }

        const uint32_t source = sources_[nextSource_ % sources_.size()];
        nextSource_ = (nextSource_ + 1u) % sources_.size();
        alSourceStop(static_cast<ALuint>(source));
        alSourcei(static_cast<ALuint>(source), AL_BUFFER, 0);
        return source;
    }

    void AudioSystem::updateListener(Vec3 position, Vec3 forward, Vec3 up)
    {
        if (!available_)
        {
            return;
        }

        const std::array<ALfloat, 6> orientation = {
            forward.x, forward.y, forward.z,
            up.x, up.y, up.z
        };
        alListener3f(AL_POSITION, position.x, position.y, position.z);
        alListener3f(AL_VELOCITY, 0.0f, 0.0f, 0.0f);
        alListenerfv(AL_ORIENTATION, orientation.data());
    }

    void AudioSystem::updateMusicPlayback(MusicScene scene, double now)
    {
        if (!available_ || musicSource_ == 0 || musicTracks_.empty())
        {
            return;
        }

        if (scene != activeMusicScene_)
        {
            resetMusicPlayback(scene, now);
        }
        if (scene == MusicScene::None)
        {
            return;
        }

        if (musicStreamActive_)
        {
            updateMusicStream(now);
            return;
        }

        ALint state = AL_STOPPED;
        alGetSourcei(static_cast<ALuint>(musicSource_), AL_SOURCE_STATE, &state);
        if (musicLazyBuffer_ != 0)
        {
            if (state == AL_PLAYING || state == AL_PAUSED)
            {
                return;
            }
            closeMusicStream();
            scheduleNextMusic(now);
            return;
        }

        if (state == AL_PLAYING || state == AL_PAUSED)
        {
            return;
        }

        if (nextMusicStartTime_ <= 0.0)
        {
            scheduleNextMusic(now);
            return;
        }
        if (now < nextMusicStartTime_)
        {
            return;
        }

        std::uniform_int_distribution<size_t> trackDistribution(0, musicTracks_.size() - 1u);
        size_t trackIndex = trackDistribution(musicRandom_);
        if (musicTracks_.size() > 1u && trackIndex == lastMusicTrackIndex_)
        {
            trackIndex = (trackIndex + 1u) % musicTracks_.size();
        }
        if (!startMusicTrack(trackIndex))
        {
            scheduleNextMusic(now);
        }
    }

    void AudioSystem::setMusicVolume(float volume)
    {
        musicVolume_ = std::clamp(volume, 0.0f, 1.0f);
        if (available_ && musicSource_ != 0)
        {
            alSourcef(static_cast<ALuint>(musicSource_), AL_GAIN, musicVolume_);
        }
    }

    void AudioSystem::setSfxVolume(float volume)
    {
        sfxVolume_ = std::clamp(volume, 0.0f, 1.0f);
    }

    bool AudioSystem::startMusicTrack(size_t trackIndex)
    {
        if (!available_ || musicSource_ == 0 || trackIndex >= musicTracks_.size())
        {
            return false;
        }

        closeMusicStream();
        const MusicTrack& track = musicTracks_[trackIndex];
        if (track.type == MusicTrackType::Wav)
        {
            const uint32_t buffer = loadWavSound(track.path);
            if (buffer == 0)
            {
                return false;
            }

            musicLazyBuffer_ = buffer;
            lastMusicTrackIndex_ = trackIndex;
            const ALuint source = static_cast<ALuint>(musicSource_);
            alSourcei(source, AL_BUFFER, static_cast<ALint>(musicLazyBuffer_));
            alSourcei(source, AL_SOURCE_RELATIVE, AL_TRUE);
            alSource3f(source, AL_POSITION, 0.0f, 0.0f, 0.0f);
            alSourcef(source, AL_GAIN, musicVolume_);
            alSourcePlay(source);
            if (alGetError() != AL_NO_ERROR)
            {
                closeMusicStream();
                log::warn("OpenAL music WAV playback failed: " + track.path.string());
                return false;
            }
            nextMusicStartTime_ = 0.0;
            return true;
        }

        int error = 0;
        const std::string pathString = track.path.string();
        stb_vorbis* decoder = stb_vorbis_open_filename(pathString.c_str(), &error, nullptr);
        if (decoder == nullptr)
        {
            log::warn("OGG stream open failed: " + pathString);
            return false;
        }

        const stb_vorbis_info info = stb_vorbis_get_info(decoder);
        if (info.channels == 1)
        {
            musicStreamFormat_ = AL_FORMAT_MONO16;
        }
        else if (info.channels == 2)
        {
            musicStreamFormat_ = AL_FORMAT_STEREO16;
        }
        else
        {
            stb_vorbis_close(decoder);
            log::warn("Unsupported OGG channel count: " + pathString);
            return false;
        }

        musicDecoder_ = decoder;
        musicStreamChannels_ = info.channels;
        musicStreamSampleRate_ = static_cast<int>(info.sample_rate);
        const size_t framesPerBuffer = std::max<size_t>(1u, static_cast<size_t>(static_cast<float>(musicStreamSampleRate_) * MusicStreamBufferSeconds));
        musicStreamPcm_.assign(framesPerBuffer * static_cast<size_t>(musicStreamChannels_), 0);

        ALuint buffers[3]{};
        alGenBuffers(static_cast<ALsizei>(musicStreamBuffers_.size()), buffers);
        if (alGetError() != AL_NO_ERROR)
        {
            closeMusicStream();
            log::warn("OpenAL music stream buffer creation failed.");
            return false;
        }
        for (size_t index = 0; index < musicStreamBuffers_.size(); ++index)
        {
            musicStreamBuffers_[index] = static_cast<uint32_t>(buffers[index]);
        }

        std::array<ALuint, 3> queuedBuffers{};
        ALsizei queuedCount = 0;
        for (uint32_t buffer : musicStreamBuffers_)
        {
            if (!fillMusicStreamBuffer(buffer))
            {
                break;
            }
            queuedBuffers[static_cast<size_t>(queuedCount)] = static_cast<ALuint>(buffer);
            ++queuedCount;
        }
        if (queuedCount == 0)
        {
            closeMusicStream();
            log::warn("OGG stream has no playable samples: " + pathString);
            return false;
        }

        const ALuint source = static_cast<ALuint>(musicSource_);
        alSourcei(source, AL_SOURCE_RELATIVE, AL_TRUE);
        alSource3f(source, AL_POSITION, 0.0f, 0.0f, 0.0f);
        alSourcef(source, AL_GAIN, musicVolume_);
        alSourceQueueBuffers(source, queuedCount, queuedBuffers.data());
        alSourcePlay(source);
        if (alGetError() != AL_NO_ERROR)
        {
            closeMusicStream();
            log::warn("OpenAL music stream playback failed: " + pathString);
            return false;
        }

        musicStreamActive_ = true;
        musicStreamFinished_ = queuedCount < static_cast<ALsizei>(musicStreamBuffers_.size());
        lastMusicTrackIndex_ = trackIndex;
        nextMusicStartTime_ = 0.0;
        return true;
    }

    bool AudioSystem::fillMusicStreamBuffer(uint32_t buffer)
    {
        if (musicDecoder_ == nullptr || buffer == 0 || musicStreamChannels_ <= 0 || musicStreamSampleRate_ <= 0 || musicStreamPcm_.empty())
        {
            return false;
        }

        const int sampleCapacity = static_cast<int>(std::min<size_t>(musicStreamPcm_.size(), static_cast<size_t>(std::numeric_limits<int>::max())));
        const int frames = stb_vorbis_get_samples_short_interleaved(
            static_cast<stb_vorbis*>(musicDecoder_),
            musicStreamChannels_,
            musicStreamPcm_.data(),
            sampleCapacity);
        if (frames <= 0)
        {
            return false;
        }

        const size_t pcmBytes = static_cast<size_t>(frames) * static_cast<size_t>(musicStreamChannels_) * sizeof(int16_t);
        if (pcmBytes > static_cast<size_t>(std::numeric_limits<ALsizei>::max()))
        {
            return false;
        }

        alBufferData(
            static_cast<ALuint>(buffer),
            static_cast<ALenum>(musicStreamFormat_),
            musicStreamPcm_.data(),
            static_cast<ALsizei>(pcmBytes),
            static_cast<ALsizei>(musicStreamSampleRate_));
        return alGetError() == AL_NO_ERROR;
    }

    bool AudioSystem::updateMusicStream(double now)
    {
        if (!available_ || !musicStreamActive_ || musicSource_ == 0)
        {
            return false;
        }

        const ALuint source = static_cast<ALuint>(musicSource_);
        ALint processedCount = 0;
        alGetSourcei(source, AL_BUFFERS_PROCESSED, &processedCount);
        while (processedCount > 0)
        {
            ALuint buffer = 0;
            alSourceUnqueueBuffers(source, 1, &buffer);
            --processedCount;
            if (buffer == 0)
            {
                continue;
            }
            if (!musicStreamFinished_ && fillMusicStreamBuffer(static_cast<uint32_t>(buffer)))
            {
                alSourceQueueBuffers(source, 1, &buffer);
            }
            else
            {
                musicStreamFinished_ = true;
            }
        }

        ALint queuedCount = 0;
        alGetSourcei(source, AL_BUFFERS_QUEUED, &queuedCount);
        if (queuedCount <= 0 && musicStreamFinished_)
        {
            closeMusicStream();
            scheduleNextMusic(now);
            return false;
        }

        ALint state = AL_STOPPED;
        alGetSourcei(source, AL_SOURCE_STATE, &state);
        if (queuedCount > 0 && state != AL_PLAYING && state != AL_PAUSED)
        {
            alSourcePlay(source);
        }
        return queuedCount > 0;
    }

    void AudioSystem::resetMusicPlayback(MusicScene scene, double now)
    {
        stopMusicPlayback();
        activeMusicScene_ = scene;
        lastMusicTrackIndex_ = static_cast<size_t>(-1);
        if (scene == MusicScene::Lobby)
        {
            nextMusicStartTime_ = now;
        }
        else if (scene != MusicScene::None)
        {
            scheduleNextMusic(now);
        }
    }

    void AudioSystem::stopMusicPlayback()
    {
        closeMusicStream();
        nextMusicStartTime_ = 0.0;
    }

    void AudioSystem::closeMusicStream()
    {
        if (musicSource_ != 0)
        {
            const ALuint source = static_cast<ALuint>(musicSource_);
            alSourceStop(source);
            if (musicStreamActive_ || musicDecoder_ != nullptr)
            {
                ALint queuedCount = 0;
                alGetSourcei(source, AL_BUFFERS_QUEUED, &queuedCount);
                while (queuedCount > 0)
                {
                    ALuint buffer = 0;
                    alSourceUnqueueBuffers(source, 1, &buffer);
                    --queuedCount;
                }
            }
            alSourcei(source, AL_BUFFER, 0);
        }

        if (musicDecoder_ != nullptr)
        {
            stb_vorbis_close(static_cast<stb_vorbis*>(musicDecoder_));
            musicDecoder_ = nullptr;
        }

        for (uint32_t& buffer : musicStreamBuffers_)
        {
            if (buffer != 0)
            {
                const ALuint alBuffer = static_cast<ALuint>(buffer);
                alDeleteBuffers(1, &alBuffer);
                buffer = 0;
            }
        }

        if (musicLazyBuffer_ != 0)
        {
            const ALuint buffer = static_cast<ALuint>(musicLazyBuffer_);
            alDeleteBuffers(1, &buffer);
            musicLazyBuffer_ = 0;
        }

        musicStreamChannels_ = 0;
        musicStreamSampleRate_ = 0;
        musicStreamFormat_ = 0;
        musicStreamPcm_.clear();
        musicStreamActive_ = false;
        musicStreamFinished_ = false;
    }

    void AudioSystem::scheduleNextMusic(double now)
    {
        std::uniform_real_distribution<double> delayDistribution(MusicMinDelaySeconds, MusicMaxDelaySeconds);
        nextMusicStartTime_ = now + delayDistribution(musicRandom_);
    }

    void AudioSystem::playSfx2D(uint32_t buffer, float gain)
    {
        if (!available_ || buffer == 0)
        {
            return;
        }

        const uint32_t source = acquireSource();
        if (source == 0)
        {
            return;
        }

        alSourcei(static_cast<ALuint>(source), AL_BUFFER, static_cast<ALint>(buffer));
        alSourcei(static_cast<ALuint>(source), AL_SOURCE_RELATIVE, AL_TRUE);
        alSource3f(static_cast<ALuint>(source), AL_POSITION, 0.0f, 0.0f, 0.0f);
        alSourcef(static_cast<ALuint>(source), AL_GAIN, gain * sfxVolume_);
        alSourcePlay(static_cast<ALuint>(source));
    }

    void AudioSystem::playSfx3D(uint32_t buffer, Vec3 position, float gain)
    {
        if (!available_ || buffer == 0)
        {
            return;
        }

        const uint32_t source = acquireSource();
        if (source == 0)
        {
            return;
        }

        alSourcei(static_cast<ALuint>(source), AL_BUFFER, static_cast<ALint>(buffer));
        alSourcei(static_cast<ALuint>(source), AL_SOURCE_RELATIVE, AL_FALSE);
        alSource3f(static_cast<ALuint>(source), AL_POSITION, position.x, position.y, position.z);
        alSource3f(static_cast<ALuint>(source), AL_VELOCITY, 0.0f, 0.0f, 0.0f);
        alSourcef(static_cast<ALuint>(source), AL_GAIN, gain * sfxVolume_);
        alSourcef(static_cast<ALuint>(source), AL_REFERENCE_DISTANCE, 8.0f);
        alSourcef(static_cast<ALuint>(source), AL_MAX_DISTANCE, 48.0f);
        alSourcef(static_cast<ALuint>(source), AL_ROLLOFF_FACTOR, 1.0f);
        alSourcePlay(static_cast<ALuint>(source));
    }

    void AudioSystem::playButtonClick()
    {
        playSfx2D(buttonClickSound_);
    }

    void AudioSystem::playBlockBreak(Vec3 position)
    {
        playSfx3D(blockBreakSound_, position);
    }

    void AudioSystem::playBlockPlace(Vec3 position)
    {
        playSfx3D(blockPlaceSound_, position);
    }

    void AudioSystem::playItemPickup()
    {
        playSfx2D(itemPickupSound_);
    }
}
