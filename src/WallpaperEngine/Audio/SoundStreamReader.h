#pragma once

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <deque>
#include <span>

namespace WallpaperEngine::Audio {
/** Bridges decoded frames to OpenAL's pull buffer without treating starvation as EOF. */
class SoundStreamReader {
public:
    using Decoder = int (*) (void*, uint8_t*, int);
    static constexpr int End = -1;
    // OpenAL1.21.1: HrtfDirectDelay256 + maximum HRIR128 + history64.
    static constexpr unsigned FilterTail = 448;

    SoundStreamReader (std::span<uint8_t> buffer, int channels, Decoder decoder, void* state,
                       unsigned sourceRate = 1, unsigned outputRate = 1) :
        m_buffer (buffer), m_frameBytes (channels * sizeof (float)), m_decoder (decoder), m_state (state) {
        // OpenAL1.21.1 CalcAttnSourceParams/CalcNonAttnSourceParams: pitch is
        // unity, velocities are zero, and the source step has 12 fraction bits.
        const float step = std::min (float (sourceRate) / float (outputRate), 10.0f) * 4096.0f;
#if defined(__i386__) || defined(__x86_64__)
        m_step = std::max (1u, static_cast<unsigned> (std::lrintf (step)));
#else
        m_step = std::max (1u, static_cast<unsigned> (step));
#endif
    }

    bool repeat = false;
    unsigned tail = 0; // Output frames, independent of the decoded source rate.

    static int read (void* state, void* destination, int bytes) {
        return static_cast<SoundStreamReader*> (state)->read (static_cast<uint8_t*> (destination), bytes);
    }

    /** Called only for unpaused output frames, after the mixer has consumed them. */
    unsigned consumed (unsigned frames) {
        m_consumed += frames;
        unsigned completed = 0;
        while (!m_completions.empty () && m_completions.front () <= m_consumed) {
            m_completions.pop_front ();
            ++completed;
        }
        return completed;
    }

private:
    int read (uint8_t* destination, int bytes) {
        int written = 0;
        bool emptyPass = false;
        while (written < bytes) {
            if (m_index == m_size) {
                if (m_ended) break;
                const int decoded = m_decoder (m_state, m_buffer.data (), static_cast<int> (m_buffer.size ()));
                if (decoded == End) {
                    m_completions.push_back ((m_provided * 4096 + m_step - 1) / m_step + tail);
                    if (!repeat) { m_ended = true; break; }
                    if (!emptyPass) { emptyPass = true; continue; }
                }
                if (decoded <= 0) break;
                m_index = 0;
                m_size = decoded;
                emptyPass = false;
            }
            const int count = std::min (bytes - written, m_size - m_index);
            std::memcpy (destination + written, m_buffer.data () + m_index, count);
            written += count;
            m_index += count;
            m_provided += count / m_frameBytes;
        }
        // Keep the source alive through temporary starvation and its final FIR
        // tail. OpenAL interprets any short callback result as permanent EOF.
        if (!m_ended || (!m_completions.empty () && m_provided * 4096 < m_completions.back () * m_step)) {
            std::memset (destination + written, 0, bytes - written);
            m_provided += (bytes - written) / m_frameBytes;
            written = bytes;
        }
        return written;
    }

    std::span<uint8_t> m_buffer;
    int m_frameBytes;
    Decoder m_decoder;
    void* m_state;
    int m_index = 0, m_size = 0;
    unsigned m_step = 4096;
    uint64_t m_provided = 0; // Input frames delivered, including starvation silence.
    uint64_t m_consumed = 0; // Unpaused output frames.
    bool m_ended = false;
    std::deque<uint64_t> m_completions;
};
}
