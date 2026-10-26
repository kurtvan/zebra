#pragma once

#include "common/SpscRingBuffer.hpp"

#include <SDL.h>

#include <string>
#include <vector>

namespace gv::io {

/// Captures one macOS audio input device and writes interleaved stereo float samples into a ring buffer.
///
/// Uses SDL2's capture API (CoreAudio underneath), so devices are identified by name; SDL doesn't expose
/// CoreAudio device UIDs.
class AudioInput
{
public:
    explicit AudioInput(SpscRingBuffer<float>& ring);
    ~AudioInput();

    AudioInput(const AudioInput&) = delete;
    auto operator=(const AudioInput&) -> AudioInput& = delete;

    static auto ListDevices() -> std::vector<std::string>;

    /// Opens `name`, or the system default input when empty. Returns false (and logs) on failure.
    auto Open(const std::string& name) -> bool;
    void Close();

    /// Handles SDL_AUDIODEVICEREMOVED for the open device. Returns true if the device went away.
    auto HandleEvent(const SDL_Event& event) -> bool;

    auto IsOpen() const -> bool
    {
        return m_device != 0;
    }
    auto DeviceName() const -> const std::string&
    {
        return m_name;
    }
    auto SampleRate() const -> int
    {
        return m_sampleRate;
    }

private:
    static void Callback(void* userData, Uint8* stream, int length);

    SpscRingBuffer<float>& m_ring;
    SDL_AudioDeviceID m_device{0};
    std::string m_name;
    int m_channels{0};
    int m_sampleRate{0};
    std::vector<float> m_stereo; //!< Audio thread scratch for mono -> stereo.
};

} // namespace gv::io
