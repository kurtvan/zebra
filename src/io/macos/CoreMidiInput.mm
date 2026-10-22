#include "io/MidiInput.hpp"

#include "common/log.hpp"

#import <CoreMIDI/CoreMIDI.h>

#include <atomic>
#include <mutex>

namespace gv::io {

namespace {

constexpr const char* kLogCategory = "midi";

auto ObjectName(MIDIObjectRef object) -> std::string
{
    CFStringRef name = nullptr;
    if (MIDIObjectGetStringProperty(object, kMIDIPropertyDisplayName, &name) != noErr || name == nullptr)
    {
        return "(unnamed)";
    }
    char buffer[256];
    CFStringGetCString(name, buffer, sizeof(buffer), kCFStringEncodingUTF8);
    CFRelease(name);
    return buffer;
}

class CoreMidiInput final : public MidiInput
{
public:
    CoreMidiInput()
    {
        OSStatus status = MIDIClientCreateWithBlock(CFSTR("Govtech Visualizer"), &m_client, ^(const MIDINotification* notification) {
            if (notification->messageID == kMIDIMsgSetupChanged)
            {
                ConnectAllSources();
            }
        });
        if (status != noErr)
        {
            GV_LOG_ERROR(kLogCategory, "MIDIClientCreate failed (%d); MIDI input is unavailable", static_cast<int>(status));
            return;
        }

        status = MIDIInputPortCreateWithProtocol(m_client, CFSTR("Scene triggers"), kMIDIProtocol_1_0, &m_port, ^(const MIDIEventList* list, void*) {
            const MIDIEventPacket* packet = &list->packet[0];
            for (UInt32 i = 0; i < list->numPackets; ++i)
            {
                for (const auto& cc : DecodeControlChanges(packet->words, packet->wordCount))
                {
                    OnControlChange(cc);
                }
                packet = MIDIEventPacketNext(packet);
            }
        });
        if (status != noErr)
        {
            GV_LOG_ERROR(kLogCategory, "MIDIInputPortCreate failed (%d); MIDI input is unavailable", static_cast<int>(status));
            return;
        }
        ConnectAllSources();
    }

    ~CoreMidiInput() override
    {
        if (m_port != 0)
            MIDIPortDispose(m_port);
        if (m_client != 0)
            MIDIClientDispose(m_client);
    }

    void SetChannel(int channel) override
    {
        m_channel.store(channel);
    }

    auto TakeSceneTrigger() -> std::optional<int> override
    {
        const int value = m_pendingTrigger.exchange(-1);
        if (value < 0)
            return std::nullopt;
        return value;
    }

    auto SourceNames() const -> std::vector<std::string> override
    {
        std::lock_guard<std::mutex> lock(m_sourcesMutex);
        return m_sourceNames;
    }

    auto LastControlChange() const -> std::optional<ControlChange> override
    {
        const int packed = m_lastControlChange.load();
        if (packed < 0)
            return std::nullopt;
        return ControlChange{(packed >> 16) & 0xF, (packed >> 8) & 0x7F, packed & 0x7F};
    }

private:
    void OnControlChange(const ControlChange& cc)
    {
        m_lastControlChange.store((cc.channel << 16) | (cc.controller << 8) | cc.value);
        if (cc.channel == m_channel.load() && cc.controller == kSceneTriggerController)
        {
            m_pendingTrigger.store(cc.value);
        }
    }

    /// (Re)connects every source. Connecting an already connected source is harmless.
    void ConnectAllSources()
    {
        std::vector<std::string> names;
        const ItemCount count = MIDIGetNumberOfSources();
        for (ItemCount i = 0; i < count; ++i)
        {
            MIDIEndpointRef source = MIDIGetSource(i);
            MIDIPortConnectSource(m_port, source, nullptr);
            names.push_back(ObjectName(source));
        }
        std::lock_guard<std::mutex> lock(m_sourcesMutex);
        if (names != m_sourceNames)
        {
            std::string joined;
            for (const auto& name : names)
                joined += (joined.empty() ? "" : ", ") + name;
            GV_LOG_INFO(kLogCategory, "listening to %zu source(s): %s", names.size(), joined.c_str());
        }
        m_sourceNames = std::move(names);
    }

    MIDIClientRef m_client{0};
    MIDIPortRef m_port{0};
    std::atomic<int> m_channel{0};
    std::atomic<int> m_pendingTrigger{-1};
    std::atomic<int> m_lastControlChange{-1};
    mutable std::mutex m_sourcesMutex;
    std::vector<std::string> m_sourceNames;
};

} // namespace

auto CreateMidiInput() -> std::unique_ptr<MidiInput>
{
    return std::make_unique<CoreMidiInput>();
}

} // namespace gv::io
