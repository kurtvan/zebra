#include "io/AudioInput.hpp"

#include "common/log.hpp"

namespace gv::io {

namespace {
constexpr const char* kLogCategory = "audio";
}

AudioInput::AudioInput(SpscRingBuffer<float>& ring)
    : m_ring(ring)
{
}

AudioInput::~AudioInput()
{
    Close();
}

auto AudioInput::ListDevices() -> std::vector<std::string>
{
    std::vector<std::string> names;
    const int count = SDL_GetNumAudioDevices(1);
    for (int i = 0; i < count; ++i)
    {
        if (const char* name = SDL_GetAudioDeviceName(i, 1))
        {
            names.emplace_back(name);
        }
    }
    return names;
}

auto AudioInput::Open(const std::string& name) -> bool
{
    Close();

    SDL_AudioSpec want{};
    want.freq = 48000;
    want.format = AUDIO_F32SYS;
    want.channels = 2;
    want.samples = 512;
    want.callback = &AudioInput::Callback;
    want.userdata = this;

    SDL_AudioSpec have{};
    m_device = SDL_OpenAudioDevice(name.empty() ? nullptr : name.c_str(), 1, &want, &have, SDL_AUDIO_ALLOW_CHANNELS_CHANGE | SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (m_device == 0)
    {
        GV_LOG_ERROR(kLogCategory, "could not open input \"%s\": %s", name.empty() ? "(system default)" : name.c_str(), SDL_GetError());
        return false;
    }
    if (have.channels > 2)
    {
        GV_LOG_WARN(kLogCategory, "input has %d channels; using the first two", have.channels);
    }
    m_name = name;
    m_channels = have.channels;
    m_sampleRate = have.freq;
    SDL_PauseAudioDevice(m_device, 0);
    GV_LOG_INFO(kLogCategory, "capturing from \"%s\": %d Hz, %d channel(s)", name.empty() ? "(system default)" : name.c_str(), m_sampleRate, m_channels);
    return true;
}

void AudioInput::Close()
{
    if (m_device != 0)
    {
        SDL_CloseAudioDevice(m_device);
        m_device = 0;
        GV_LOG_INFO(kLogCategory, "closed input \"%s\"", m_name.c_str());
    }
}

auto AudioInput::HandleEvent(const SDL_Event& event) -> bool
{
    if (event.type == SDL_AUDIODEVICEREMOVED && event.adevice.iscapture && event.adevice.which == m_device)
    {
        GV_LOG_WARN(kLogCategory, "input \"%s\" was removed", m_name.c_str());
        Close();
        return true;
    }
    return false;
}

void AudioInput::Callback(void* userData, Uint8* stream, int length)
{
    auto* self = static_cast<AudioInput*>(userData);
    const auto* samples = reinterpret_cast<const float*>(stream);
    const size_t frames = static_cast<size_t>(length) / sizeof(float) / static_cast<size_t>(self->m_channels);

    if (self->m_channels == 2)
    {
        self->m_ring.Write(samples, frames * 2);
        return;
    }
    self->m_stereo.resize(frames * 2);
    for (size_t i = 0; i < frames; ++i)
    {
        const float left = samples[i * static_cast<size_t>(self->m_channels)];
        const float right = self->m_channels > 1 ? samples[i * static_cast<size_t>(self->m_channels) + 1] : left;
        self->m_stereo[i * 2] = left;
        self->m_stereo[i * 2 + 1] = right;
    }
    self->m_ring.Write(self->m_stereo.data(), self->m_stereo.size());
}

} // namespace gv::io
