#include "Maths.h"

#include <algorithm>
#include <cmath>

using namespace WallpaperEngine::Maths;

float WallpaperEngine::Maths::randomFloat (std::mt19937& rng, float min, float max) {
    if (max < min) {
	std::swap (min, max);
    }
    std::uniform_real_distribution<float> dist (min, max);
    return dist (rng);
}

glm::vec3 WallpaperEngine::Maths::randomVec3 (std::mt19937& rng, const glm::vec3& min, const glm::vec3& max) {
    return glm::vec3 (
	randomFloat (rng, min.x, max.x), randomFloat (rng, min.y, max.y), randomFloat (rng, min.z, max.z)
    );
}

// Helper: Linear interpolation
float WallpaperEngine::Maths::lerp (float t, float a, float b) { return a + t * (b - a); }

// Helper: Fade value change over lifetime
float WallpaperEngine::Maths::fadeValue (float life, float startTime, float endTime, float startValue, float endValue) {
    if (life <= startTime) {
	return startValue;
    } else if (life >= endTime) {
	return endValue;
    } else {
	float t = (life - startTime) / (endTime - startTime);
	return lerp (t, startValue, endValue);
    }
}

glm::vec3 WallpaperEngine::Maths::particleRgbToHsv (const glm::vec3& color) {
    // Native 1401b8b90 uses hue in turns and treats near-gray ranges as gray.
    const float value = std::max ({ color.r, color.g, color.b });
    const float delta = value - std::min ({ color.r, color.g, color.b });
    if (delta < 1e-5f || value <= 0.0f) return { 0.0f, 0.0f, value };
    float hue = color.r == value ? (color.g - color.b) / delta
	: color.g == value ? (color.b - color.r) / delta + 2.0f
	: (color.r - color.g) / delta + 4.0f;
    hue /= 6.0f;
    if (hue < 0.0f) hue += 1.0f;
    return { hue, delta / value, value };
}

glm::vec3 WallpaperEngine::Maths::particleHsvToRgb (const glm::vec3& color) {
    // Reference construction and override adjustment bound HSV before this call.
    const float chroma = color.z * color.y;
    const float sector = std::fmod (color.x * 6.0f, 6.0f);
    const float other = chroma * (1.0f - std::abs (std::fmod (sector, 2.0f) - 1.0f));
    const float minimum = color.z - chroma;
    const glm::vec3 rgb = sector < 1.0f ? glm::vec3 (chroma, other, 0.0f)
	: sector < 2.0f ? glm::vec3 (other, chroma, 0.0f)
	: sector < 3.0f ? glm::vec3 (0.0f, chroma, other)
	: sector < 4.0f ? glm::vec3 (0.0f, other, chroma)
	: sector < 5.0f ? glm::vec3 (other, 0.0f, chroma)
	: glm::vec3 (chroma, 0.0f, other);
    return rgb + minimum;
}
