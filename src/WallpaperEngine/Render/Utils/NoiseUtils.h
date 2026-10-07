#pragma once

#include <bit>
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>

namespace WallpaperEngine::Render::Utils {

// Perlin noise permutation table
static const unsigned char PERLIN_PERM[]
    = { 151, 160, 137, 91, 90, 15, 131, 13, 201, 95, 96, 53, 194, 233, 7, 225, 140, 36, 103, 30, 69, 142, 8, 99, 37,
	240, 21, 10, 23, 190, 6, 148, 247, 120, 234, 75, 0, 26, 197, 62, 94, 252, 219, 203, 117, 35, 11, 32, 57, 177,
	33, 88, 237, 149, 56, 87, 174, 20, 125, 136, 171, 168, 68, 175, 74, 165, 71, 134, 139, 48, 27, 166, 77, 146,
	158, 231, 83, 111, 229, 122, 60, 211, 133, 230, 220, 105, 92, 41, 55, 46, 245, 40, 244, 102, 143, 54, 65, 25,
	63, 161, 1, 216, 80, 73, 209, 76, 132, 187, 208, 89, 18, 169, 200, 196, 135, 130, 116, 188, 159, 86, 164, 100,
	109, 198, 173, 186, 3, 64, 52, 217, 226, 250, 124, 123, 5, 202, 38, 147, 118, 126, 255, 82, 85, 212, 207, 206,
	59, 227, 47, 16, 58, 17, 182, 189, 28, 42, 223, 183, 170, 213, 119, 248, 152, 2, 44, 154, 163, 70, 221, 153,
	101, 155, 167, 43, 172, 9, 129, 22, 39, 253, 19, 98, 108, 110, 79, 113, 224, 232, 178, 185, 112, 104, 218, 246,
	97, 228, 251, 34, 242, 193, 238, 210, 144, 12, 191, 179, 162, 241, 81, 51, 145, 235, 249, 14, 239, 107, 49, 192,
	214, 31, 181, 199, 106, 157, 184, 84, 204, 176, 115, 121, 50, 45, 127, 4, 150, 254, 138, 236, 205, 93, 222, 114,
	67, 29, 24, 72, 243, 141, 128, 195, 78, 66, 215, 61, 156, 180,
	// Duplicate for wrapping
	151, 160, 137, 91, 90, 15, 131, 13, 201, 95, 96, 53, 194, 233, 7, 225, 140, 36, 103, 30, 69, 142, 8, 99, 37,
	240, 21, 10, 23, 190, 6, 148, 247, 120, 234, 75, 0, 26, 197, 62, 94, 252, 219, 203, 117, 35, 11, 32, 57, 177,
	33, 88, 237, 149, 56, 87, 174, 20, 125, 136, 171, 168, 68, 175, 74, 165, 71, 134, 139, 48, 27, 166, 77, 146,
	158, 231, 83, 111, 229, 122, 60, 211, 133, 230, 220, 105, 92, 41, 55, 46, 245, 40, 244, 102, 143, 54, 65, 25,
	63, 161, 1, 216, 80, 73, 209, 76, 132, 187, 208, 89, 18, 169, 200, 196, 135, 130, 116, 188, 159, 86, 164, 100,
	109, 198, 173, 186, 3, 64, 52, 217, 226, 250, 124, 123, 5, 202, 38, 147, 118, 126, 255, 82, 85, 212, 207, 206,
	59, 227, 47, 16, 58, 17, 182, 189, 28, 42, 223, 183, 170, 213, 119, 248, 152, 2, 44, 154, 163, 70, 221, 153,
	101, 155, 167, 43, 172, 9, 129, 22, 39, 253, 19, 98, 108, 110, 79, 113, 224, 232, 178, 185, 112, 104, 218, 246,
	97, 228, 251, 34, 242, 193, 238, 210, 144, 12, 191, 179, 162, 241, 81, 51, 145, 235, 249, 14, 239, 107, 49, 192,
	214, 31, 181, 199, 106, 157, 184, 84, 204, 176, 115, 121, 50, 45, 127, 4, 150, 254, 138, 236, 205, 93, 222, 114,
	67, 29, 24, 72, 243, 141, 128, 195, 78, 66, 215, 61, 156, 180 };

// Native turbulent-velocity initialization uses a scalar, time-based field.
inline float simplexNoise1D (float x) {
    const int cell = static_cast<int> (std::floor (x));
    const float t = x - static_cast<float> (cell);
    const auto corner = [] (float position, int index) {
	const int hash = PERLIN_PERM[index & 255];
	const float gradient = static_cast<float> (1 + (hash & 7)) * ((hash & 8) ? -1.0f : 1.0f);
	const float weight = 1.0f - position * position;
	const float squared = weight * weight;
	return squared * squared * gradient * position;
    };
    return 0.395f * (corner (t, cell) + corner (t - 1.0f, cell + 1));
}

// The particle position-offset field uses 2D simplex noise with the same
// permutation as Perlin, a 6-bit gradient hash and normalized octave weights.
inline float simplexNoise2D (float x, float y) {
    constexpr float skew = 0.3660254037844386f;
    constexpr float unskew = 0.2113248654051871f;
    const float s = (x + y) * skew;
    const int i = static_cast<int> (std::floor (x + s));
    const int j = static_cast<int> (std::floor (y + s));
    const float t = static_cast<float> (i + j) * unskew;
    const float x0 = x - (static_cast<float> (i) - t);
    const float y0 = y - (static_cast<float> (j) - t);
    const int i1 = x0 > y0 ? 1 : 0;
    const int j1 = 1 - i1;
    const auto corner = [] (float cx, float cy, int ix, int iy) {
	const float weight = 0.5f - cx * cx - cy * cy;
	if (weight < 0.0f) return 0.0f;
	const int hash = PERLIN_PERM[((ix & 255) + PERLIN_PERM[iy & 255]) & 255] & 63;
	const float u = hash < 4 ? cx : cy;
	const float v = hash < 4 ? cy : cx;
	const float gradient = ((hash & 1) ? -u : u) + ((hash & 2) ? -2.0f * v : 2.0f * v);
	return gradient * weight * weight * weight * weight;
    };
    return 45.23065f * (corner (x0, y0, i, j)
	+ corner (x0 - i1 + unskew, y0 - j1 + unskew, i + i1, j + j1)
	+ corner (x0 - 1.0f + 2.0f * unskew, y0 - 1.0f + 2.0f * unskew, i + 1, j + 1));
}

// Native remap noise uses the particle random float's bits as its seed, not
// the permutation table used by position offsets and turbulence (1400fbe90).
inline float seededSimplexNoise2D (uint32_t seed, float x, float y) {
    constexpr float skew = .3660253882408142f;
    constexpr float unskew = .21132487058639526f;
    const float s = (x + y) * skew;
    const float i = std::floor (x + s), j = std::floor (y + s);
    const float t = (j + i) * unskew;
    const float x0 = x - (i - t), y0 = y - (j - t);
    const uint32_t ix = static_cast<uint32_t> (static_cast<int32_t> (i)) * 501125321u;
    const uint32_t iy = static_cast<uint32_t> (static_cast<int32_t> (j)) * 1136930381u;
    const uint32_t dx = y0 < x0 ? 501125321u : 0u;
    const uint32_t dy = y0 < x0 ? 0u : 1136930381u;
    const auto corner = [seed] (float cx, float cy, uint32_t a, uint32_t b) {
	uint32_t hash = (seed ^ a ^ b) * 668265261u;
	hash ^= static_cast<uint32_t> (std::bit_cast<int32_t> (hash) >> 15);
	const float gx = (hash & 1) ? -cx : cx;
	const float gy = (hash & 2) ? -cy : cy;
	const float gradient = (hash & 4) ? gy * 2.4142136573791504f + gx
					 : gx * 2.4142136573791504f + gy;
	const float weight = glm::max (.5f + ((-cx * cx) - cy * cy), 0.0f);
	const float squared = weight * weight;
	return gradient * (squared * squared);
    };
    return (corner (x0, y0, ix, iy)
	    + corner ((x0 - (dx != 0)) + unskew, (y0 - (dy != 0)) + unskew, ix + dx, iy + dy)
	    + corner (x0 - .5773502588272095f, y0 - .5773502588272095f,
		      ix + 501125321u, iy + 1136930381u)) * 38.283687591552734f;
}

// Native remap's default FBM: three octaves, gain .5, lacunarity 2 and
// normalized initial amplitude. Each octave increments the integer seed.
inline float seededFbmNoise2D (uint32_t seed, float x, float y, uint32_t octaves = 3) {
    float normalization = 1.0f;
    float amplitude = .5f;
    for (uint32_t octave = 1; octave < octaves; ++octave) {
	normalization += amplitude;
	amplitude *= .5f;
    }
    // Native always evaluates the first seeded sample, including count zero.
    amplitude = 1.0f / normalization;
    float sum = seededSimplexNoise2D (seed, x, y) * amplitude;
    for (uint32_t octave = 1; octave < octaves; ++octave) {
	++seed;
	x *= 2.0f;
	y *= 2.0f;
	amplitude *= .5f;
	sum += seededSimplexNoise2D (seed, x, y) * amplitude;
    }
    return sum;
}

inline float fractalNoise2D (float x, float y, int octaves) {
    float sum = 0.0f;
    float normalization = 0.0f;
    float frequency = 1.0f;
    float weight = 1.0f;
    for (int octave = 0; octave < octaves; ++octave) {
	sum += simplexNoise2D (x * frequency, y * frequency) * weight;
	normalization += weight;
	frequency *= 2.0f;
	weight *= 0.5f;
    }
    return sum / normalization;
}

// Native turbulence's scalar 3D simplex field. Its last corner and Y/Z tie
// ranking differ from the conventional simplex algorithm.
inline float simplexNoise3D (float x, float y, float z) {
    constexpr float skew = 1.0f / 3.0f;
    constexpr float unskew = 1.0f / 6.0f;
    const float s = ((y + z) + x) * skew;
    int i = static_cast<int> (std::floor (x + s));
    int j = static_cast<int> (std::floor (y + s));
    int k = static_cast<int> (std::floor (z + s));
    const float t = static_cast<float> ((k + j) + i) * unskew;
    const float x0 = x - (static_cast<float> (i) - t);
    const float y0 = y - (static_cast<float> (j) - t);
    const float z0 = z - (static_cast<float> (k) - t);
    glm::ivec3 first, second;
    if (y0 == z0 && x0 < y0) {
	first = { 0, 0, 0 };
	second = { 0, 0, 1 };
    } else if (x0 >= y0) {
	if (y0 >= z0) { first = { 1, 0, 0 }; second = { 1, 1, 0 }; }
	else if (x0 >= z0) { first = { 1, 0, 0 }; second = { 1, 0, 1 }; }
	else { first = { 0, 0, 1 }; second = { 1, 0, 1 }; }
    } else {
	if (y0 < z0) { first = { 0, 0, 1 }; second = { 0, 1, 1 }; }
	else if (x0 < z0) { first = { 0, 1, 0 }; second = { 0, 1, 1 }; }
	else { first = { 0, 1, 0 }; second = { 1, 1, 0 }; }
    }
    static constexpr int gradients[12][3] = {
	{ 1, 1, 0 }, { -1, 1, 0 }, { 1, -1, 0 }, { -1, -1, 0 },
	{ 1, 0, 1 }, { -1, 0, 1 }, { 1, 0, -1 }, { -1, 0, -1 },
	{ 0, 1, 1 }, { 0, -1, 1 }, { 0, 1, -1 }, { 0, -1, -1 }
    };
    const auto corner = [] (float cx, float cy, float cz, int hash) {
	const float weight = ((0.6f - cx * cx) - cy * cy) - cz * cz;
	if (weight < 0.0f) return 0.0f;
	const auto& gradient = gradients[hash % 12];
	const float dot = (gradient[2] * cz + gradient[1] * cy) + gradient[0] * cx;
	const float squared = weight * weight;
	return dot * (squared * squared);
    };
    i &= 255; j &= 255; k &= 255;
    const auto hash = [] (int ix, int iy, int iz) { return PERLIN_PERM[ix + PERLIN_PERM[iy + PERLIN_PERM[iz]]]; };
    const float n0 = corner (x0, y0, z0, hash (i, j, k));
    const float n1 = corner (x0 - first.x + unskew, y0 - first.y + unskew, z0 - first.z + unskew,
			     hash (i + first.x, j + first.y, k + first.z));
    const float n2 = corner (x0 - second.x + skew, y0 - second.y + skew, z0 - second.z + skew,
			     hash (i + second.x, j + second.y, k + second.z));
    // Native advances i/j but retains k for this corner's gradient lookup.
    const float n3 = corner ((x0 - 1.0f) + 0.5f, (y0 - 1.0f) + 0.5f, (z0 - 1.0f) + 0.5f,
			     hash (i + 1, j + 1, k));
    return (((n3 + n2) + n1) + n0) * 32.0f;
}

} // namespace WallpaperEngine::Render::Utils
