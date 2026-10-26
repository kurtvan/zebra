#include "io/MidiInput.hpp"

namespace gv::io {

auto UmpWordCount(uint32_t firstWord) -> size_t
{
    // UMP message sizes by message type (high nibble), from the UMP specification.
    static constexpr size_t kWords[16] = {1, 1, 1, 2, 2, 4, 1, 1, 2, 2, 2, 3, 3, 4, 4, 4};
    return kWords[(firstWord >> 28) & 0xF];
}

auto DecodeControlChanges(const uint32_t* words, size_t wordCount) -> std::vector<ControlChange>
{
    std::vector<ControlChange> result;
    size_t i = 0;
    while (i < wordCount)
    {
        const uint32_t word = words[i];
        const uint32_t messageType = (word >> 28) & 0xF;
        if (messageType == 0x2)
        {
            const uint32_t status = (word >> 16) & 0xFF;
            if ((status & 0xF0) == 0xB0)
            {
                result.push_back({static_cast<int>(status & 0x0F),
                                  static_cast<int>((word >> 8) & 0x7F),
                                  static_cast<int>(word & 0x7F)});
            }
        }
        i += UmpWordCount(word);
    }
    return result;
}

} // namespace gv::io
