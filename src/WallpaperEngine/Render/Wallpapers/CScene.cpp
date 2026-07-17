#include "WallpaperEngine/Render/Objects/CImage.h"
#include "WallpaperEngine/Render/Objects/CLight.h"
#include "WallpaperEngine/Render/Objects/CModel.h"
#include "WallpaperEngine/Render/Objects/CParticle.h"
#include "WallpaperEngine/Render/Objects/CSound.h"
#include "WallpaperEngine/Render/Objects/CText.h"

#include "WallpaperEngine/Render/WallpaperState.h"

#include "CScene.h"
#include "WallpaperEngine/Logging/Log.h"

#include "WallpaperEngine/Data/Model/Wallpaper.h"
#include "WallpaperEngine/Data/Parsers/ObjectParser.h"
#include "WallpaperEngine/Data/Utils/ScopeGuard.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <ranges>

extern float g_Time;
extern float g_TimeLast;

using namespace WallpaperEngine;
using namespace WallpaperEngine::Render;
using namespace WallpaperEngine::Data::Model;
using namespace WallpaperEngine::Data::Parsers;
using namespace WallpaperEngine::Render::Wallpapers;

CScene::CScene (
    const Wallpaper& wallpaper, RenderContext& context, AudioContext& audioContext,
    const WallpaperState::TextureUVsScaling& scalingMode, const uint32_t& clampMode
) : CWallpaper (wallpaper, context, audioContext, scalingMode, clampMode) {
    // caller should check this, if not a std::bad_cast is good to throw
    auto scene = wallpaper.as<Scene> ();

    // setup scripting engine
    this->m_scriptEngine = std::make_unique<Scripting::ScriptEngine> (*this, context.getMediaSource ());
    // setup the scene camera
    this->m_camera = std::make_unique<Camera> (*this, scene->camera);
    this->registerFogScripts ();
    this->updateFogState ();

    float width = scene->camera.projection.width;
    float height = scene->camera.projection.height;
    const bool isPerspective = scene->camera.projection.isPerspective;

    // 3D scenes have no authored projection size; render at the output's resolution
    if (isPerspective) {
	width = this->getContext ().getOutput ().getFullWidth ();
	height = this->getContext ().getOutput ().getFullHeight ();
    }

    // detect size if the orthogonal project is auto
    if (!isPerspective && scene->camera.projection.isAuto) {
	glm::vec2 maxExtent = { 0.0f, 0.0f };

	for (const auto& object : scene->objects) {
	    if (!object->is<Image> ()) {
		continue;
	    }

	    const auto* image = object->as<Image> ();
	    if (!image->origin || !image->origin->value) {
		continue;
	    }

	    const glm::vec3 origin = image->origin->value->getVec3 ();
	    const glm::vec2 halfSize = image->size / 2.0f;

	    maxExtent.x = glm::max (maxExtent.x, glm::abs (origin.x) + halfSize.x);
	    maxExtent.y = glm::max (maxExtent.y, glm::abs (origin.y) + halfSize.y);
	}

	if (maxExtent.x > 0.0f && maxExtent.y > 0.0f) {
	    width = maxExtent.x * 2.0f;
	    height = maxExtent.y * 2.0f;
	} else {
	    // Use the first-captured output size, not the live one: an in-process rebuild (live property
	    // change, control-socket bg swap) must size the scene exactly like the original build or the
	    // effect-composite chain samples misaligned buffers and effects stop showing.
	    const auto fallback = this->getContext ().getStableOutputSize ();
	    width = fallback.x;
	    height = fallback.y;
	    sLog.debug ("Auto projection: falling back to screen resolution ", width, "x", height);
	}
    }

    this->m_parallaxDisplacement = { 0, 0 };

    if (isPerspective) {
	this->m_camera->setPerspectiveProjection (
	    width, height, this->getContext ().getOutput ().renderVFlip ()
	);

	// fixed light counts let passes compile LightingV1 with matching uniform arrays
	// before any object is created
	for (const auto& object : scene->objects) {
	    if (!object->is<Data::Model::Light> ()) {
		continue;
	    }

	    const auto* light = object->as<Data::Model::Light> ();
	    switch (light->type) {
		case LightData::Type_Directional:
		    this->m_lights.directionalCount++;
		    if (light->castShadow) {
			this->m_lights.directionalShadowCount++;
		    }
		    break;
		case LightData::Type_Spot:
		    this->m_lights.spotCount++;
		    if (light->castShadow) {
			this->m_lights.spotShadowCount++;
		    }
		    break;
		case LightData::Type_Tube: this->m_lights.tubeCount++; break;
		case LightData::Type_Point:
		    this->m_lights.pointCount++;
		    if (light->castShadow) {
			this->m_lights.pointShadowCount++;
		    }
		    break;
	    }
	}

	this->m_lights.shadowFeatureCount
	    = this->m_lights.directionalShadowCount * 3 + this->m_lights.spotShadowCount;
	this->m_lights.shadowViewCount
	    = this->m_lights.shadowFeatureCount + this->m_lights.pointShadowCount * 6;
	this->m_lights.directionalDirections.resize (this->m_lights.directionalCount, glm::vec4 (0.0f));
	this->m_lights.directionalColors.resize (this->m_lights.directionalCount, glm::vec4 (0.0f));
	this->m_lights.directionalShadowFeatures.resize (this->m_lights.directionalCount, glm::ivec3 (-1));
	this->m_lights.directionalShadowEnabled.resize (this->m_lights.directionalCount, 0.0f);
	this->m_lights.pointOrigins.resize (this->m_lights.pointCount, glm::vec4 (0.0f));
	this->m_lights.pointColors.resize (this->m_lights.pointCount, glm::vec4 (0.0f));
	this->m_lights.pointShadowProjections.resize (this->m_lights.pointCount, glm::vec4 (0.0f));
	this->m_lights.pointShadowTransforms.resize (this->m_lights.pointCount, glm::vec4 (0.0f));
	this->m_lights.pointShadowEnabled.resize (this->m_lights.pointCount, 0.0f);
	this->m_lights.pointShadowMatrices.resize (this->m_lights.pointCount);
	this->m_lights.pointShadowViewports.resize (this->m_lights.pointCount);
	this->m_lights.spotOrigins.resize (this->m_lights.spotCount, glm::vec4 (0.0f));
	this->m_lights.spotDirections.resize (this->m_lights.spotCount, glm::vec4 (0.0f));
	this->m_lights.spotColors.resize (this->m_lights.spotCount, glm::vec4 (0.0f));
	this->m_lights.spotExponents.resize (this->m_lights.spotCount, glm::vec4 (0.0f));
	this->m_lights.spotShadowFeatures.resize (this->m_lights.spotCount, -1);
	this->m_lights.spotShadowEnabled.resize (this->m_lights.spotCount, 0.0f);
	this->m_lights.shadowMatrices.resize (this->m_lights.shadowFeatureCount, glm::mat4 (1.0f));
	this->m_lights.shadowTransforms.resize (this->m_lights.shadowFeatureCount, glm::vec4 (0.0f));
	this->m_lights.shadowFeatureEnabled.resize (this->m_lights.shadowFeatureCount, 0.0f);
	this->m_lights.shadowViewports.resize (this->m_lights.shadowFeatureCount, glm::ivec4 (0));
	this->m_lights.tubeOriginsA.resize (this->m_lights.tubeCount, glm::vec4 (0.0f));
	this->m_lights.tubeOriginsB.resize (this->m_lights.tubeCount, glm::vec4 (0.0f));
	this->m_lights.tubeColors.resize (this->m_lights.tubeCount, glm::vec4 (0.0f));

	int directional = 0;
	int point = 0;
	int spot = 0;
	int directionalFeature = 0;
	int spotFeature = this->m_lights.directionalShadowCount * 3;
	for (const auto& object : scene->objects) {
	    if (!object->is<Data::Model::Light> ()) {
		continue;
	    }

	    const auto* light = object->as<Data::Model::Light> ();
	    if (light->type == LightData::Type_Directional) {
		if (light->castShadow) {
		    this->m_lights.directionalShadowFeatures[directional]
			= glm::ivec3 (directionalFeature, directionalFeature + 1, directionalFeature + 2);
		    directionalFeature += 3;
		    if (directional < 32) {
			this->m_lights.directionalShadowMask |= uint32_t (1) << directional;
		    }
		}
		directional++;
	    } else if (light->type == LightData::Type_Point) {
		if (light->castShadow && point < 32) {
		    this->m_lights.pointShadowMask |= uint32_t (1) << point;
		}
		point++;
	    } else if (light->type == LightData::Type_Spot) {
		if (light->castShadow) {
		    this->m_lights.spotShadowFeatures[spot] = spotFeature++;
		    if (spot < 32) {
			this->m_lights.spotShadowMask |= uint32_t (1) << spot;
		    }
		}
		spot++;
	    }
	}

	if (this->m_lights.shadowViewCount > 0) {
	    int grid = static_cast<int> (std::ceil (std::sqrt (this->m_lights.shadowViewCount)));
	    std::vector<glm::ivec2> pointBlocks (this->m_lights.pointCount, glm::ivec2 (-1));
	    std::vector<glm::ivec2> featureCells (this->m_lights.shadowFeatureCount, glm::ivec2 (-1));

	    while (true) {
		std::vector<bool> occupied (grid * grid, false);
		const auto reserveBlock = [&occupied, grid] (const int width, const int height)
		    -> std::optional<glm::ivec2> {
		    for (int y = 0; y <= grid - height; y++) {
			for (int x = 0; x <= grid - width; x++) {
			    bool available = true;
			    for (int row = 0; row < height && available; row++) {
				for (int column = 0; column < width; column++) {
				    available = !occupied[(y + row) * grid + x + column];
				    if (!available) {
					break;
				    }
				}
			    }
			    if (!available) {
				continue;
			    }
			    for (int row = 0; row < height; row++) {
				for (int column = 0; column < width; column++) {
				    occupied[(y + row) * grid + x + column] = true;
				}
			    }
			    return glm::ivec2 (x, y);
			}
		    }
		    return std::nullopt;
		};

		bool packed = true;
		for (int pointIndex = 0; pointIndex < this->m_lights.pointCount && pointIndex < 32; pointIndex++) {
		    if ((this->m_lights.pointShadowMask & (uint32_t (1) << pointIndex)) == 0) {
			continue;
		    }
		    const auto block = reserveBlock (2, 3);
		    if (!block.has_value ()) {
			packed = false;
			break;
		    }
		    pointBlocks[pointIndex] = *block;
		}
		for (int feature = 0; packed && feature < this->m_lights.shadowFeatureCount; feature++) {
		    const auto cell = reserveBlock (1, 1);
		    if (!cell.has_value ()) {
			packed = false;
			break;
		    }
		    featureCells[feature] = *cell;
		}
		if (packed) {
		    break;
		}
		grid++;
	    }

	    const int tileSize = SHADOW_ATLAS_SIZE / glm::max (grid, 1);
	    for (int pointIndex = 0; pointIndex < this->m_lights.pointCount; pointIndex++) {
		const glm::ivec2 block = pointBlocks[pointIndex];
		if (block.x < 0) {
		    continue;
		}
		this->m_lights.pointShadowTransforms[pointIndex] = glm::vec4 (
		    static_cast<float> (block.x * tileSize) / SHADOW_ATLAS_SIZE,
		    static_cast<float> (block.y * tileSize) / SHADOW_ATLAS_SIZE,
		    static_cast<float> (tileSize * 2) / SHADOW_ATLAS_SIZE,
		    static_cast<float> (tileSize * 3) / SHADOW_ATLAS_SIZE
		);
		for (int face = 0; face < 6; face++) {
		    this->m_lights.pointShadowViewports[pointIndex][face] = glm::ivec4 (
			(block.x + face % 2) * tileSize, (block.y + face / 2) * tileSize, tileSize, tileSize
		    );
		}
	    }
	    for (int feature = 0; feature < this->m_lights.shadowFeatureCount; feature++) {
		const glm::ivec2 cell = featureCells[feature];
		const glm::ivec4 viewport (
		    cell.x * tileSize + SHADOW_ATLAS_GUARD, cell.y * tileSize + SHADOW_ATLAS_GUARD,
		    tileSize - SHADOW_ATLAS_GUARD * 2, tileSize - SHADOW_ATLAS_GUARD * 2
		);
		this->m_lights.shadowViewports[feature] = viewport;
		this->m_lights.shadowTransforms[feature] = glm::vec4 (
		    static_cast<float> (viewport.x) / SHADOW_ATLAS_SIZE,
		    static_cast<float> (viewport.y) / SHADOW_ATLAS_SIZE,
		    static_cast<float> (viewport.z) / SHADOW_ATLAS_SIZE,
		    static_cast<float> (viewport.w) / SHADOW_ATLAS_SIZE
		);
	    }
	}
    } else {
	// TODO: CONVERSION
	this->m_camera->setOrthogonalProjection (width, height);
    }

    // setup framebuffers here as they're required for the scene setup;
    // 3D scenes depth-test their models, so their scene framebuffer carries a depth buffer
    this->setupFramebuffers (isPerspective);

    const uint32_t sceneWidth = this->m_camera->getWidth ();
    const uint32_t sceneHeight = this->m_camera->getHeight ();

    if (this->m_lights.shadowViewCount > 0) {
	this->_rt_shadowAtlas = this->create (
	    "_rt_shadowAtlas", TextureFormat_ARGB8888, TextureFlags_ClampUVsBorder, 1.0,
	    { SHADOW_ATLAS_SIZE, SHADOW_ATLAS_SIZE }, { SHADOW_ATLAS_SIZE, SHADOW_ATLAS_SIZE }, false, true
	);
    } else {
	// Keep the legacy placeholder available for shaders that declare the atlas but
	// compile their shadow combo out.
	this->_rt_shadowAtlas = this->create (
	    "_rt_shadowAtlas", TextureFormat_ARGB8888, TextureFlags_ClampUVs, 1.0,
	    { sceneWidth, sceneHeight }, { sceneWidth, sceneHeight }
	);
    }
    // Cookies are ordinary color textures and cannot alias the depth-comparison atlas.
    this->create (
	"_alias_lightCookie", TextureFormat_ARGB8888, TextureFlags_ClampUVs, 1.0, { 1, 1 }, { 1, 1 }
    );

    // set clear color
    const glm::vec3 clearColor = scene->colors.clear->value->getVec3 ();

    glClearColor (clearColor.r, clearColor.g, clearColor.b, 1.0f);

    // create all objects based off their dependencies
    for (const auto& object : scene->objects) {
	this->createObject (*object);
    }

    // copy over objects by render order
    for (const auto& object : scene->objects) {
	this->addObjectToRenderOrder (*object);
    }

    // create extra framebuffers for the bloom effect
    this->_rt_4FrameBuffer = this->create (
	"_rt_4FrameBuffer", TextureFormat_ARGB8888, TextureFlags_ClampUVs, 1.0, { sceneWidth / 4, sceneHeight / 4 },
	{ sceneWidth / 4, sceneHeight / 4 }
    );
    this->_rt_8FrameBuffer = this->create (
	"_rt_8FrameBuffer", TextureFormat_ARGB8888, TextureFlags_ClampUVs, 1.0, { sceneWidth / 8, sceneHeight / 8 },
	{ sceneWidth / 8, sceneHeight / 8 }
    );
    this->_rt_Bloom = this->create (
	"_rt_Bloom", TextureFormat_ARGB8888, TextureFlags_ClampUVs, 1.0, { sceneWidth / 8, sceneHeight / 8 },
	{ sceneWidth / 8, sceneHeight / 8 }
    );

    //
    // Had to get a little creative with the effects to achieve the same bloom effect without any custom code
    // this custom image loads some effect files from the virtual container to achieve the same bloom effect
    // this approach requires of two extra draw calls due to the way the effect works in official WPE
    // (it renders directly to the screen, whereas here we never do that from a scene)
    //

    const auto bloomOrigin = glm::vec3 { sceneWidth / 2, sceneHeight / 2, 0.0f };
    const auto bloomSize = glm::vec2 { sceneWidth, sceneHeight };

    const JSON bloom
	= { { "image", "models/wpenginelinux.json" },
	    { "name", "bloomimagewpenginelinux" },
	    { "visible", true },
	    { "scale", "1.0 1.0 1.0" },
	    { "angles", "0.0 0.0 0.0" },
	    { "origin",
	      std::to_string (bloomOrigin.x) + " " + std::to_string (bloomOrigin.y) + " "
		  + std::to_string (bloomOrigin.z) },
	    { "size", std::to_string (bloomSize.x) + " " + std::to_string (bloomSize.y) },
	    { "id", -1 },
	    { "effects",
	      JSON::array (
		  { { { "file", "effects/wpenginelinux/bloomeffect.json" },
		      { "id", 15242000 },
		      { "name", "" },
		      { "passes",
			JSON::array (
			    { { { "constantshadervalues",
				  { { "bloomstrength", this->getScene ().camera.bloom.strength->value->getFloat () },
				    { "bloomthreshold",
				      this->getScene ().camera.bloom.threshold->value->getFloat () } } } },
			      { { "constantshadervalues",
				  { { "bloomstrength", this->getScene ().camera.bloom.strength->value->getFloat () },
				    { "bloomthreshold",
				      this->getScene ().camera.bloom.threshold->value->getFloat () } } } },
			      { { "constantshadervalues",
				  { { "bloomstrength", this->getScene ().camera.bloom.strength->value->getFloat () },
				    { "bloomthreshold",
				      this->getScene ().camera.bloom.threshold->value->getFloat () } } } } }
			) } } }
	      ) } };

    // create image for bloom passes
    // the bloom helper is an orthographic full-scene quad; 3D scenes use the HDR bloom
    // pipeline instead, which is not implemented yet
    if (isPerspective && scene->camera.bloom.enabled->value->getBool ()) {
	sLog.error ("Bloom is not supported on 3D scenes yet, ignoring");
    } else if (scene->camera.bloom.enabled->value->getBool ()) {
	this->m_bloomObjectData = ObjectParser::parse (bloom, scene->project);
	this->m_bloomObject = this->createObject (*this->m_bloomObjectData);

	this->m_objectsByRenderOrder.push_back (this->m_bloomObject);
    }
}

CScene::~CScene () {
    // bloom object is in the objects list, so no need to explicitly delete it
    this->m_bloomObject = nullptr;

    for (const auto& val : this->m_objects | std::views::values) {
	delete val;
    }

    this->m_objectsByRenderOrder.clear ();
    this->m_objects.clear ();
}

Render::CObject* CScene::createObject (const Object& object) {
    Render::CObject* renderObject = nullptr;

    // ensure the item is not loaded already
    if (const auto current = this->m_objects.find (object.id); current != this->m_objects.end ()) {
	return current->second;
    }

    if (this->m_objectsInCreation.contains (object.id)) {
	return nullptr;
    }

    this->m_objectsInCreation.insert (object.id);
    WallpaperEngine::Data::Utils::ScopeGuard creationGuard ([this, &object] {
	this->m_objectsInCreation.erase (object.id);
    });

    std::vector<const Object*> deferredDependencies;

    // check dependencies too!
    for (const auto& cur : object.dependencies) {
	// self-dependency is a possibility...
	if (cur == object.id) {
	    continue;
	}

	const auto dep
	    = std::ranges::find_if (this->getScene ().objects, [&cur] (const auto& o) { return o->id == cur; });

	if (dep != this->getScene ().objects.end ()) {
	    const auto& depObject = **dep;
	    if (depObject.parent.has_value () && depObject.parent.value () == object.id) {
		deferredDependencies.push_back (&depObject);
		continue;
	    }

	    this->createObject (**dep);
	}
    }

    // check if the item has any parent and also create it first
    if (object.parent.has_value ()) {
	int parentId = object.parent.value ();

	const auto dep = std::ranges::find_if (this->getScene ().objects, [&parentId] (const auto& o) {
	    return o->id == parentId;
	});

	if (dep == this->getScene ().objects.end ()) {
	    sLog.exception ("Cannot find parent ", parentId, " for object ", object.id);
	}

	this->createObject (**dep);
    }

    renderObject = this->dispatchObjectType (object);

    if (renderObject != nullptr) {
	this->m_objects.emplace (renderObject->getId (), renderObject);
    }

    for (const auto* deferred : deferredDependencies) {
	this->createObject (*deferred);
    }

    return renderObject;
}

Render::CObject* CScene::dispatchObjectType (const Object& object) {
    Render::CObject* renderObject = nullptr;

    if (object.is<Image> ()) {
	renderObject = new Objects::CImage (*this, *object.as<Image> ());
    } else if (object.is<Sound> ()) {
	renderObject = new Objects::CSound (*this, *object.as<Sound> ());
    } else if (object.is<Text> ()) {
	renderObject = new Objects::CText (*this, *object.as<Text> ());
    } else if (object.is<Data::Model::Model3D> ()) {
	renderObject = new Objects::CModel (*this, *object.as<Data::Model::Model3D> ());
    } else if (object.is<Data::Model::Light> ()) {
	auto* light = new Objects::CLight (*this, *object.as<Data::Model::Light> ());
	this->m_lightObjects.push_back (light);
	renderObject = light;
    } else if (object.is<Particle> ()) {
	const auto& particleData = *object.as<Particle> ();

	if (this->getContext ().getApp ().getContext ().settings.general.disableParticles == true) {
	    sLog.debug ("Ignoring particle system (disabled in settings): ", particleData.name);
	    return nullptr;
	}

	renderObject = new Objects::CParticle (*this, particleData);
    } else {
	// containers/groups: no rendering of their own, but their transform properties can be
	// script-driven (e.g. orbital mechanics groups), so they must register with the script engine
	renderObject = new Scripting::ScriptableObject (*this, object);
    }

    try {
	renderObject->setup ();
    } catch (const std::exception& e) {
	sLog.error ("Failed to setup object ", object.id, ": ", e.what ());
	delete renderObject;
	renderObject = nullptr;
    }

    return renderObject;
}

void CScene::addObjectToRenderOrder (const Object& object) {
    const auto obj = this->m_objects.find (object.id);

    // ignores not created objects like particle systems
    if (obj == this->m_objects.end ()) {
	return;
    }

    // take into account any dependency first
    for (const auto& dep : object.dependencies) {
	// self-dependency is possible
	if (dep == object.id) {
	    continue;
	}

	// add the dependency to the list if it's created
	auto depIt = std::ranges::find_if (this->getScene ().objects, [&dep] (const auto& o) { return o->id == dep; });

	if (depIt != this->getScene ().objects.end ()) {
	    this->addObjectToRenderOrder (**depIt);
	} else {
	    sLog.error ("Cannot find dependency ", dep, " for object ", object.id);
	}
    }

    // ensure we're added only once to the render list
    const auto renderIt = std::ranges::find_if (this->m_objectsByRenderOrder, [&object] (const auto& o) {
	return o->getId () == object.id;
    });

    if (renderIt == this->m_objectsByRenderOrder.end ()) {
	this->m_objectsByRenderOrder.emplace_back (obj->second);
    }
}

ScriptEngine& CScene::getScriptEngine () const { return *this->m_scriptEngine; }
Camera& CScene::getCamera () const { return *this->m_camera; }
const CScene::SceneFog& CScene::getFog () const { return this->m_fog; }

void CScene::renderFrame (const glm::ivec4& viewport) {
    // Advance the active path before scripts read cursorWorldPosition and before
    // any object renders. Script-driven camera visibility from the previous tick
    // selects the source, matching the normal one-frame property update cadence.
    this->updateCameraPath (glm::max (0.0f, this->getDeltaTime ()));

    // ensure the virtual mouse position is up to date
    this->updateMouse (viewport);

    // update the parallax position if required
    if (this->getScene ().camera.parallax.enabled->value->getBool ()
	&& !this->getContext ().getApp ().getContext ().settings.mouse.disableparallax) {
	const float influence = this->getScene ().camera.parallax.mouseInfluence->value->getFloat ();
	const float delay = this->getScene ().camera.parallax.delay->value->getFloat ();
	const float alpha = calculateParallaxSmoothingAlpha (delay, g_Time - g_TimeLast);

	// per-object depth and the global parallax amount are applied by each renderable,
	// this only tracks the smoothed mouse offset (-1 to 1 across the screen) scaled by
	// influence; only y is negated because m_mousePosition already has y flipped
	// relative to x by the viewport UV mapping in updateMouse
	const glm::vec2 centeredMouse = {
	    this->m_mousePosition.x * 2.0f - 1.0f,
	    1.0f - this->m_mousePosition.y * 2.0f,
	};
	this->m_parallaxDisplacement = glm::mix (this->m_parallaxDisplacement, centeredMouse * influence, alpha);
	// Shader-driven parallax effects consume the influenced mouse position directly.
	// cameraparallaxamount belongs only to whole-layer camera translation; multiplying the
	// shader input by it suppresses depth-map-only scenes whose camera amount is zero.
	this->m_parallaxPosition = calculateShaderParallaxPosition (this->m_parallaxDisplacement);
    }

    // run a tick in the javascript logic
    this->getScriptEngine ().tick ();

    // Fog colors and ranges can be SceneScript- or user-property-driven.
    this->updateFogState ();

    // refresh light uniform state after scripts have moved the lights
    this->updateLightState ();

    // Render light-space depth before any material pass samples _rt_shadowAtlas.
    this->renderShadowAtlas ();

    // update every cached texture instead of walking image objects: textures that
    // are only referenced as effect/pass inputs have no CImage driving them, so
    // video-backed effect inputs never decoded a single frame (upstream c3f526f)
#if !NDEBUG
    glPushDebugGroup (GL_DEBUG_SOURCE_APPLICATION, 0, -1, "Updating textures");
#endif

    this->getContext ().updateAllTextures ();

#if !NDEBUG
    glPopDebugGroup ();
#endif

    // bind the vertex array
    glBindVertexArray (this->m_vaoBuffer);
    // use the scene's framebuffer by default
    glBindFramebuffer (GL_FRAMEBUFFER, this->getWallpaperFramebuffer ());
    // ensure we render over the whole framebuffer
    glViewport (0, 0, this->m_sceneFBO->getRealWidth (), this->m_sceneFBO->getRealHeight ());

    // passes with depthwrite disabled leave the mask off, which would block the depth clear
    glDepthMask (true);
    // the clear must write all channels: a leaked alpha-disabled color mask would silently keep the
    // framebuffer's stale alpha, which every alpha-blended effect writeback then composites against
    glColorMask (true, true, true, true);
    glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const std::vector<CObject*> renderOrder = this->buildFrameRenderOrder ();
    for (const auto& cur : renderOrder) {
	const auto& debug = this->getContext ().getApp ().getContext ().settings.render.debug;
	if (debug.objectFilter.has_value () && cur->getId () != debug.objectFilter.value ()) {
	    continue;
	}
	if (std::ranges::find (debug.skipObjects, cur->getId ()) != debug.skipObjects.end ()) {
	    continue;
	}

	cur->render ();
    }
}

float CScene::calculateParallaxSmoothingAlpha (const float delay, const float deltaTime) {
    if (delay <= 0.0f) {
	return 1.0f;
    }

    return glm::clamp ((1.0f - delay / PARALLAX_DELAY_LIMIT) * PARALLAX_DELAY_RATE * deltaTime, 0.0f, 1.0f);
}

glm::vec2 CScene::calculateShaderParallaxPosition (const glm::vec2& displacement) {
    // displacement uses [-1,1], whereas g_ParallaxPosition uses [0,1].
    return glm::vec2 (0.5f) + displacement * 0.5f;
}

glm::vec4
CScene::calculateFogParams (const float start, const float end, const float startDensity, const float endDensity) {
    return glm::vec4 (start, end - start, startDensity, endDensity - startDensity);
}

void CScene::registerFogScripts () {
    const auto queue = [this] (const std::string& key, const UserSettingUniquePtr& setting) {
	this->m_scriptEngine->queueSceneScript (key + "_scene", *setting->value);
    };
    const auto queueLayer = [&queue] (const std::string& prefix, const SceneData::Fog::Layer& layer) {
	queue (prefix, layer.enabled);
	queue (prefix + "color", layer.color);
	queue (prefix + "start", layer.start);
	queue (prefix + "end", layer.end);
	queue (prefix + "startdensity", layer.startDensity);
	queue (prefix + "enddensity", layer.endDensity);
    };

    queueLayer ("fogdistance", this->getScene ().fog.distance);
    queueLayer ("fogheight", this->getScene ().fog.height);
}

void CScene::updateFogState () {
    const auto& fog = this->getScene ().fog;
    const bool perspective = this->getScene ().camera.projection.isPerspective;
    this->m_fog.distanceEnabled = perspective && fog.distance.enabled->value->getBool ();
    this->m_fog.heightEnabled = perspective && fog.height.enabled->value->getBool ();
    this->m_fog.distanceParams = calculateFogParams (
	fog.distance.start->value->getFloat (), fog.distance.end->value->getFloat (),
	fog.distance.startDensity->value->getFloat (), fog.distance.endDensity->value->getFloat ()
    );
    this->m_fog.heightParams = calculateFogParams (
	fog.height.start->value->getFloat (), fog.height.end->value->getFloat (),
	fog.height.startDensity->value->getFloat (), fog.height.endDensity->value->getFloat ()
    );
}

float CScene::calculateCameraFadeAlpha (const float elapsedTime, const float duration) {
    if (duration <= 0.0f) {
	return 0.0f;
    }

    const float remaining = glm::max (duration - elapsedTime, 0.0f);
    // Reference order matters for shots shorter than one second: the end
    // envelope wins when it overlaps the opening envelope.
    if (remaining < CAMERA_FADE_DURATION) {
	return glm::clamp (1.0f - remaining / CAMERA_FADE_DURATION, 0.0f, 1.0f);
    }
    if (elapsedTime < CAMERA_FADE_DURATION) {
	return glm::clamp (1.0f - elapsedTime / CAMERA_FADE_DURATION, 0.0f, 1.0f);
    }
    return 0.0f;
}

float CScene::getSceneFadeAlpha () const {
    if (!this->getScene ().camera.fade->value->getBool () || this->m_activeCameraPathSource == nullptr
	|| !this->m_activeCameraPathIndex.has_value ()) {
	return 0.0f;
    }

    const CameraPath& path = this->m_activeCameraPathSource->paths[*this->m_activeCameraPathIndex];
    return calculateCameraFadeAlpha (this->m_cameraPathElapsed, path.duration);
}

size_t CScene::chooseCameraPathIndex (
    const std::optional<size_t> current, const size_t count, const std::string& queueMode,
    const uint32_t randomValue
) {
    if (count <= 1) {
	return 0;
    }
    if (queueMode != "random") {
	return current.has_value () ? (*current + 1) % count : 0;
    }
    if (!current.has_value ()) {
	return randomValue % count;
    }

    // Random queues do not immediately repeat the shot that just ended.
    size_t candidate = randomValue % (count - 1);
    if (candidate >= *current) {
	candidate++;
    }
    return candidate;
}

std::vector<size_t>
CScene::calculateTransparentSortPermutation (const std::vector<TransparentSortKey>& keys) {
    std::vector<size_t> permutation (keys.size ());
    std::iota (permutation.begin (), permutation.end (), 0);

    std::vector<size_t> sortableSlots;
    std::vector<size_t> sortedEntries;
    for (size_t index = 0; index < keys.size (); index++) {
	if (keys[index].sortable) {
	    sortableSlots.push_back (index);
	    sortedEntries.push_back (index);
	}
    }

    std::stable_sort (sortedEntries.begin (), sortedEntries.end (), [&keys] (const size_t left, const size_t right) {
	const TransparentSortKey& lhs = keys[left];
	const TransparentSortKey& rhs = keys[right];
	if (lhs.renderClass != rhs.renderClass) {
	    return lhs.renderClass < rhs.renderClass;
	}
	if (lhs.renderClass == RenderSortClass::Opaque) {
	    return false;
	}
	const float leftDepth = std::isfinite (lhs.cameraDepth) ? lhs.cameraDepth : 0.0f;
	const float rightDepth = std::isfinite (rhs.cameraDepth) ? rhs.cameraDepth : 0.0f;
	return leftDepth < rightDepth;
    });

    for (size_t index = 0; index < sortableSlots.size (); index++) {
	permutation[sortableSlots[index]] = sortedEntries[index];
    }
    return permutation;
}

std::vector<CObject*> CScene::buildFrameRenderOrder () const {
    if (!this->getScene ().camera.projection.isPerspective || !this->getScene ().transparentSorting
	|| this->getScene ().customSortOrder) {
	return this->m_objectsByRenderOrder;
    }

    std::vector<TransparentSortKey> keys;
    keys.reserve (this->m_objectsByRenderOrder.size ());
    const glm::mat4& view = this->m_camera->getLookAt ();
    for (const CObject* object : this->m_objectsByRenderOrder) {
	const bool sortable = object->getObject ().is<Model3D> ();
	float cameraDepth = 0.0f;
	if (sortable) {
	    cameraDepth = (view * object->resolveWorldMatrix () * glm::vec4 (0.0f, 0.0f, 0.0f, 1.0f)).z;
	}
	keys.push_back (TransparentSortKey {
	    .sortable = sortable,
	    .renderClass = sortable ? object->getRenderSortClass () : RenderSortClass::Opaque,
	    .cameraDepth = cameraDepth,
	});
    }

    const std::vector<size_t> permutation = calculateTransparentSortPermutation (keys);
    std::vector<CObject*> result = this->m_objectsByRenderOrder;
    for (size_t index = 0; index < permutation.size (); index++) {
	result[index] = this->m_objectsByRenderOrder[permutation[index]];
    }
    return result;
}

const CameraPathSource* CScene::findActiveCameraPathSource () const {
    const CameraPathSource* active = nullptr;
    for (const CameraPathSource& source : this->getScene ().camera.paths) {
	if (!source.objectId.has_value ()) {
	    active = &source;
	    continue;
	}

	const CObject* object = this->getObject (*source.objectId);
	if (object != nullptr && object->getObject ().groupVisible->value->getBool ()) {
	    // Later visible camera objects override earlier/default cameras.
	    active = &source;
	}
    }
    return active;
}

void CScene::updateCameraPath (const float deltaTime) {
    const CameraPathSource* source = this->findActiveCameraPathSource ();
    if (source != this->m_activeCameraPathSource) {
	this->m_activeCameraPathSource = source;
	this->m_activeCameraPathIndex = std::nullopt;
	this->m_cameraPathElapsed = 0.0f;
	if (source == nullptr || source->paths.empty ()) {
	    this->m_camera->resetTransform ();
	    return;
	}
	this->m_activeCameraPathIndex = chooseCameraPathIndex (
	    std::nullopt, source->paths.size (), source->queueMode, this->m_cameraPathRandom ()
	);
    }

    if (source == nullptr || source->paths.empty () || !this->m_activeCameraPathIndex.has_value ()) {
	return;
    }

    this->m_cameraPathElapsed += deltaTime;
    // A stalled frame can cross multiple very short shots. Bound the loop by
    // the queue length plus one so malformed assets can never spin forever.
    for (size_t step = 0; step <= source->paths.size (); step++) {
	const CameraPath& current = source->paths[*this->m_activeCameraPathIndex];
	if (current.duration <= 0.0f || this->m_cameraPathElapsed < current.duration) {
	    break;
	}
	this->m_cameraPathElapsed -= current.duration;
	this->m_activeCameraPathIndex = chooseCameraPathIndex (
	    this->m_activeCameraPathIndex, source->paths.size (), source->queueMode, this->m_cameraPathRandom ()
	);
    }

    const CameraPath& path = source->paths[*this->m_activeCameraPathIndex];
    this->m_camera->setTransform (path.evaluate (this->m_cameraPathElapsed, this->m_camera->getDefaultTransform ()));
}

void CScene::updateMouse (const glm::ivec4& viewport) {
    // update virtual mouse position first
    const glm::dvec2 position = this->getContext ().getInputContext ().getMouseInput ().position ();

    // rollover the position to the last
    this->m_mousePositionLast = this->m_mousePosition;

    // calculate the current position of the mouse in viewport space [0, 1]
    double mouseX = glm::clamp ((position.x - viewport.x) / viewport.z, 0.0, 1.0);
    // Normalize Y coordinate (OpenGL convention: 0=bottom, 1=top)
    // Particle code expects this convention: 0=bottom results in negative Y (down), 1=top results in positive Y (up)
    double normalizedMouseY = glm::clamp ((position.y - viewport.y) / viewport.w, 0.0, 1.0);

    // Account for UV cropping when using fill/fit scaling modes
    // The scene may be rendered larger than viewport and cropped via UVs
    const auto uvs = this->getState ().getTextureUVs ();

    // Map mouse position from viewport space to scene UV space
    // UVs define what portion of the scene texture is visible
    this->m_mousePositionNormalized.x = uvs.ustart + mouseX * (uvs.uend - uvs.ustart);
    this->m_mousePositionNormalized.y = uvs.vstart + normalizedMouseY * (uvs.vend - uvs.vstart);

    // Invert previous normalization of Y to match what the shader expects
    double mouseY = 1.0 - normalizedMouseY;

    this->m_mousePosition.x = this->m_mousePositionNormalized.x;
    this->m_mousePosition.y = uvs.vstart + mouseY * (uvs.vend - uvs.vstart);
}

const CScene::SceneLights& CScene::getLights () const { return this->m_lights; }

void CScene::updateLightState () {
    int directional = 0;
    int point = 0;
    int spot = 0;
    int tube = 0;

    for (const auto* light : this->m_lightObjects) {
	const auto& data = light->getLight ();

	if (data.type == LightData::Type_Directional && directional < this->m_lights.directionalCount) {
	    // the shader expects the direction towards the light
	    this->m_lights.directionalDirections[directional] = glm::vec4 (-light->getWorldDirection (), 0.0f);
	    this->m_lights.directionalColors[directional] = glm::vec4 (light->getPremultipliedColor (), 0.0f);
	    this->m_lights.directionalShadowEnabled[directional] = 0.0f;

	    const glm::ivec3 features = this->m_lights.directionalShadowFeatures[directional];
	    if (data.castShadow && features.x >= 0) {
		const float enabled
		    = data.groupVisible->value->getBool () && light->isVisibleThroughParents () ? 1.0f : 0.0f;
		this->m_lights.directionalShadowEnabled[directional] = enabled;

		const Camera& camera = this->getCamera ();
		const float cameraNear = std::max (camera.getNearZ (), 0.0001f);
		const float cameraFar = std::max (camera.getFarZ (), cameraNear + 0.0001f);
		glm::vec3 cascadeFar = glm::abs (data.cascadeDistances);
		cascadeFar.x = glm::clamp (cascadeFar.x, cameraNear + 0.0001f, cameraFar);
		cascadeFar.y = glm::clamp (cascadeFar.y, cascadeFar.x, cameraFar);
		cascadeFar.z = glm::clamp (cascadeFar.z, cascadeFar.y, cameraFar);

		for (int cascade = 0; cascade < 3; cascade++) {
		    const int feature = features[cascade];
		    this->m_lights.shadowMatrices[feature]
			= Objects::CLight::calculateDirectionalShadowViewProjection (
			    camera.getEye (), camera.getCenter (), camera.getUp (), camera.getFov (),
			    camera.getWidth () / std::max (camera.getHeight (), 1.0f), camera.getZoom (), cameraNear,
			    cascadeFar[cascade], light->getWorldDirection (),
			    this->m_lights.shadowViewports[feature].z
			);
		    this->m_lights.shadowFeatureEnabled[feature] = enabled;
		}
	    }
	    directional++;
	} else if (data.type == LightData::Type_Point && point < this->m_lights.pointCount) {
	    this->m_lights.pointOrigins[point]
		= glm::vec4 (light->getWorldPosition (), data.exponent->value->getFloat ());
	    this->m_lights.pointColors[point]
		= glm::vec4 (light->getPremultipliedColor (), data.radius->value->getFloat ());
	    this->m_lights.pointShadowEnabled[point] = 0.0f;
	    if (data.castShadow && point < 32 && (this->m_lights.pointShadowMask & (uint32_t (1) << point)) != 0) {
		const float radius = data.radius->value->getFloat ();
		this->m_lights.pointShadowProjections[point]
		    = Objects::CLight::calculatePointShadowProjectionInfo (radius);
		this->m_lights.pointShadowMatrices[point]
		    = Objects::CLight::calculatePointShadowViewProjections (light->getWorldPosition (), radius);
		this->m_lights.pointShadowEnabled[point]
		    = data.groupVisible->value->getBool () && light->isVisibleThroughParents () ? 1.0f : 0.0f;
	    }
	    point++;
	} else if (data.type == LightData::Type_Spot && spot < this->m_lights.spotCount) {
	    const glm::vec2 cones = Objects::CLight::calculateSpotConeCosines (
		data.innerCone->value->getFloat (), data.outerCone->value->getFloat ()
	    );
	    this->m_lights.spotOrigins[spot] = glm::vec4 (light->getWorldPosition (), cones.x);
	    this->m_lights.spotDirections[spot] = glm::vec4 (light->getWorldDirection (), cones.y);
	    this->m_lights.spotColors[spot]
		= glm::vec4 (light->getPremultipliedColor (), data.radius->value->getFloat ());
	    this->m_lights.spotExponents[spot] = glm::vec4 (data.exponent->value->getFloat (), 0.0f, 0.0f, 0.0f);

	    const int feature = this->m_lights.spotShadowFeatures[spot];
	    if (data.castShadow && feature >= 0) {
		this->m_lights.shadowMatrices[feature] = Objects::CLight::calculateSpotShadowViewProjection (
		    light->getWorldPosition (), light->getWorldDirection (), data.outerCone->value->getFloat (),
		    data.radius->value->getFloat ()
		);
		this->m_lights.spotShadowEnabled[spot]
		    = data.groupVisible->value->getBool () && light->isVisibleThroughParents () ? 1.0f : 0.0f;
		this->m_lights.shadowFeatureEnabled[feature] = this->m_lights.spotShadowEnabled[spot];
	    } else {
		this->m_lights.spotShadowEnabled[spot] = 0.0f;
	    }
	    spot++;
	} else if (data.type == LightData::Type_Tube && tube < this->m_lights.tubeCount) {
	    this->m_lights.tubeOriginsA[tube]
		= glm::vec4 (light->getWorldPosition (), data.exponent->value->getFloat ());
	    this->m_lights.tubeOriginsB[tube] = glm::vec4 (light->getTubeEndPosition (), 0.0f);
	    this->m_lights.tubeColors[tube]
		= glm::vec4 (light->getPremultipliedColor (), data.radius->value->getFloat ());
	    tube++;
	}
    }
}

void CScene::renderShadowAtlas () {
    if (this->m_lights.shadowViewCount == 0 || this->_rt_shadowAtlas == nullptr) {
	return;
    }

#if !NDEBUG
    glPushDebugGroup (GL_DEBUG_SOURCE_APPLICATION, 0, -1, "Scene shadow atlas");
#endif

    glBindFramebuffer (GL_FRAMEBUFFER, this->_rt_shadowAtlas->getFramebuffer ());
    glViewport (0, 0, SHADOW_ATLAS_SIZE, SHADOW_ATLAS_SIZE);
    glColorMask (false, false, false, false);
    glDisable (GL_BLEND);
    glEnable (GL_DEPTH_TEST);
    glDepthFunc (GL_LEQUAL);
    glDepthMask (true);
    glClearDepth (1.0);
    glClear (GL_DEPTH_BUFFER_BIT);
    glEnable (GL_POLYGON_OFFSET_FILL);
    glPolygonOffset (2.0f, 4.0f);

    for (int feature = 0; feature < this->m_lights.shadowFeatureCount; feature++) {
	if (this->m_lights.shadowFeatureEnabled[feature] < 0.5f) {
	    continue;
	}

	const glm::ivec4& viewport = this->m_lights.shadowViewports[feature];
	glViewport (viewport.x, viewport.y, viewport.z, viewport.w);

	for (auto* object : this->m_objectsByRenderOrder) {
	    if (auto* model = dynamic_cast<Objects::CModel*> (object); model != nullptr) {
		model->renderShadow (this->m_lights.shadowMatrices[feature]);
	    }
	}
    }

    for (int point = 0; point < this->m_lights.pointCount; point++) {
	if (this->m_lights.pointShadowEnabled[point] < 0.5f) {
	    continue;
	}

	for (int face = 0; face < 6; face++) {
	    const glm::ivec4& viewport = this->m_lights.pointShadowViewports[point][face];
	    glViewport (viewport.x, viewport.y, viewport.z, viewport.w);
	    for (auto* object : this->m_objectsByRenderOrder) {
		if (auto* model = dynamic_cast<Objects::CModel*> (object); model != nullptr) {
		    model->renderShadow (this->m_lights.pointShadowMatrices[point][face]);
		}
	    }
	}
    }

    glDisable (GL_POLYGON_OFFSET_FILL);
    glDisable (GL_CULL_FACE);
    glFrontFace (GL_CCW);
    glColorMask (true, true, true, true);
    glDepthMask (true);
    glUseProgram (GL_NONE);

#if !NDEBUG
    glPopDebugGroup ();
#endif
}

const Scene& CScene::getScene () const { return *this->getWallpaperData ().as<Scene> (); }

int CScene::getWidth () const { return this->m_camera->getWidth (); }

int CScene::getHeight () const { return this->m_camera->getHeight (); }

float CScene::getTime () const { return g_Time; }

float CScene::getDeltaTime () const { return g_Time - g_TimeLast; }

float CScene::getFps () const {
    const float dt = g_Time - g_TimeLast;
    // Guard against the first frame (where g_TimeLast is 0 so dt == g_Time)
    // and division by zero on the very first call.
    if (dt <= 1e-6f) {
	return 60.0f;
    }
    return 1.0f / dt;
}

const glm::vec2* CScene::getMousePosition () const { return &this->m_mousePosition; }

const glm::vec2* CScene::getMousePositionLast () const { return &this->m_mousePositionLast; }

const glm::vec2* CScene::getMousePositionNormalized () const { return &this->m_mousePositionNormalized; }

const glm::vec2* CScene::getParallaxDisplacement () const { return &this->m_parallaxDisplacement; }

const glm::vec2* CScene::getParallaxPosition () const { return &this->m_parallaxPosition; }

const std::vector<CObject*>& CScene::getObjectsByRenderOrder () const { return this->m_objectsByRenderOrder; }

const CObject* CScene::getObject (int id) const {
    const auto object = this->m_objects.find (id);
    return object == this->m_objects.end () ? nullptr : object->second;
}

Render::CObject* CScene::createLayer (const std::string& modelPath, const std::string& workshopId) {
    // Resolve the asset path: scripts reference bar models by their bare workshop-relative path
    // (e.g. "models/full-pixel.json"), but the packaged asset lives under the script's workshop id
    // ("models/workshop/<id>/full-pixel.json"). Try the bare path first, then the workshop-scoped one.
    std::string path = modelPath;
    const auto resolves = [this] (const std::string& candidate) {
	try {
	    this->getAssetLocator ().readString (candidate);
	    return true;
	} catch (const std::exception&) {
	    return false;
	}
    };

    if (!resolves (path) && !workshopId.empty ()) {
	if (const auto slash = path.find ('/'); slash != std::string::npos) {
	    std::string scoped
		= path.substr (0, slash + 1) + "workshop/" + workshopId + "/" + path.substr (slash + 1);
	    if (resolves (scoped)) {
		path = std::move (scoped);
	    }
	}
    }

    // Allocate a fresh id above everything currently known (live objects + parse-time objects), so it
    // never collides with an existing key in m_objects or a not-yet-instantiated dependency.
    int newId = 0;
    for (const auto& id : this->m_objects | std::views::keys) {
	newId = std::max (newId, id);
    }
    for (const auto& object : this->getScene ().objects) {
	newId = std::max (newId, object->id);
    }
    ++newId;

    const glm::vec3 origin = { this->m_camera->getWidth () / 2.0f, this->m_camera->getHeight () / 2.0f, 0.0f };

    const JSON layer = {
	{ "image", path },
	{ "name", "runtime-layer-" + std::to_string (newId) },
	{ "visible", true },
	{ "scale", "1.0 1.0 1.0" },
	{ "angles", "0.0 0.0 0.0" },
	{ "origin", std::to_string (origin.x) + " " + std::to_string (origin.y) + " " + std::to_string (origin.z) },
	{ "id", newId },
    };

    try {
	ObjectUniquePtr data = ObjectParser::parse (layer, this->getScene ().project);
	Object* raw = data.get ();
	this->m_runtimeLayerData.push_back (std::move (data));

	Render::CObject* renderObject = this->createObject (*raw);
	if (renderObject == nullptr) {
	    return nullptr;
	}

	this->m_objectsByRenderOrder.push_back (renderObject);
	return renderObject;
    } catch (const std::exception& e) {
	sLog.error ("createLayer failed for ", modelPath, ": ", e.what ());
	return nullptr;
    }
}

int CScene::getScriptableLayerIndex (const CObject* layer) const {
    int index = 0;
    for (const auto* object : this->m_objectsByRenderOrder) {
	if (object == nullptr || !object->is<Scripting::ScriptableObject> ()) {
	    continue;
	}
	if (object == layer) {
	    return index;
	}
	++index;
    }
    return -1;
}

void CScene::moveLayerToScriptableIndex (CObject* layer, int index) {
    auto& order = this->m_objectsByRenderOrder;
    const auto current = std::ranges::find (order, layer);
    if (current == order.end ()) {
	return;
    }
    order.erase (current);

    // Insert just before the index-th scriptable layer; a negative / past-the-end index appends,
    // leaving the layer on top of the render order.
    auto insertPos = order.end ();
    if (index >= 0) {
	int scriptIndex = 0;
	for (auto it = order.begin (); it != order.end (); ++it) {
	    if (*it == nullptr || !(*it)->is<Scripting::ScriptableObject> ()) {
		continue;
	    }
	    if (scriptIndex == index) {
		insertPos = it;
		break;
	    }
	    ++scriptIndex;
	}
    }
    order.insert (insertPos, layer);
}
