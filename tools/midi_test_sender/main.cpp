// Creates a virtual MIDI source ("GV Test MIDI") and sends CC 20 values, for checking the app's MIDI input
// by hand: new sources appearing while it runs, triggers, and sources going away.
//
//   gv_midi_test_sender [channel 1-16] item ...
//   item: value[@seconds]       CC 20 = value after `seconds` (default 1)
//         clock:BPM:SECONDS     Start, MIDI clock at BPM for SECONDS, Stop
//   e.g. gv_midi_test_sender 1 3@1 clock:128:4 5@0.5

#include <CoreMIDI/CoreMIDI.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

int main(int argc, char* argv[])
{
    if (argc < 3)
    {
        std::fprintf(stderr, "usage: %s channel value[@seconds] ...\n", argv[0]);
        return 1;
    }
    const int channel = std::atoi(argv[1]) - 1;

    MIDIClientRef client = 0;
    MIDIEndpointRef source = 0;
    if (MIDIClientCreate(CFSTR("GV Test MIDI"), nullptr, nullptr, &client) != noErr ||
        MIDISourceCreateWithProtocol(client, CFSTR("GV Test MIDI"), kMIDIProtocol_1_0, &source) != noErr)
    {
        std::fprintf(stderr, "could not create a virtual MIDI source\n");
        return 1;
    }
    std::printf("virtual source \"GV Test MIDI\" up\n");
    std::fflush(stdout);
    // Give listeners a moment to notice the new source and connect, or they miss the first messages.
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    auto sendSystem = [&](uint8_t status) {
        MIDIEventList list{};
        MIDIEventPacket* packet = MIDIEventListInit(&list, kMIDIProtocol_1_0);
        const uint32_t word = (0x1u << 28) | (static_cast<uint32_t>(status) << 16);
        MIDIEventListAdd(&list, sizeof(list), packet, 0, 1, &word);
        MIDIReceivedEventList(source, &list);
    };

    for (int i = 2; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg.rfind("clock:", 0) == 0)
        {
            const auto second = arg.find(':', 6);
            const double bpm = std::atof(arg.substr(6, second - 6).c_str());
            const double seconds = std::atof(arg.substr(second + 1).c_str());
            const auto pulse = std::chrono::duration<double>(60.0 / bpm / 24.0);
            sendSystem(0xFA);
            auto next = std::chrono::steady_clock::now();
            const auto end = next + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(seconds));
            int pulses = 0;
            while (next < end)
            {
                sendSystem(0xF8);
                pulses++;
                next += std::chrono::duration_cast<std::chrono::steady_clock::duration>(pulse);
                std::this_thread::sleep_until(next);
            }
            sendSystem(0xFC);
            std::printf("sent %d clock pulses at %.1f BPM, then stop\n", pulses, bpm);
            std::fflush(stdout);
            continue;
        }
        const auto at = arg.find('@');
        const int value = std::atoi(arg.substr(0, at).c_str());
        const double delay = at == std::string::npos ? 1.0 : std::atof(arg.substr(at + 1).c_str());
        std::this_thread::sleep_for(std::chrono::duration<double>(delay));

        MIDIEventList list{};
        MIDIEventPacket* packet = MIDIEventListInit(&list, kMIDIProtocol_1_0);
        const uint32_t word = (0x2u << 28) | (static_cast<uint32_t>(0xB0 | (channel & 0xF)) << 16) | (20u << 8) | static_cast<uint32_t>(value & 0x7F);
        MIDIEventListAdd(&list, sizeof(list), packet, 0, 1, &word);
        MIDIReceivedEventList(source, &list);
        std::printf("sent CC 20 = %d on channel %d\n", value, channel + 1);
        std::fflush(stdout);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    MIDIEndpointDispose(source);
    MIDIClientDispose(client);
    std::printf("virtual source removed\n");
    return 0;
}
