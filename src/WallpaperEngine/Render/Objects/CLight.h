#pragma once

#include "WallpaperEngine/Render/CObject.h"
#include "WallpaperEngine/Scripting/ScriptableObject.h"

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
    /** light color premultiplied by intensity, zeroed while the light is invisible */
    [[nodiscard]] glm::vec3 getPremultipliedColor () const;

private:
    const Light& m_light;
};
} // namespace WallpaperEngine::Render::Objects
