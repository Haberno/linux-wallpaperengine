#pragma once

#include "WallpaperEngine/Render/Camera.h"

#include "WallpaperEngine/Render/CWallpaper.h"
#include "WallpaperEngine/Scripting/ScriptEngine.h"

#include <set>
#include <optional>
#include <random>

namespace WallpaperEngine::Render {
enum class RenderSortClass {
    Opaque = 0,
    Translucent = 1,
    Additive = 2,
};

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
    /** Official camera-delay response recovered from wallpaper64.exe. A positive authored
     * delay maps to alpha = (1 - delay / limit) * rate * dt, clamped to [0,1]; zero snaps. */
    static constexpr float PARALLAX_DELAY_LIMIT = 3.0f;
    static constexpr float PARALLAX_DELAY_RATE = 10.0f;
    /** Fraction of each axis' extent a unit-depth layer travels over a full mouse swing
     * (applied per-axis: width for x, height for y) */
    static constexpr float PARALLAX_TRANSLATION_SPAN = 0.5f;

    /** fade.json is fully opaque at a path boundary and clears over 0.5 seconds. */
    static constexpr float CAMERA_FADE_DURATION = 0.5f;

    [[nodiscard]] static float calculateParallaxSmoothingAlpha (float delay, float deltaTime);
    [[nodiscard]] static glm::vec2 calculateShaderParallaxPosition (const glm::vec2& displacement);
    /** Pack the authoring start/end values into common_fog.h's
     * {start, range, startDensity, densityRange} uniform layout. */
    [[nodiscard]] static glm::vec4
    calculateFogParams (float start, float end, float startDensity, float endDensity);
    [[nodiscard]] static float calculateCameraFadeAlpha (float elapsedTime, float duration);
    [[nodiscard]] static size_t chooseCameraPathIndex (
	std::optional<size_t> current, size_t count, const std::string& queueMode, uint32_t randomValue
    );

    struct TransparentSortKey {
	bool sortable = false;
	RenderSortClass renderClass = RenderSortClass::Opaque;
	/** View-space Z: more-negative values are farther from the camera. */
	float cameraDepth = 0.0f;
    };

    /** Return source indices in their per-frame draw slots. Non-sortable entries
     * remain fixed; opaque entries stay stable, followed by blended entries
     * ordered back-to-front and additive entries last. */
    [[nodiscard]] static std::vector<size_t>
    calculateTransparentSortPermutation (const std::vector<TransparentSortKey>& keys);

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
	int spotCount = 0;
	int spotShadowCount = 0;
	int tubeCount = 0;
	/** xyz = world-space direction towards the light, w unused */
	std::vector<glm::vec4> directionalDirections = {};
	/** rgb = color premultiplied by intensity, w unused */
	std::vector<glm::vec4> directionalColors = {};
	/** xyz = world-space position, w = falloff exponent */
	std::vector<glm::vec4> pointOrigins = {};
	/** rgb = color premultiplied by intensity, w = radius */
	std::vector<glm::vec4> pointColors = {};
	/** xyz = world-space position, w = cos(inner cone angle) */
	std::vector<glm::vec4> spotOrigins = {};
	/** xyz = direction the spotlight points, w = cos(outer cone angle) */
	std::vector<glm::vec4> spotDirections = {};
	/** rgb = color premultiplied by intensity, w = radius */
	std::vector<glm::vec4> spotColors = {};
	/** x = falloff exponent, yzw unused */
	std::vector<glm::vec4> spotExponents = {};
	/** Light-space projection and atlas data, indexed like the spot arrays above. */
	std::vector<glm::mat4> spotShadowMatrices = {};
	std::vector<glm::vec4> spotShadowTransforms = {};
	std::vector<float> spotShadowEnabled = {};
	/** Pixel viewport inside the depth atlas; not uploaded to material shaders. */
	std::vector<glm::ivec4> spotShadowViewports = {};
	/** xyz = first world-space endpoint, w = falloff exponent */
	std::vector<glm::vec4> tubeOriginsA = {};
	/** xyz = second world-space endpoint, w unused */
	std::vector<glm::vec4> tubeOriginsB = {};
	/** rgb = color premultiplied by intensity, w = radius */
	std::vector<glm::vec4> tubeColors = {};
    };

    [[nodiscard]] const SceneLights& getLights () const;

    struct SceneFog {
	bool distanceEnabled = false;
	bool heightEnabled = false;
	glm::vec4 distanceParams = {};
	glm::vec4 heightParams = {};
    };

    [[nodiscard]] const SceneFog& getFog () const;

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
    [[nodiscard]] float getSceneFadeAlpha () const override;

    friend class CWallpaper;

private:
    Render::CObject* createObject (const Object& object);
    Render::CObject* dispatchObjectType (const Object& object);
    void addObjectToRenderOrder (const Object& object);
    void updateLightState ();
    void renderSpotShadows ();
    void registerFogScripts ();
    void updateFogState ();
    void updateCameraPath (float deltaTime);
    [[nodiscard]] const CameraPathSource* findActiveCameraPathSource () const;
    [[nodiscard]] std::vector<CObject*> buildFrameRenderOrder () const;

    std::unique_ptr<Scripting::ScriptEngine> m_scriptEngine;
    std::unique_ptr<Camera> m_camera;
    ObjectUniquePtr m_bloomObjectData;
    CObject* m_bloomObject = nullptr;
    // Keeps runtime-created layer data (createLayer) alive: CImage holds a const Image& into it.
    std::vector<ObjectUniquePtr> m_runtimeLayerData = {};
    std::set<int> m_objectsInCreation = {};
    std::map<int, CObject*> m_objects = {};
    std::vector<CObject*> m_objectsByRenderOrder = {};
    std::vector<Objects::CLight*> m_lightObjects = {};
    SceneLights m_lights = {};
    SceneFog m_fog = {};
    std::vector<DynamicValue*> m_scriptedValues = {};
    glm::vec2 m_mousePosition = {};
    glm::vec2 m_mousePositionLast = {};
    glm::vec2 m_mousePositionNormalized = {};
    glm::vec2 m_parallaxDisplacement = {};
    /** Parallax position fed to shaders via g_ParallaxPosition, 0.5,0.5 = centered */
    glm::vec2 m_parallaxPosition = { 0.5f, 0.5f };
    const CameraPathSource* m_activeCameraPathSource = nullptr;
    std::optional<size_t> m_activeCameraPathIndex = std::nullopt;
    float m_cameraPathElapsed = 0.0f;
    std::mt19937 m_cameraPathRandom { std::random_device {} () };
    std::shared_ptr<const CFBO> _rt_4FrameBuffer = nullptr;
    std::shared_ptr<const CFBO> _rt_8FrameBuffer = nullptr;
    std::shared_ptr<const CFBO> _rt_Bloom = nullptr;
    std::shared_ptr<const CFBO> _rt_shadowAtlas = nullptr;

    static constexpr int SHADOW_ATLAS_SIZE = 2048;
    static constexpr int SHADOW_ATLAS_GUARD = 2;
};
} // namespace WallpaperEngine::Render::Wallpaper
