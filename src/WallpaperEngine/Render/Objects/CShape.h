#pragma once

#include "CRenderable.h"
#include "WallpaperEngine/Data/Model/Object.h"
#include "WallpaperEngine/Scripting/ScriptableObject.h"

#include <array>
#include <memory>

namespace WallpaperEngine::Render::Objects {
using namespace WallpaperEngine::Data::Model;
/** Procedural quad: its effect supplies both the shader and the four UV-space corners. */
class CShape final : public CRenderable, public Scripting::ScriptableObject {
public:
    CShape (Wallpapers::CScene& scene, const Shape& shape);
    ~CShape () override;
    void setup () override;
    void render () override;

    const float& getBrightness () const override { return m_brightness; }
    const float& getUserAlpha () const override { return m_alpha; }
    const float& getAlpha () const override { return m_alpha; }
    const glm::vec3& getColor () const override { return m_color; }
    const glm::vec4& getColor4 () const override { return m_color4; }
    const glm::vec3& getCompositeColor () const override { return m_color; }

private:
    struct Pass {
	const ImageEffect* effect;
	std::unique_ptr<Effects::CPass> draw;
    };
    bool corner (const Effects::CPass& pass, const std::string& name, glm::vec2& value) const;
    const Shape& m_shape;
    std::vector<Pass> m_passes;
    GLuint m_vertices = 0;
    GLuint m_indices = 0;
    glm::mat4 m_model { 1.0f };
    glm::mat4 m_viewProjection { 1.0f };
    glm::mat4 m_mvp { 1.0f };
    glm::mat4 m_mvpInverse { 1.0f };
    float m_brightness = 1.0f;
    float m_alpha = 1.0f;
    glm::vec3 m_color { 1.0f };
    glm::vec4 m_color4 { 1.0f };
};
}
