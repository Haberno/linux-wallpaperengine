#pragma once

#include "WallpaperEngine/Render/CObject.h"
#include "WallpaperEngine/Scripting/ScriptableObject.h"

#include <array>

namespace WallpaperEngine::Render::Objects {
using namespace WallpaperEngine::Scripting;

/**
 * Light source in a 3D scene. Draws nothing itself; CScene reads its
 * world-space state every frame to feed the lighting uniforms. Deriving from
 * ScriptableObject keeps script-driven origins/angles ticking (e.g. a sun
 * position script).
 */
class CLight final : public ScriptableObject {
public:
    CLight (Wallpapers::CScene& scene, const Light& light);

    [[nodiscard]] const Light& getLight () const;

    /** world-space position (point lights) */
    [[nodiscard]] glm::vec3 getWorldPosition () const;
    /** world-space direction the light points at (directional lights); +X at zero angles */
    [[nodiscard]] glm::vec3 getWorldDirection () const;
    /** world-space second endpoint of a tube light */
    [[nodiscard]] glm::vec3 getTubeEndPosition () const;
    /** light color premultiplied by intensity, zeroed while the light is invisible */
    [[nodiscard]] glm::vec3 getPremultipliedColor () const;

    /** Native spot uniform packing: cos(inner degrees), cos(outer degrees). */
    [[nodiscard]] static glm::vec2 calculateSpotConeCosines (float innerDegrees, float outerDegrees);
    /** OpenGL light-space matrix spanning the authored outer cone and radius. */
    [[nodiscard]] static glm::mat4 calculateSpotShadowViewProjection (
	const glm::vec3& origin, const glm::vec3& direction, float outerDegrees, float radius
    );
    /** Stable orthographic light-space matrix enclosing a nested camera-frustum cascade. */
    [[nodiscard]] static glm::mat4 calculateDirectionalShadowViewProjection (
	const glm::vec3& cameraEye, const glm::vec3& cameraCenter, const glm::vec3& cameraUp,
	float fieldOfViewDegrees, float aspectRatio, float zoom, float nearDistance, float farDistance,
	const glm::vec3& lightDirection, int shadowResolution
    );
    /** Six +X/-X/+Y/-Y/+Z/-Z view-projections used by the native 2x3 point atlas block. */
    [[nodiscard]] static std::array<glm::mat4, 6>
    calculatePointShadowViewProjections (const glm::vec3& origin, float radius);
    /** Compact perspective coefficients consumed by CalculateProjectedCoordsPoint. */
    [[nodiscard]] static glm::vec4 calculatePointShadowProjectionInfo (float radius);
    /** Transform a tube's authored local control point into its world-space endpoint. */
    [[nodiscard]] static glm::vec3
    calculateTubeEndPosition (const glm::mat4& worldMatrix, const glm::vec3& controlPoint);

private:
    const Light& m_light;
};
} // namespace WallpaperEngine::Render::Objects
