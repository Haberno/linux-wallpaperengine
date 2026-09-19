#pragma once

namespace WallpaperEngine::Render {
/** Measures completed frame intervals, including pacing and stalls, per output. */
class FrameRateCounter {
public:
    void frame (double now) {
        if (m_start < 0.0) {
            m_start = now;
            return;
        }
        ++m_frames;
        const double elapsed = now - m_start;
        if (elapsed >= 0.5) {
            m_fps = static_cast<float> (m_frames / elapsed);
            m_frames = 0;
            m_start = now;
        }
    }

    [[nodiscard]] float fps () const { return m_fps; }

private:
    double m_start = -1.0;
    unsigned int m_frames = 0;
    float m_fps = 0.0f;
};
} // namespace WallpaperEngine::Render
