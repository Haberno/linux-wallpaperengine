#pragma once

#include "WallpaperEngine/Render/Wallpapers/CScene.h"
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "WallpaperEngine/Data/Model/Wallpaper.h"

namespace WallpaperEngine::Render::Wallpapers {
class CScene;
}

namespace WallpaperEngine::Render {
using namespace WallpaperEngine::Data::Model;

class Camera {
public:
    Camera (Wallpapers::CScene& scene, const SceneData::Camera& camera);
    ~Camera ();

    void setOrthogonalProjection (const float width, const float height);
    void setPerspectiveProjection (const float width, const float height, const bool flipY);

    [[nodiscard]] const glm::vec3& getCenter () const;
    [[nodiscard]] const glm::vec3& getEye () const;
    [[nodiscard]] const glm::vec3& getUp () const;
    [[nodiscard]] const glm::mat4& getProjection () const;
    [[nodiscard]] const glm::mat4& getLookAt () const;
    [[nodiscard]] Wallpapers::CScene& getScene () const;
    [[nodiscard]] bool isOrthogonal () const;
    /** True when the perspective projection renders Y-mirrored to compensate the
     *  output's vertical flip at present time (see Output::renderVFlip) */
    [[nodiscard]] bool isYFlipped () const;
    [[nodiscard]] float getWidth () const;
    [[nodiscard]] float getHeight () const;
    [[nodiscard]] float getFov () const;
    [[nodiscard]] float getNearZ () const;
    [[nodiscard]] float getFarZ () const;
    [[nodiscard]] float getZoom () const;
    [[nodiscard]] CameraTransform getDefaultTransform () const;
    /** Replace the resting pose used when no camera path is active. */
    void setDefaultTransform (const CameraTransform& transform, bool apply);
    /** Convert a camera layer's world matrix into the eye/forward/up contract. */
    [[nodiscard]] static CameraTransform objectTransform (
	const glm::mat4& world, float fov, float zoom
    );
    /** Apply a sampled camera path transform and rebuild view/projection matrices. */
    void setTransform (const CameraTransform& transform);
    /** Restore the authored resting pose. */
    void resetTransform ();
    /** Convert a normalized scene position (0..1, bottom-left origin) to the
     *  Wallpaper Engine authoring plane at world Z=0. */
    [[nodiscard]] glm::vec3 screenToWorld (const glm::vec2& normalizedPosition) const;

    /** Pure projection helper exposed for coordinate regression tests. */
    [[nodiscard]] static glm::vec3 projectScreenPosition (
	const glm::vec2& normalizedPosition, bool orthogonal, float width, float height, const glm::mat4& projection,
	const glm::mat4& view, bool projectionYFlipped
    );

private:
    float m_width;
    float m_height;
    bool m_isOrthogonal = false;
    bool m_isYFlipped = false;
    glm::mat4 m_projection = {};
    glm::mat4 m_lookat = {};
    CameraTransform m_defaultTransform = {};
    CameraTransform m_transform = {};
    const SceneData::Camera& m_camera;
    Wallpapers::CScene& m_scene;

    void updateMatrices ();
};
} // namespace WallpaperEngine::Render
