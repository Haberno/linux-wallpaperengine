#pragma once

#include "WallpaperEngine/Render/Camera.h"

#include "WallpaperEngine/Render/CWallpaper.h"
#include "WallpaperEngine/Scripting/ScriptEngine.h"

namespace WallpaperEngine::Render {
class Camera;
class CObject;
}

namespace WallpaperEngine::Render::Objects {
class CLight;
}

namespace WallpaperEngine::Render::Wallpapers {
using namespace WallpaperEngine::Data::Model;

class CScene final : public CWallpaper {
public:
    CScene (
	const Wallpaper& wallpaper, RenderContext& context, AudioContext& audioContext,
	const WallpaperState::TextureUVsScaling& scalingMode, const uint32_t& clampMode
    );

    ~CScene () override;

    [[nodiscard]] Scripting::ScriptEngine& getScriptEngine () const;
    [[nodiscard]] Camera& getCamera () const;

    [[nodiscard]] const Scene& getScene () const;

    [[nodiscard]] int getWidth () const override;
    [[nodiscard]] int getHeight () const override;

    // Time accessors used by dynamic text layers (CText + ScriptEngine).
    // Read from the application-wide g_Time/g_TimeLast globals that other
    // renderers already consume via extern (e.g. CParticle).
    [[nodiscard]] float getTime () const;
    [[nodiscard]] float getDeltaTime () const;
    [[nodiscard]] float getFps () const;

    /**
     * Wallpaper Engine conventions for camera parallax that are not encoded in wallpaper
     * assets; everything else (amount, delay, influence, per-object depth, locktransforms)
     * comes from scene.json.
     */
    /** Converts the authored cameraparallaxdelay value into the smoothing time constant in seconds */
    static constexpr float PARALLAX_DELAY_TO_SECONDS = 0.1f;
    /** Fraction of the scene size a unit-depth layer travels over a full mouse swing */
    static constexpr float PARALLAX_TRANSLATION_SPAN = 0.5f;

    const glm::vec2* getMousePosition () const;
    const glm::vec2* getMousePositionLast () const;
    const glm::vec2* getMousePositionNormalized () const;
    const glm::vec2* getParallaxDisplacement () const;
    const glm::vec2* getParallaxPosition () const;

    [[nodiscard]] const std::vector<CObject*>& getObjectsByRenderOrder () const;
    [[nodiscard]] const CObject* getObject (int id) const;

    /**
     * Per-frame light state for 3D scenes, laid out to match the LightingV1
     * uniform contract (see ShaderUnit::generateLightingV1). Counts are fixed
     * at load time so the vectors' storage stays valid for uniform pointers.
     */
    struct SceneLights {
	int directionalCount = 0;
	int pointCount = 0;
	/** xyz = world-space direction towards the light, w unused */
	std::vector<glm::vec4> directionalDirections = {};
	/** rgb = color premultiplied by intensity, w unused */
	std::vector<glm::vec4> directionalColors = {};
	/** xyz = world-space position, w = falloff exponent */
	std::vector<glm::vec4> pointOrigins = {};
	/** rgb = color premultiplied by intensity, w = radius */
	std::vector<glm::vec4> pointColors = {};
    };

    [[nodiscard]] const SceneLights& getLights () const;

    // Runtime layer API — backs thisScene.createLayer()/getLayerIndex()/sortLayer() in the
    // scripting engine. Audio visualizers (and other generative scripts) spawn their bar layers
    // at init() time via these; without them the controlling script throws and the placeholder
    // template renders as a static block.
    //
    // createLayer instantiates a new image layer from a model path (e.g. "models/full-pixel.json"),
    // resolving the script's workshop-scoped asset path when the bare path doesn't exist. Returns the
    // created object (a scriptable CImage) or nullptr on failure.
    Render::CObject* createLayer (const std::string& modelPath, const std::string& workshopId);
    // Index of a layer within the scriptable-layer subset of the render order (matches getLayer()/
    // getLayerCount()), or -1 if not present.
    [[nodiscard]] int getScriptableLayerIndex (const CObject* layer) const;
    // Move a layer so it sits at the given scriptable-layer index in the render order (z-order).
    void moveLayerToScriptableIndex (CObject* layer, int index);

protected:
    void renderFrame (const glm::ivec4& viewport) override;
    void updateMouse (const glm::ivec4& viewport);

    friend class CWallpaper;

private:
    Render::CObject* createObject (const Object& object);
    Render::CObject* dispatchObjectType (const Object& object);
    void addObjectToRenderOrder (const Object& object);
    void updateLightState ();

    std::unique_ptr<Scripting::ScriptEngine> m_scriptEngine;
    std::unique_ptr<Camera> m_camera;
    ObjectUniquePtr m_bloomObjectData;
    CObject* m_bloomObject = nullptr;
    // Keeps runtime-created layer data (createLayer) alive: CImage holds a const Image& into it.
    std::vector<ObjectUniquePtr> m_runtimeLayerData = {};
    std::map<int, CObject*> m_objects = {};
    std::vector<CObject*> m_objectsByRenderOrder = {};
    std::vector<Objects::CLight*> m_lightObjects = {};
    SceneLights m_lights = {};
    std::vector<DynamicValue*> m_scriptedValues = {};
    glm::vec2 m_mousePosition = {};
    glm::vec2 m_mousePositionLast = {};
    glm::vec2 m_mousePositionNormalized = {};
    glm::vec2 m_parallaxDisplacement = {};
    /** Parallax position fed to shaders via g_ParallaxPosition, 0.5,0.5 = centered */
    glm::vec2 m_parallaxPosition = {0.5f, 0.5f};
    std::shared_ptr<const CFBO> _rt_4FrameBuffer = nullptr;
    std::shared_ptr<const CFBO> _rt_8FrameBuffer = nullptr;
    std::shared_ptr<const CFBO> _rt_Bloom = nullptr;
    std::shared_ptr<const CFBO> _rt_shadowAtlas = nullptr;
};
} // namespace WallpaperEngine::Render::Wallpaper
