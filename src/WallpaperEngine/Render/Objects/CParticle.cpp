#include "CParticle.h"

#include "WallpaperEngine/Data/Model/Property.h"
#include "WallpaperEngine/Data/Parsers/ObjectParser.h"
#include "WallpaperEngine/Logging/Log.h"
#include "WallpaperEngine/Maths.h"
#include "WallpaperEngine/Render/Utils/NoiseUtils.h"

#include <GL/glew.h>
#include <algorithm>
#include <cmath>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <limits>

extern float g_Time;

using namespace WallpaperEngine::Render::Objects;
using namespace WallpaperEngine::Render::Utils;
using namespace WallpaperEngine::Data::Model;

float WallpaperEngine::Render::Objects::calculateParticleSimulationDelta (const float elapsed, const float rate) {
    return glm::max (elapsed, 0.0f) * glm::max (rate, 0.0f);
}

float WallpaperEngine::Render::Objects::calculateParticleEmissionRate (const float emitterRate, const float count) {
    return glm::max (emitterRate, 0.0f) * glm::max (count, 0.0f);
}

float WallpaperEngine::Render::Objects::calculateParticleAudioResponse (
    const float* left, const float* right, const int mode, const glm::vec2& bounds,
    const float exponent, const int frequencyStart, const int frequencyEnd
) {
    if (mode == 0) return 1.0f;
    // Native 14022a8a0 uses the strongest selected band. Stereo averages
    // each pair before taking that maximum, rather than averaging the range.
    float peak = 0.0f;
    for (int band = std::clamp (frequencyStart, 0, 15); band <= std::clamp (frequencyEnd, 0, 15); ++band) {
	const float value = mode == 1 ? left[band] : mode == 2 ? right[band]
	    : mode == 3 ? (left[band] + right[band]) * 0.5f : 0.0f;
	peak = std::max (peak, value);
    }
    const float mapped = (peak - bounds.x) / (bounds.y - bounds.x);
    const float value = mapped < 0.0f ? 0.0f : mapped < 1.0f ? mapped : 1.0f;
    const float response = std::pow ((3.0f - 2.0f * value) * value * value, exponent);
    return response < 0.0f ? 0.0f : response < 1.0f ? response : 1.0f;
}

void WallpaperEngine::Render::Objects::initializeParticleBetweenControlPoints (
    ParticleInstance& particle, const MapSequenceBetweenControlPointsInitializer& initializer,
    const glm::vec3& start, const glm::vec3& end, float& phase, float& direction
) {
    // Native initializer opcode 14 (14023ca93) preserves only the emitter's
    // displacement perpendicular to the control-point segment.
    const glm::vec3 segment = end - start;
    const float distance = glm::length (segment);
    const glm::vec3 axis = distance > 0.0f ? segment / distance : glm::vec3 (0.0f);
    glm::vec3 displacement = particle.position - axis * glm::dot (particle.position, axis);
    const float weight = 1.0f - std::pow (std::abs (phase * 2.0f - 1.0f), 2.0f);
    if ((initializer.flags & 1) != 0) displacement *= weight;
    const glm::vec2 bounds = initializer.bounds->value->getVec2 ();
    particle.position = start + segment * glm::mix (bounds.x, bounds.y, phase) + displacement;
    if ((initializer.flags & 8) != 0) {
	const glm::vec3 arcDirection (initializer.arcDirection.x, -initializer.arcDirection.y,
				    initializer.arcDirection.z);
	particle.position += arcDirection * (weight * distance * initializer.arcAmount);
    }
    if ((initializer.flags & 2) != 0) particle.velocity *= weight;
    if ((initializer.flags & 4) != 0) {
	particle.size *= 1.0f - initializer.sizeReductionAmount + initializer.sizeReductionAmount * weight;
	particle.initial.size = particle.size;
    }

    // Both endpoints are samples: unlike the circular initializer, the step
    // is 1/(count-1). Native repeat resets to zero; mirror reflects overshoot.
    phase += direction / std::max (0.0001f, initializer.count->value->getFloat () - 1.0f);
    if (phase > 1.0f) {
	if (initializer.limitBehavior == "mirror") {
	    phase = 2.0f - phase;
	    direction = -direction;
	} else {
	    phase = 0.0f;
	}
    } else if (phase < 0.0f) {
	phase = -phase;
	direction = -direction;
    }
}

glm::vec3
WallpaperEngine::Render::Objects::convertParticleRotationForRender (
    const glm::vec3& rotation, const bool preserveZ
) {
    // Particle definitions use Wallpaper Engine's Y-down scene space. Reflection across
    // Y changes the handedness of axial rotation vectors, so X and Z change sign.
    // Perspective billboards and upright 2D sprite tangents already account
    // for that reflection. Preserve native Z spin (1402308a0 uploads it unchanged).
    return { -rotation.x, rotation.y, preserveZ ? rotation.z : -rotation.z };
}

float WallpaperEngine::Render::Objects::calculateRopeTrailVisualValue (
    const float currentValue, const float trailPosition, const bool fadeAlongTrail
) {
    return currentValue * (fadeAlongTrail ? std::clamp (trailPosition, 0.0f, 1.0f) : 1.0f);
}

glm::vec3 WallpaperEngine::Render::Objects::calculateControlPointAttraction (
    const glm::vec3& toCenter, const float strength, const float radius, const float deltaTime,
    const bool limitToDistance, const float lifetimeWeight
) {
    const float distance = glm::length (toCenter);
    if (distance <= std::numeric_limits<float>::min () || distance >= radius) {
	return glm::vec3 (0.0f);
    }
    // The native controlpointattract operator fades linearly over the complete
    // threshold radius. A constant force in half that radius makes flocks turn
    // abruptly and changes the shape of magic vortices.
    const float falloff = 1.0f - distance / radius;
    float magnitude = strength * deltaTime * falloff;
    // Native flags default to 2: cap positive attraction before lifetime blending.
    // Negative strength remains an uncapped repulsion.
    if (limitToDistance) magnitude = std::min (magnitude, distance);
    return toCenter * (magnitude * lifetimeWeight / distance);
}

glm::vec3 WallpaperEngine::Render::Objects::resolveParticleControlPoint (
    const glm::vec3& offset, const glm::mat4& worldToLocal, const bool worldSpace
) {
    // Native 14022bd40/14022a070: instance overrides replace the reference
    // position without changing its space. World points use the full inverse
    // layer transform; local points remain relative to the emitter.
    const glm::vec3 local = worldSpace ? glm::vec3 (worldToLocal * glm::vec4 (offset, 1.0f)) : offset;
    if (!std::isfinite (local.x) || !std::isfinite (local.y) || !std::isfinite (local.z)) {
	return glm::vec3 (0.0f); // A zero-scale layer has no invertible coordinate space.
    }
    return { local.x, -local.y, local.z };
}

glm::mat3 WallpaperEngine::Render::Objects::calculateFixedParticleOrientation (
    const glm::vec3& axis, const glm::mat3& model, const bool worldSpace
) {
    const auto normalize = [] (const glm::vec3& value, const glm::vec3& fallback) {
	const float lengthSquared = glm::dot (value, value);
	return std::isfinite (lengthSquared) && lengthSquared > 0.0f
	    ? value / std::sqrt (lengthSquared) : fallback;
    };
    const glm::vec3 forward = normalize (axis, { 0.0f, 1.0f, 0.0f });
    glm::vec3 up (0.0f, 0.0f, -1.0f);
    if (forward.x != 0.0f || forward.z != 0.0f) {
	up = normalize (glm::cross (forward, glm::cross (glm::vec3 (0.0f, 1.0f, 0.0f), forward)), up);
    }

    // Native 1401c22e0/1402298b0: axis is the plane normal; the default up
    // tangent is -Z, so depth streaks lie along the scene rather than the screen.
    // The draw helper applies the model and its transpose before normalizing.
    // Keep that order for nonuniform scale; renderer flag 1 skips the first step.
    const glm::mat3 transform = glm::transpose (model) * (worldSpace ? glm::mat3 (1.0f) : model);
    const glm::vec3 localForward = transform * forward;
    const glm::vec3 localUp = transform * up;
    return {
	normalize (glm::cross (localUp, localForward), { 1.0f, 0.0f, 0.0f }),
	normalize (localUp, up), normalize (localForward, forward)
    };
}

glm::mat3 WallpaperEngine::Render::Objects::calculateBillboardParticleOrientation (
    const glm::mat4& modelInverse, const glm::mat4& cameraWorld, const float roll
) {
    // Keep the layer's authored roll in the camera plane. Cancelling the entire
    // model rotation makes deliberately upside-down billboards stand upright.
    // Rotate before converting to local space so nonuniform scales do not shear
    // the camera-facing directions when the model matrix is applied again.
    const glm::mat3 cameraToLocal (modelInverse * glm::rotate (cameraWorld, roll, { 0.0f, 0.0f, 1.0f }));
    return {
	glm::normalize (cameraToLocal[0]), glm::normalize (cameraToLocal[1]), glm::normalize (cameraToLocal[2])
    };
}

CParticle::CParticle (Wallpapers::CScene& scene, const Particle& particle, CParticle* parent) :
    CObject (scene, particle), CRenderable (scene, particle, *particle.material->material),
    ScriptableObject (scene, particle), m_particle (particle), m_particleParent (parent) {
    this->registerProperty ("scale", *particle.scale);
    this->registerProperty ("angles", *particle.angles);
    this->registerProperty ("visible", *particle.visible);
    this->registerProperty ("parallaxDepth", *particle.parallaxDepth);
    if (parent == nullptr) {
	// Child systems consume the root's live overrides. Queue their property
	// scripts once on that root, with the same values used by the simulation.
	const auto& settings = particle.instanceOverride;
	for (const auto& [name, value] : std::initializer_list<std::pair<const char*, const UserSetting*>> {
	    { "enabled", settings.enabled.get () }, { "alpha", settings.alpha.get () },
	    { "brightness", settings.brightness.get () },
	    { "size", settings.size.get () }, { "lifetime", settings.lifetime.get () },
	    { "rate", settings.rate.get () }, { "speed", settings.speed.get () },
	    { "count", settings.count.get () }, { "color", settings.color.get () },
	    { "colorn", settings.colorn.get () } }) {
	    this->registerProperty (name, *value);
	}
	for (const auto& [id, value] : settings.controlPointOffsets) {
	    m_controlPointSubscriptions.emplace_back (Data::Utils::ScopeGuard (value->value->listen ([this, id] (const DynamicValue&, DynamicValue::UpdateSource source) {
		if (source != DynamicValue::UpdateSource::Initialization) m_controlPointOverridesChanged[id] = true;
	    })));
	    this->registerProperty ("instance.controlpoint" + std::to_string (id), *value, "instance.");
	}
    }

    this->detectTexture ();
    // Initialize random number generator with time-based seed
    std::random_device rd;
    m_rng.seed (rd ());

    // Read renderer configuration early to determine rendering mode
    if (!m_particle.renderers.empty ()) {
	const auto& renderer = m_particle.renderers[0];
	if (renderer.name == "rope" || renderer.name == "ropetrail") {
	    // Both rope and ropetrail use genericropeparticle shader
	    m_useRopeRenderer = true;
	    m_ropeSubdivision = std::max (0, static_cast<int> (renderer.subdivision));
	    m_ropeUVScale = renderer.uvScale;
	    m_ropeUVScrolling = renderer.uvScrolling;
	    m_ropeUVSmoothing = renderer.uvSmoothing;

	    if (renderer.name == "ropetrail") {
		m_useTrailRenderer = true;
		m_useRopeTrailRenderer = true;
		m_trailLength = renderer.length;
		if (renderer.segmentsExplicit) {
		    m_ropeSegments = std::max (2, static_cast<int> (renderer.segments));
		} else {
		    // Wallpaper Engine omits the default segment count from JSON. Four
		    // samples made long trails visibly step at 1-5 Hz, so derive a smooth
		    // bounded history rate from the authored duration instead.
		    m_ropeSegments
			= std::clamp (static_cast<int> (std::ceil (std::max (m_trailLength, 0.001f) * 30.0f)), 8, 96);
		}
		m_ropeFadeAlpha = renderer.fadeAlpha;
		m_ropeFadeSize = renderer.fadeSize;
	    }
	} else if (renderer.name == "spritetrail") {
	    // spritetrail uses genericparticle with TRAILRENDERER combo
	    m_useTrailRenderer = true;
	    m_trailLength = renderer.length;
	    m_trailMaxLength = renderer.maxLength;
	    m_trailMinLength = renderer.minLength;
	}
    }

    // maxCount is the authored capacity. The instance count override changes emission,
    // not the pool size.
    m_maxParticles = particle.maxCount > 0 ? particle.maxCount : DEFAULT_MAX_PARTICLES;

    m_particles.resize (m_maxParticles);

    // Calculate buffer sizes based on renderer type
    if (m_useRopeRenderer) {
	const int subdivision = std::max (1, m_ropeSubdivision);
	// A rope connects the particle pool once. A rope trail builds an independent
	// history for every particle and must never bridge two particle instances.
	const int maxSegments = m_useRopeTrailRenderer
	    ? std::max (1, static_cast<int> (m_maxParticles)) * m_ropeSegments
	    : std::max (1, static_cast<int> (m_maxParticles - 1));
	const int maxSubSegments = maxSegments * subdivision;
	m_vertices.resize (maxSubSegments * 4 * ROPE_FLOATS_PER_VERTEX);
	m_indices.resize (maxSubSegments * 6);
    } else {
	// Trail particles: (N+1) * 2 vertices for ribbon strip, N * 6 indices for N quads
	// Normal particles: 4 vertices, 6 indices
	const int verticesPerParticle = 4;
	const int indicesPerParticle = 6;

	m_vertices.resize (m_maxParticles * verticesPerParticle * SPRITE_FLOATS_PER_VERTEX);
	m_indices.resize (m_maxParticles * indicesPerParticle);
    }
}

CParticle::~CParticle () {
    delete m_pass;

    if (m_vao != 0) {
	glDeleteVertexArrays (1, &m_vao);
    }
    if (m_vbo != 0) {
	glDeleteBuffers (1, &m_vbo);
    }
    if (m_ebo != 0) {
	glDeleteBuffers (1, &m_ebo);
    }

    m_vertices.clear ();
    m_indices.clear ();
}

void CParticle::setup () {
    if (m_initialized) {
	return;
    }

    // Load particle material constants
    if (m_particle.material && m_particle.material->material && !m_particle.material->material->passes.empty ()) {
	auto& firstPass = *m_particle.material->material->passes.begin ();

	// Read overbright constant (brightness multiplier for additive particles)
	auto overbrightIt = firstPass->constants.find ("ui_editor_properties_overbright");
	if (overbrightIt != firstPass->constants.end ()) {
	    m_overbright = overbrightIt->second->value->getFloat ();
	}
    }

    // Texture is resolved by CRenderable base class; read spritesheet data.
    // TextureParser computes spritesheet grid from TEXS frame data (animated textures)
    // or .tex-json metadata (static textures). For GIF-style animated textures (separate
    // GL texture per frame), the parser returns 0 cols/rows since a 1x1 grid can't hold
    // all frames — so no SPRITESHEET mode is needed (frame switching happens via texture ID).
    if (const auto texture = getTexture ()) {
	m_spritesheetCols = static_cast<int> (texture->getSpritesheetCols ());
	m_spritesheetRows = static_cast<int> (texture->getSpritesheetRows ());
	m_spritesheetFrames = static_cast<int> (texture->getSpritesheetFrames ());
	m_spritesheetDuration = texture->getSpritesheetDuration ();
    }

    setupEmitters ();
    setupInitializers ();
    setupOperators ();
    setupPass ();

    m_controlPoints.resize (PARTICLE_CONTROL_POINT_COUNT);
    for (const auto& cp : m_particle.controlPoints) {
	if (cp.id < 0 || cp.id >= PARTICLE_CONTROL_POINT_COUNT) continue;
	auto& runtime = m_controlPoints[cp.id];
	runtime.offset = cp.offset;
	runtime.flags = cp.flags;
	runtime.parentControlPoint = cp.parentControlPoint;
    }
    updateControlPoints ();

    setupChildren ();
    m_initialized = true;
}

const ParticleInstanceOverride& CParticle::getInstanceOverride () const {
    return m_particleParent ? m_particleParent->getInstanceOverride () : m_particle.instanceOverride;
}

void CParticle::setupChildren () {
    const auto supported = [] (const ParticleChild& child) {
	return (child.type == "static" || (child.type == "eventfollow" && child.maxCount > 0))
	    && !child.particleFile.empty ();
    };
    size_t remainingChildren = std::ranges::count_if (m_particle.children, supported);
    for (const auto& child : m_particle.children) {
	if (!supported (child)) {
	    continue;
	}
	bool recursive = false;
	int depth = 0;
	CParticle* root = this;
	for (auto* ancestor = this; ancestor; ancestor = ancestor->m_particleParent) {
	    root = ancestor;
	    recursive |= ancestor->m_particle.particleFile == child.particleFile;
	    ++depth;
	}
	// Reserve a share for every sibling before letting this branch allocate
	// nested systems. A large first event pool must not discard later children
	// such as the star sprite accompanying a shooting star's glow.
	const size_t branchEnd = root->m_childSystemCount
	    + (m_childAllocationLimit - root->m_childSystemCount) / remainingChildren--;
	if (recursive || depth >= 8 || root->m_childSystemCount >= branchEnd) {
	    sLog.error ("Skipping recursive or excessive particle child: ", child.particleFile);
	    continue;
	}
	try {
	    using WallpaperEngine::Data::JSON::JSON;
	    auto definition = WallpaperEngine::Data::Parsers::ObjectParser::parse (
		JSON { { "id", -1 }, { "name", child.name }, { "particle", child.particleFile } },
		getScene ().getScene ().project
	    );
	    auto* particle = definition->as<Particle> ();
	    if (!particle || !particle->material || !particle->material->material
		|| particle->material->material->passes.empty ()) {
		continue;
	    }
	    particle->origin->value->update (child.origin, DynamicValue::UpdateSource::Initialization);
	    particle->angles->value->update (child.angles, DynamicValue::UpdateSource::Initialization);
	    particle->scale->value->update (child.scale, DynamicValue::UpdateSource::Initialization);
	    if (child.type == "static") {
		++root->m_childSystemCount;
		auto renderer = std::make_unique<CParticle> (getScene (), *particle, this);
		renderer->m_childAllocationLimit = branchEnd;
		renderer->m_controlPointStartIndex = child.controlPointStartIndex;
		renderer->setup ();
		m_children.push_back ({ std::move (definition), std::move (renderer) });
	    } else {
		FollowChildSystem system { .definition = std::move (definition), .settings = &child, .instances = {} };
		// Prepare a bounded pool on the load worker. Spawning an event child must
		// never parse assets or compile shaders in the middle of a rendered frame.
		const auto capacity = std::min (std::max (child.maxCount, 0), static_cast<int> (m_maxParticles));
		for (int i = 0; i < capacity; ++i) {
		    if (root->m_childSystemCount >= branchEnd) break;
		    ++root->m_childSystemCount;
		    auto renderer = std::make_unique<CParticle> (getScene (), *particle, this);
		    renderer->m_childAllocationLimit = branchEnd;
		    renderer->m_controlPointStartIndex = child.controlPointStartIndex;
		    renderer->setup ();
		    renderer->pause ();
		    system.instances.push_back ({ .renderer = std::move (renderer) });
		}
		m_followChildren.push_back (std::move (system));
	    }
	} catch (const std::exception& error) {
	    sLog.error ("Cannot load child particle system: ", child.particleFile, " - ", error.what ());
	}
    }
}

void CParticle::render () {
    if (!m_initialized || !m_particle.visible->value->getBool ()) {
	return;
    }

    // All children start with the parent, including systems with no root emitter.
    if (m_time == 0.0) {
	m_time = g_Time;
    }

    // Update particles
    float dt = g_Time - static_cast<float> (m_time);
    m_time = g_Time;

    // Control-point motion uses elapsed frame time, before simulation rate and
    // the physics stability cap. Sample even when emission/rate is paused.
    updateControlPoints (dt);

    if (m_turbulenceRateDirty) {
	m_turbulenceRate = std::max (0.01f, getInstanceOverride ().rate->value->getFloat ());
	m_turbulenceRateDirty = false;
    }

    if (dt > 0.0f) {
	// Cap dt to prevent simulation instability
	// Also provides more consistent behavior across different FPS
	dt = std::min (dt, 0.1f);
	dt = calculateParticleSimulationDelta (dt, getInstanceOverride ().rate->value->getFloat ());
	if (dt > 0.0f) {
	    m_simulationTime += dt;
	    update (dt);
	}
    }

    // Render particles
    if (m_particleCount > 0 && m_particle.material) {
	// Scene alpha belongs to the compositor, just as for image/model draws.
	// Composition layers need the particle alpha for their later group blend.
	glColorMask (true, true, true, getScene ().isRenderingToComposition () ? GL_TRUE : GL_FALSE);
	if (m_useRopeRenderer) {
	    renderRope ();
	} else {
	    renderSprites ();
	}
	glColorMask (true, true, true, true);
    }
    for (auto& child : m_children) {
	child.renderer->render ();
    }
    for (auto& child : m_followChildren) {
	for (auto& instance : child.instances) instance.renderer->render ();
    }
}

void CParticle::play () {
    m_emitting = true;
    for (auto& child : m_children) {
	child.renderer->play ();
    }
    for (auto& child : m_followChildren) {
	for (auto& instance : child.instances) {
	    if (instance.parentSerial != 0) instance.renderer->play ();
	}
    }
}

void CParticle::pause () {
    m_emitting = false;
    for (auto& child : m_children) {
	child.renderer->pause ();
    }
    for (auto& child : m_followChildren) {
	for (auto& instance : child.instances) instance.renderer->pause ();
    }
}

void CParticle::emitParticles (const uint32_t count) {
    if (count > 0 && m_pendingEmission == 0) {
	m_pendingControlPoints = m_controlPoints;
	m_pendingBirthTransform = m_controlPointTransform;
    }
    m_pendingEmission += std::min (count, m_maxParticles - m_pendingEmission);
}

void CParticle::stop () {
    m_pendingEmission = 0;
    m_pendingControlPoints.clear ();
    m_emitting = false;
    m_particleCount = 0;
    m_emitters.clear ();
    setupEmitters ();
    for (auto& child : m_children) {
	child.renderer->stop ();
    }
    for (auto& child : m_followChildren) {
	for (auto& instance : child.instances) {
	    instance.parentSerial = 0;
	    instance.renderer->stop ();
	}
    }
}

bool CParticle::hasLivingParticles () const {
    if (m_particleCount != 0) return true;
    for (const auto& child : m_children) {
	if (child.renderer->hasLivingParticles ()) return true;
    }
    for (const auto& child : m_followChildren) {
	for (const auto& instance : child.instances) {
	    if (instance.renderer->hasLivingParticles ()) return true;
	}
    }
    return false;
}

void CParticle::updateFollowChildren (const uint32_t firstNewParticle) {
    for (auto& child : m_followChildren) {
	for (uint32_t i = firstNewParticle; i < m_particleCount; ++i) {
	    const auto& particle = m_particles[i];
	    if (!particle.isAlive () || WallpaperEngine::Maths::randomFloat (m_rng, 0.0f, 1.0f)
		>= child.settings->probability) continue;
	    const auto slot = std::find_if (child.instances.begin (), child.instances.end (), [] (const auto& instance) {
		return instance.parentSerial == 0 && !instance.renderer->hasLivingParticles ();
	    });
	    if (slot == child.instances.end ()) break;
	    slot->parentSerial = particle.serial;
	    slot->renderer->stop ();
	    slot->renderer->m_simulationTime = 0.0f;
	    // Native child activation rebuilds derived parameters from root overrides.
	    slot->renderer->m_turbulenceRateDirty = true;
	    slot->renderer->m_time = g_Time;
	    slot->renderer->play ();
	}
	for (auto& instance : child.instances) {
	    if (instance.parentSerial == 0) continue;
	    const auto parent = std::find_if (
		m_particles.begin (), m_particles.begin () + m_particleCount, [&] (const auto& particle) {
		    return particle.serial == instance.parentSerial && particle.isAlive ();
		}
	    );
	    if (parent == m_particles.begin () + m_particleCount) {
		instance.parentSerial = 0;
		// Released child particles finish their lifetime at their own positions.
		instance.renderer->pause ();
	    } else {
		const glm::mat4 flipY = glm::scale (glm::mat4 (1.0f), glm::vec3 (1.0f, -1.0f, 1.0f));
		const glm::mat4 parentToLocal = glm::inverse (flipY
		    * ((m_particle.flags & 1) != 0 ? instance.renderer->particleWorldMatrix ()
			: instance.renderer->resolveWorldMatrix ()) * flipY);
		instance.renderer->m_followPosition = glm::vec3 (parentToLocal * glm::vec4 (parent->position, 1.0f));
	    }
	}
    }
}

glm::mat4 CParticle::particleWorldMatrix () {
    const glm::mat4 local = resolveWorldMatrix ();
    return m_particleParent ? m_particleParent->particleWorldMatrix () * local : local;
}

void ControlPointData::sampleVelocity (const glm::vec3& simulationPosition, const float frameTime) {
    velocity = hasPreviousPosition && frameTime > 0.0f
	? (simulationPosition - previousPosition) / frameTime : glm::vec3 (0.0f);
    previousPosition = simulationPosition;
    hasPreviousPosition = true;
}

void CParticle::updateControlPoints (const float frameTime) {
    const CParticle* root = this;
    while (root->m_particleParent != nullptr) root = root->m_particleParent;
    const glm::mat4 localToWorld = particleWorldMatrix ();
    const glm::mat4 worldToLocal = glm::inverse (localToWorld);
    const bool worldSpace = (m_particle.flags & 1) != 0;
    const glm::vec2* mouse = getScene ().getMousePositionNormalized ();
    glm::vec3 mouseWorld (0.0f);
    if (mouse) {
	if (getScene ().getScene ().camera.projection.isPerspective) {
	    const auto& camera = getScene ().getCamera ();
	    const glm::vec4 unprojected = glm::inverse (camera.getProjection () * camera.getLookAt ())
		* glm::vec4 (mouse->x * 2.0f - 1.0f, mouse->y * 2.0f - 1.0f, 0.0f, 1.0f);
	    mouseWorld = glm::vec3 (glm::vec2 (unprojected) / unprojected.w, 0.0f);
	} else {
	    mouseWorld = { mouse->x * getScene ().getCamera ().getWidth (),
		mouse->y * getScene ().getCamera ().getHeight (), 0.0f };
	}
    }
    const glm::mat4 flipY = glm::scale (glm::mat4 (1.0f), glm::vec3 (1.0f, -1.0f, 1.0f));
    const glm::mat4 parentToLocal = m_particleParent
	? flipY * glm::inverse (resolveWorldMatrix ()) * flipY : glm::mat4 (1.0f);
    for (size_t i = 0; i < m_controlPoints.size (); ++i) {
	auto& cp = m_controlPoints[i];
	if ((cp.flags & 1) != 0 && mouse) {
	    // Native 14022e3e0 replaces the translation with the cursor position;
	    // neither the definition offset nor an instance override is added to it.
	    cp.position = resolveParticleControlPoint (mouseWorld, worldToLocal, true);
	} else if ((cp.flags & 4) != 0 && m_particleParent && cp.parentControlPoint >= 0
	    && cp.parentControlPoint < static_cast<int> (m_particleParent->m_controlPoints.size ())) {
	    const auto& source = m_particleParent->m_controlPoints[cp.parentControlPoint];
	    cp.position = (cp.flags & 8) != 0 ? source.position
		: glm::vec3 (parentToLocal * glm::vec4 (source.position, 1.0f));
	} else {
	    glm::vec3 offset = cp.offset;
	    // Authored root offsets stay on that system. Script/user assignments
	    // propagate to descendants, including assignments of the same value.
	    const auto& overrides = root->m_particle.instanceOverride.controlPointOffsets;
	    if ((cp.flags & 5) == 0) {
		if (const auto it = overrides.find (static_cast<int> (i)); it != overrides.end ()) {
		    if (this == root || root->m_controlPointOverridesChanged[i] || it->second->animation != nullptr) {
			const auto value = it->second->evaluateVec3 (getScene ().getTime ());
			if (value.x != std::numeric_limits<float>::max ()) offset = value;
		    }
		}
	    }
	    // Native 14022a070 keeps point zero attached to a world-space emitter
	    // even when flagged as world. Other world points remain independent.
	    const bool worldOffset = (cp.flags & 2) != 0 && !(worldSpace && i == 0);
	    cp.position = resolveParticleControlPoint (offset, worldToLocal, worldOffset);
	}
    }
    if (m_followPosition.has_value () && (m_controlPoints[0].flags & 5) == 0) {
	m_controlPoints[0].position = *m_followPosition;
    }
    const glm::mat4 particleToWorld = flipY * localToWorld * flipY;
    m_controlPointTransform = particleToWorld;
    const glm::mat3 worldToParticle = glm::mat3 (flipY * worldToLocal * flipY);
    for (auto& cp : m_controlPoints) {
	cp.sampleVelocity (worldSpace ? glm::vec3 (particleToWorld * glm::vec4 (cp.position, 1.0f))
	    : cp.position, frameTime);
	// Native initializer opcode 8 measures in simulation space, then undoes
	// the layer transform for local-authored points before the birth transform.
	if (worldSpace && (cp.flags & 2) == 0) {
	    cp.velocity = worldToParticle * cp.velocity;
	}
    }
}

void CParticle::update (float dt) {
    // Reclaim this frame's expired slots before emitting. A full one-particle
    // glow otherwise rejects its replacement, then disappears for one frame.
    uint32_t writeIdx = 0;
    for (uint32_t readIdx = 0; readIdx < m_particleCount; ++readIdx) {
	auto& particle = m_particles[readIdx];
	particle.age += dt;
	if (particle.isAlive ()) {
	    if (writeIdx != readIdx) m_particles[writeIdx] = std::move (particle);
	    ++writeIdx;
	}
    }
    m_particleCount = writeIdx;

    // Pausing stops new emission; bubbles already released keep moving and aging.
    const uint32_t firstNewParticle = m_particleCount;
    if (m_emitting) {
	for (auto& emitter : m_emitters) {
	    emitter (m_particles, m_particleCount, dt, 0);
	}
    }
    const uint32_t firstManualParticle = m_particleCount;
    if (m_pendingEmission > 0) {
	// Native emitParticles() uses the matrices resolved before its callback.
	// Queue that same point state, even if the callback also changes a point.
	m_controlPoints.swap (m_pendingControlPoints);
	m_initializingManualEmission = true;
	for (auto& emitter : m_emitters) emitter (m_particles, m_particleCount, 0.0f, m_pendingEmission);
	m_initializingManualEmission = false;
	m_controlPoints.swap (m_pendingControlPoints);
	m_pendingControlPoints.clear ();
	m_pendingEmission = 0;
    }
    for (uint32_t i = firstNewParticle; i < m_particleCount; ++i) {
	m_particles[i].serial = ++m_nextParticleSerial;
    }

    std::vector<ControlPointData> worldControlPoints;
    const auto* operatorControlPoints = &m_controlPoints;
    if ((m_particle.flags & 1) != 0) {
	// Native 1402378a0/14023b340 transform births and initial velocity into
	// world space. Size and subsequent forces stay in world units; applying
	// the emitter model again made scaled meteor trails too wide and steep.
	const glm::mat4 flipY = glm::scale (glm::mat4 (1.0f), glm::vec3 (1.0f, -1.0f, 1.0f));
	const glm::mat4 localToWorld = flipY * particleWorldMatrix () * flipY;
	for (uint32_t i = firstNewParticle; i < m_particleCount; ++i) {
	    auto& particle = m_particles[i];
	    const auto& birthTransform = i < firstManualParticle ? localToWorld : m_pendingBirthTransform;
	    particle.position = glm::vec3 (birthTransform * glm::vec4 (particle.position, 1.0f));
	    particle.velocity = glm::mat3 (birthTransform) * particle.velocity;
	}
	// Emitters and child inheritance still need layer-local points. Operators
	// act in the same space as the already spawned particles.
	worldControlPoints = m_controlPoints;
	for (auto& point : worldControlPoints) {
	    point.position = glm::vec3 (localToWorld * glm::vec4 (point.position, 1.0f));
	}
	operatorControlPoints = &worldControlPoints;
    }

    // Native 14023fc75/14023fc99 restore birth alpha/size before the ordered
    // operator chain. Reset once, so oscillators do not accumulate per frame.
    for (uint32_t i = 0; i < m_particleCount; ++i) {
        if (m_hasAlphaOperators) m_particles[i].alpha = m_particles[i].initial.alpha;
        m_particles[i].size = m_particles[i].initial.size;
    }

    // Apply operators to living particles (including alphafade)
    for (auto& op : m_operators) {
	op (m_particles, m_particleCount, *operatorControlPoints, m_simulationTime, dt);
    }

    // Update animation frames
    for (uint32_t i = 0; i < m_particleCount; i++) {
	auto& p = m_particles[i];

	if (m_spritesheetFrames > 0) {
	    float lifetimePos = p.getLifetimePos ();
	    float animSpeed = m_particle.sequenceMultiplier > 0.0f ? m_particle.sequenceMultiplier : 1.0f;

	    if (m_particle.animationMode == "randomframe") {
		if (p.frame < 0.0f) {
		    std::mt19937 particleRng (
			static_cast<std::mt19937::result_type> (reinterpret_cast<uintptr_t> (&p))
		    );
		    std::uniform_int_distribution<int> dist (0, m_spritesheetFrames - 1);
		    p.frame = static_cast<float> (dist (particleRng));
		}
	    } else if (m_particle.animationMode == "once") {
		p.frame = std::min (
		    lifetimePos * m_spritesheetFrames * animSpeed, static_cast<float> (m_spritesheetFrames - 1)
		);
	    } else {
		if (m_particle.sequenceMultiplier > 0.0f) {
		    // sequencemultiplier is relative to the particle's lifetime: the sequence
		    // plays that many times over the particle's life, regardless of the
		    // texture's own frame timings
		    p.frame = std::fmod (
			lifetimePos * m_spritesheetFrames * animSpeed, static_cast<float> (m_spritesheetFrames)
		    );
		} else if (m_spritesheetDuration > 0.0f) {
		    // no multiplier authored: play at the texture's own animation speed
		    float timeInCycle = std::fmod (p.age, m_spritesheetDuration);
		    float cyclePos = timeInCycle / m_spritesheetDuration;
		    p.frame = std::fmod (cyclePos * m_spritesheetFrames, static_cast<float> (m_spritesheetFrames));
		} else {
		    p.frame = std::fmod (lifetimePos * m_spritesheetFrames, static_cast<float> (m_spritesheetFrames));
		}
	    }
	}
    }

    updateRopeTrailHistory (dt);
    updateFollowChildren (firstNewParticle);

}

void CParticle::updateRopeTrailHistory (float dt) {
    if (!m_useRopeTrailRenderer || dt <= 0.0f) {
	return;
    }

    const int maxSegments = std::max (2, m_ropeSegments);
    const float historyLength = std::max (m_trailLength, 0.001f);
    const float sampleInterval = historyLength / static_cast<float> (maxSegments);
    const float timerBeforeFrame = m_ropeHistoryTimer;
    const float accumulatedTime = timerBeforeFrame + dt;
    const int sampleCount = static_cast<int> (std::floor (accumulatedTime / sampleInterval));
    const int firstSample = std::max (0, sampleCount - maxSegments);

    auto snapshot = [] (const ParticleInstance& particle) {
	return ParticleInstance::TrailPoint { .position = particle.position };
    };

    auto interpolate = [] (const ParticleInstance::TrailPoint& from, const ParticleInstance::TrailPoint& to, float t) {
	return ParticleInstance::TrailPoint { .position = glm::mix (from.position, to.position, t) };
    };

    for (uint32_t i = 0; i < m_particleCount; i++) {
	auto& particle = m_particles[i];
	if (!particle.isAlive ()) {
	    continue;
	}

	const auto current = snapshot (particle);
	if (!particle.trailLastFrameValid) {
	    particle.trailHistory.clear ();
	    particle.trailHistory.reserve (static_cast<size_t> (maxSegments));
	    particle.trailHistory.push_back (current);
	    particle.trailLastFrame = current;
	    particle.trailLastFrameValid = true;
	    continue;
	}

	for (int sample = firstSample; sample < sampleCount; sample++) {
	    const float sampleOffset = sampleInterval - timerBeforeFrame + static_cast<float> (sample) * sampleInterval;
	    const float frameFraction = std::clamp (sampleOffset / dt, 0.0f, 1.0f);
	    particle.trailHistory.push_back (interpolate (particle.trailLastFrame, current, frameFraction));
	    if (static_cast<int> (particle.trailHistory.size ()) > maxSegments) {
		particle.trailHistory.erase (particle.trailHistory.begin ());
	    }
	}

	particle.trailLastFrame = current;
    }

    m_ropeHistoryTimer = std::fmod (accumulatedTime, sampleInterval);
}

const Particle& CParticle::getParticle () const { return m_particle; }

const float& CParticle::getBrightness () const { return m_overbright; }

const float& CParticle::getUserAlpha () const { return getInstanceOverride ().alpha->value->getFloat (); }

const float& CParticle::getAlpha () const { return getInstanceOverride ().alpha->value->getFloat (); }

const glm::vec3& CParticle::getColor () const {
    static const glm::vec3 defaultColor (1.0f);
    if (getInstanceOverride ().color && getInstanceOverride ().color->value) {
	return getInstanceOverride ().color->value->getVec3 ();
    }
    return defaultColor;
}

const glm::vec4& CParticle::getColor4 () const {
    static const glm::vec4 defaultColor (1.0f);
    if (getInstanceOverride ().color && getInstanceOverride ().color->value) {
	return getInstanceOverride ().color->value->getVec4 ();
    }
    return defaultColor;
}

const glm::vec3& CParticle::getCompositeColor () const { return getColor (); }

// ========== EMITTERS ==========

void CParticle::setupEmitters () {
    for (const auto& emitter : m_particle.emitters) {
	EmitterFunc func;

	if (emitter.name == "boxrandom") {
	    func = createBoxEmitter (emitter);
	} else if (emitter.name == "sphererandom") {
	    func = createSphereEmitter (emitter);
	} else {
	    sLog.out ("Unknown emitter type: ", emitter.name);
	    continue;
	}

	if (func) {
	    m_emitters.push_back (std::move (func));
	}
    }
}

EmitterFunc CParticle::createBoxEmitter (const ParticleEmitter& emitter) {
    DynamicValue* countOverride = getInstanceOverride ().count->value.get ();

    glm::vec3 transformedEmitterOrigin = emitter.origin;
    transformedEmitterOrigin.y = -transformedEmitterOrigin.y;

    int controlPointIndex = emitter.controlPoint;
    if (controlPointIndex == -1 && !m_particle.controlPoints.empty ()) {
	const auto& cp0 = m_particle.controlPoints[0];
	if ((cp0.flags & 1) != 0) {
	    controlPointIndex = 0;
	}
    }

    glm::vec3 flippedDirections = emitter.directions;
    flippedDirections.y = -flippedDirections.y;

    bool limitOnePerFrame = (emitter.flags & 2) != 0;
    bool randomPeriodicEmission = (emitter.flags & 4) != 0;

    return
	[this, emitter, transformedEmitterOrigin, controlPointIndex, countOverride, flippedDirections, limitOnePerFrame,
	 randomPeriodicEmission, emissionTimer = 0.0f, delayTimer = emitter.delay, durationTimer = 0.0f,
	 periodicTimer = 0.0f, periodicDuration = 0.0f, periodicDelay = 0.0f, emitting = false,
	 instantaneousEmitted = false] (std::vector<ParticleInstance>& particles, uint32_t& count, float dt, uint32_t burst) mutable {
	    if (count >= particles.size ()) {
		return;
	    }

	    uint32_t toEmit = burst;
	    if (burst == 0) {
		// Handle delay
		if (delayTimer > 0.0f) {
		    delayTimer -= dt;
		    return;
		}

		// Handle duration
		if (emitter.duration > 0.0f) {
		    durationTimer += dt;
		    if (durationTimer >= emitter.duration) {
			return;
		    }
		}

		// Handle random periodic emission
		if (randomPeriodicEmission) {
		    periodicTimer += dt;

		    if (!emitting) {
			if (periodicTimer >= periodicDelay) {
			    emitting = true;
			    periodicTimer = 0.0f;
			    periodicDuration = WallpaperEngine::Maths::randomFloat (
				m_rng, emitter.minPeriodicDuration, emitter.maxPeriodicDuration
			    );
			} else {
			    return;
			}
		    } else {
			if (periodicTimer >= periodicDuration) {
			    emitting = false;
			    periodicTimer = 0.0f;
			    periodicDelay = WallpaperEngine::Maths::randomFloat (
				m_rng, emitter.minPeriodicDelay, emitter.maxPeriodicDelay
			    );
			    return;
			}
		    }
		}

		// Handle instantaneous emission
		toEmit = 0;
		if (emitter.instantaneous > 0 && !instantaneousEmitted) {
		    toEmit = emitter.instantaneous;
		    instantaneousEmitted = true;
		}

		// Rate-based emission with optional cap at 1 per frame
		if (emitter.rate > 0.0f) {
		    const auto& recorder = getScene ().getAudioContext ().getRecorder ();
		    const float response = calculateParticleAudioResponse (
			recorder.audio16Left, recorder.audio16Right, emitter.audioProcessingMode,
			emitter.audioProcessingBounds, emitter.audioProcessingExponent,
			emitter.audioProcessingFrequencyStart, emitter.audioProcessingFrequencyEnd
		    );
		    const float rate = calculateParticleEmissionRate (emitter.rate, countOverride->getFloat ()) * response;
		    emissionTimer += dt * rate;
		    uint32_t rateEmit = static_cast<uint32_t> (emissionTimer);
		    emissionTimer -= static_cast<float> (rateEmit);
		    // limitOnePerFrame (flags bit 1): cap at 1 to prevent rope artifacts
		    if (limitOnePerFrame && rateEmit > 1) {
			rateEmit = 1;
		    }
		    toEmit += rateEmit;
		}

	    }
	    // Emit particles
	    for (uint32_t i = 0; i < toEmit && count < particles.size (); i++) {
		auto& p = particles[count];

		glm::vec3 spawnOrigin = transformedEmitterOrigin;
		if (controlPointIndex >= 0 && controlPointIndex < static_cast<int> (m_controlPoints.size ())) {
		    spawnOrigin += m_controlPoints[controlPointIndex].position;
		}

		// Generate random position within box volume centered on origin
		// This creates a centered box (or hollow box if distanceMin > 0)
		glm::vec3 randomPos;
		for (int axis = 0; axis < 3; axis++) {
		    float minDist = emitter.distanceMin[axis];
		    float maxDist = emitter.distanceMax[axis];
		    // Generate value in [minDist, maxDist]
		    float dist = WallpaperEngine::Maths::randomFloat (m_rng, minDist, maxDist);
		    // Randomly flip sign to center the distribution
		    if (WallpaperEngine::Maths::randomFloat (m_rng, 0.0f, 1.0f) < 0.5f) {
			dist = -dist;
		    }
		    randomPos[axis] = dist;
		}
		randomPos *= flippedDirections;

		p.position = spawnOrigin + randomPos;

		// Emitter does not set velocity - initializers handle that
		p.velocity = glm::vec3 (0.0f);
		p.acceleration = glm::vec3 (0.0f);
		p.rotation = glm::vec3 (0.0f);
		p.angularVelocity = glm::vec3 (0.0f);
		p.angularAcceleration = glm::vec3 (0.0f);

		p.color = glm::vec3 (1.0f) * getInstanceOverride ().colorn->value->getVec3 ();
		p.alpha = 1.0f * getInstanceOverride ().alpha->value->getFloat ();
		p.size = 20.0f * getInstanceOverride ().size->value->getFloat ();
		p.lifetime = 1.0f * getInstanceOverride ().lifetime->value->getFloat ();
		p.age = 0.0f;
		p.alive = true;
		p.frame = -1.0f;

		p.initial.color = p.color;
		p.initial.alpha = p.alpha;
		p.initial.size = p.size;
		p.initial.lifetime = p.lifetime;

		// Reset oscillator state for reused particles
		p.oscillationRandom = -1.0f;
		p.trailHistory.clear ();
		p.trailLastFrameValid = false;

		// Apply initializers
		for (auto& init : m_initializers) {
		    init (p);
		}

		count++;
	    }
	};
}

EmitterFunc CParticle::createSphereEmitter (const ParticleEmitter& emitter) {
    DynamicValue* countOverride = getInstanceOverride ().count->value.get ();
    float lifetime = 1.0f * getInstanceOverride ().lifetime->value->getFloat ();

    // Convert emitter origin from screen space (Y down) to centered space (Y up)
    glm::vec3 transformedEmitterOrigin = emitter.origin;
    transformedEmitterOrigin.y = -transformedEmitterOrigin.y;

    int controlPointIndex = emitter.controlPoint;

    // Auto-detect control point 0 usage if controlPoint field not specified and CP0 has linkMouse
    if (controlPointIndex == -1 && !m_particle.controlPoints.empty ()) {
	const auto& cp0 = m_particle.controlPoints[0];
	if ((cp0.flags & 1) != 0) { // Bit 0: linkMouse flag
	    controlPointIndex = 0;
	}
    }

    bool limitOnePerFrame = (emitter.flags & 2) != 0;

    return [this, emitter, transformedEmitterOrigin, controlPointIndex, countOverride, lifetime, limitOnePerFrame,
	    emissionTimer = 0.0f, delayTimer = emitter.delay,
	    remaining
	    = emitter.instantaneous] (std::vector<ParticleInstance>& particles, uint32_t& count, float dt, uint32_t burst) mutable {
	if (count >= particles.size ()) {
	    return;
	}

	uint32_t toEmit = burst;
	if (burst == 0) {
	    if (delayTimer > 0.0f) {
		delayTimer -= dt;
		return;
	    }
	    // Rate-based emission with optional cap at 1 per frame
	    const auto& recorder = getScene ().getAudioContext ().getRecorder ();
	    const float response = calculateParticleAudioResponse (
		recorder.audio16Left, recorder.audio16Right, emitter.audioProcessingMode,
		emitter.audioProcessingBounds, emitter.audioProcessingExponent,
		emitter.audioProcessingFrequencyStart, emitter.audioProcessingFrequencyEnd
	    );
	    const float rate = calculateParticleEmissionRate (emitter.rate, countOverride->getFloat ()) * response;
	    emissionTimer += dt * rate;
	    toEmit = static_cast<uint32_t> (emissionTimer);
	    emissionTimer -= static_cast<float> (toEmit);
	    // limitOnePerFrame (flags bit 1): cap at 1 to prevent rope artifacts
	    if (limitOnePerFrame && toEmit > 1) {
		toEmit = 1;
	    }

	    if (remaining > 0) {
		toEmit = remaining;
		remaining = 0;
	    }
	}

	for (uint32_t i = 0; i < toEmit && count < particles.size (); i++) {
	    auto& p = particles[count];

	    // Determine spawn origin (control point or emitter origin)
	    glm::vec3 spawnOrigin = transformedEmitterOrigin;
	    if (controlPointIndex >= 0 && controlPointIndex < static_cast<int> (m_controlPoints.size ())) {
		spawnOrigin += m_controlPoints[controlPointIndex].position;
	    }

	    // Spawn at random position on ellipsoid surface
	    glm::vec3 randomPos;

	    // Orthographic particles (flags & 4 == 0): use 2D disk distribution in X/Y plane
	    // Perspective particles (flags & 4 != 0): use 3D spherical shell distribution
	    if (!getScene ().getScene ().camera.projection.isPerspective && (m_particle.flags & 4) == 0) {
		// 2D disk distribution with random Z offset
		float angle = WallpaperEngine::Maths::randomFloat (m_rng, 0.0f, glm::two_pi<float> ());
		float minRadius = emitter.distanceMin.x;
		float maxRadius = emitter.distanceMax.x;

		// Use sqrt for uniform area distribution in annulus
		float minRadiusSq = minRadius * minRadius;
		float maxRadiusSq = maxRadius * maxRadius;
		float radiusXY = std::sqrt (WallpaperEngine::Maths::randomFloat (m_rng, minRadiusSq, maxRadiusSq));

		randomPos = glm::vec3 (
		    radiusXY * std::cos (angle), radiusXY * std::sin (angle),
		    WallpaperEngine::Maths::randomFloat (m_rng, -maxRadius, maxRadius)
		);

		randomPos *= emitter.directions;
	    } else {
		// 3D spherical shell distribution
		float theta = WallpaperEngine::Maths::randomFloat (m_rng, 0.0f, glm::two_pi<float> ());
		float cosTheta = WallpaperEngine::Maths::randomFloat (m_rng, -1.0f, 1.0f);
		float sinTheta = std::sqrt (1.0f - cosTheta * cosTheta);

		randomPos = glm::vec3 (sinTheta * std::cos (theta), sinTheta * std::sin (theta), cosTheta);

		// Use cubic root for uniform volume distribution
		float minRadius = emitter.distanceMin.x;
		float maxRadius = emitter.distanceMax.x;
		float minRadiusCubed = minRadius * minRadius * minRadius;
		float maxRadiusCubed = maxRadius * maxRadius * maxRadius;
		float radius = std::cbrt (WallpaperEngine::Maths::randomFloat (m_rng, minRadiusCubed, maxRadiusCubed));

		randomPos *= radius;
		randomPos *= emitter.directions;
	    }

	    // Apply sign property to force positive/negative values per axis
	    // 0 = both, 1 = positive only, -1 = negative only
	    for (int i = 0; i < 3; i++) {
		if (emitter.sign[i] == 1) {
		    randomPos[i] = std::abs (randomPos[i]); // Force positive
		} else if (emitter.sign[i] == -1) {
		    randomPos[i] = -std::abs (randomPos[i]); // Force negative
		}
		// If sign[i] == 0, leave as-is (both positive and negative possible)
	    }
	    p.position = spawnOrigin + randomPos;

	    // Set velocity only if emitter specifies speed (otherwise use initializers)
	    if (emitter.speedMax > 0.0f || emitter.speedMin != 0.0f) {
		// Velocity pointing outward from ellipsoid (randomPos already includes directions scaling)
		glm::vec3 direction
		    = glm::length (randomPos) > 0.0f ? glm::normalize (randomPos) : glm::vec3 (0.0f, 1.0f, 0.0f);
		float speed = WallpaperEngine::Maths::randomFloat (m_rng, emitter.speedMin, emitter.speedMax);
		p.velocity = direction * speed;
	    } else {
		// No emitter speed specified, velocity will be set by initializers
		p.velocity = glm::vec3 (0.0f);
	    }

	    p.acceleration = glm::vec3 (0.0f);
	    p.rotation = glm::vec3 (0.0f);
	    p.angularVelocity = glm::vec3 (0.0f);
	    p.angularAcceleration = glm::vec3 (0.0f);

	    p.color = glm::vec3 (1.0f) * getInstanceOverride ().colorn->value->getVec3 ();
	    p.alpha = 1.0f * getInstanceOverride ().alpha->value->getFloat ();
	    p.size = 20.0f * getInstanceOverride ().size->value->getFloat ();
	    p.lifetime = lifetime;
	    p.age = 0.0f;
	    p.alive = true;
	    p.frame = -1.0f;

	    p.initial.color = p.color;
	    p.initial.alpha = p.alpha;
	    p.initial.size = p.size;
	    p.initial.lifetime = p.lifetime;

	    // Reset oscillator state for reused particles
	    p.oscillationRandom = -1.0f;
	    p.trailHistory.clear ();
	    p.trailLastFrameValid = false;

	    for (auto& init : m_initializers) {
		init (p);
	    }

	    count++;
	}
    };
}

// ========== INITIALIZERS ==========

void CParticle::setupInitializers () {
    for (const auto& initializer : m_particle.initializers) {
	if (!initializer) {
	    continue;
	}

	InitializerFunc func;

	if (initializer->is<ColorRandomInitializer> ()) {
	    func = createColorRandomInitializer (*initializer->as<ColorRandomInitializer> ());
	} else if (initializer->is<SizeRandomInitializer> ()) {
	    func = createSizeRandomInitializer (*initializer->as<SizeRandomInitializer> ());
	} else if (initializer->is<AlphaRandomInitializer> ()) {
	    func = createAlphaRandomInitializer (*initializer->as<AlphaRandomInitializer> ());
	} else if (initializer->is<LifetimeRandomInitializer> ()) {
	    const auto& lifeInit = *initializer->as<LifetimeRandomInitializer> ();
	    m_uniformLifetimes = (lifeInit.min->value->getFloat () == lifeInit.max->value->getFloat ());
	    func = createLifetimeRandomInitializer (lifeInit);
	} else if (initializer->is<VelocityRandomInitializer> ()) {
	    func = createVelocityRandomInitializer (*initializer->as<VelocityRandomInitializer> ());
	} else if (initializer->is<InheritControlPointVelocityInitializer> ()) {
	    func = createInheritControlPointVelocityInitializer (*initializer->as<InheritControlPointVelocityInitializer> ());
	} else if (initializer->is<RotationRandomInitializer> ()) {
	    func = createRotationRandomInitializer (*initializer->as<RotationRandomInitializer> ());
	} else if (initializer->is<AngularVelocityRandomInitializer> ()) {
	    func = createAngularVelocityRandomInitializer (*initializer->as<AngularVelocityRandomInitializer> ());
	} else if (initializer->is<TurbulentVelocityRandomInitializer> ()) {
	    func = createTurbulentVelocityRandomInitializer (*initializer->as<TurbulentVelocityRandomInitializer> ());
	} else if (initializer->is<PositionOffsetRandomInitializer> ()) {
	    func = createPositionOffsetRandomInitializer (*initializer->as<PositionOffsetRandomInitializer> ());
	} else if (initializer->is<MapSequenceAroundControlPointInitializer> ()) {
	    func = createMapSequenceAroundControlPointInitializer (
		*initializer->as<MapSequenceAroundControlPointInitializer> ()
	    );
	} else if (initializer->is<MapSequenceBetweenControlPointsInitializer> ()) {
	    func = createMapSequenceBetweenControlPointsInitializer (
		*initializer->as<MapSequenceBetweenControlPointsInitializer> ()
	    );
	} else {
	    sLog.out ("Unknown initializer type");
	}

	if (func) {
	    m_initializers.push_back (std::move (func));
	}
    }
}

InitializerFunc CParticle::createColorRandomInitializer (const ColorRandomInitializer& init) {
    DynamicValue* minValue = init.min->value.get ();
    DynamicValue* maxValue = init.max->value.get ();
    DynamicValue* colorOverride = getInstanceOverride ().colorn->value.get ();

    return [this, minValue, maxValue, colorOverride] (ParticleInstance& p) {
	p.color = WallpaperEngine::Maths::randomVec3 (m_rng, minValue->getVec3 (), maxValue->getVec3 ())
	    * colorOverride->getVec3 ();
	p.initial.color = p.color;
    };
}

InitializerFunc CParticle::createSizeRandomInitializer (const SizeRandomInitializer& init) {
    DynamicValue* minValue = init.min->value.get ();
    DynamicValue* maxValue = init.max->value.get ();
    DynamicValue* exponentValue = init.exponent->value.get ();
    DynamicValue* sizeOverride = getInstanceOverride ().size->value.get ();

    return [this, minValue, maxValue, exponentValue, sizeOverride] (ParticleInstance& p) {
	float t = WallpaperEngine::Maths::randomFloat (m_rng, 0.0f, 1.0f);
	float exponent = exponentValue->getFloat ();
	float min = minValue->getFloat ();
	float max = maxValue->getFloat ();

	// Apply exponent for non-linear distribution
	float adjustedT = std::pow (t, exponent);
	p.size = (min + adjustedT * (max - min)) * sizeOverride->getFloat () / 2.0f;
	p.initial.size = p.size;
    };
}

InitializerFunc CParticle::createAlphaRandomInitializer (const AlphaRandomInitializer& init) {
    DynamicValue* minValue = init.min->value.get ();
    DynamicValue* maxValue = init.max->value.get ();
    DynamicValue* alphaOverride = getInstanceOverride ().alpha->value.get ();

    return [this, minValue, maxValue, alphaOverride] (ParticleInstance& p) {
	p.alpha = WallpaperEngine::Maths::randomFloat (m_rng, minValue->getFloat (), maxValue->getFloat ())
	    * alphaOverride->getFloat ();
	p.initial.alpha = p.alpha;
    };
}

InitializerFunc CParticle::createLifetimeRandomInitializer (const LifetimeRandomInitializer& init) {
    DynamicValue* minValue = init.min->value.get ();
    DynamicValue* maxValue = init.max->value.get ();
    DynamicValue* lifetimeOverride = getInstanceOverride ().lifetime->value.get ();

    return [this, minValue, maxValue, lifetimeOverride] (ParticleInstance& p) {
	p.lifetime = WallpaperEngine::Maths::randomFloat (m_rng, minValue->getFloat (), maxValue->getFloat ())
	    * lifetimeOverride->getFloat ();
	p.initial.lifetime = p.lifetime;
    };
}

InitializerFunc CParticle::createVelocityRandomInitializer (const VelocityRandomInitializer& init) {
    DynamicValue* minValue = init.min->value.get ();
    DynamicValue* maxValue = init.max->value.get ();
    DynamicValue* speedOverride = getInstanceOverride ().speed->value.get ();

    return [this, minValue, maxValue, speedOverride] (ParticleInstance& p) {
	glm::vec3 vel = WallpaperEngine::Maths::randomVec3 (m_rng, minValue->getVec3 (), maxValue->getVec3 ())
	    * speedOverride->getFloat ();
	vel.y = -vel.y;
	p.velocity += vel;
    };
}

InitializerFunc CParticle::createInheritControlPointVelocityInitializer (
    const InheritControlPointVelocityInitializer& init
) {
    DynamicValue* minValue = init.min->value.get ();
    DynamicValue* maxValue = init.max->value.get ();
    DynamicValue* speedOverride = getInstanceOverride ().speed->value.get ();
    const int point = init.controlPoint;
    return [this, minValue, maxValue, speedOverride, point] (ParticleInstance& particle) {
	// Use one scalar for all axes; independent samples would change direction.
	const float weight = WallpaperEngine::Maths::randomFloat (m_rng, minValue->getFloat (), maxValue->getFloat ());
	// Native scripted bursts run between simulation ticks, after current
	// control-point matrices have been copied to previous: their delta is zero.
	if (!m_initializingManualEmission && point >= 0 && point < static_cast<int> (m_controlPoints.size ())) {
	    particle.velocity += m_controlPoints[point].velocity * weight * speedOverride->getFloat ();
	}
    };
}

InitializerFunc CParticle::createRotationRandomInitializer (const RotationRandomInitializer& init) {
    DynamicValue* minValue = init.min->value.get ();
    DynamicValue* maxValue = init.max->value.get ();
    DynamicValue* speedOverride = getInstanceOverride ().speed->value.get ();

    return [this, minValue, maxValue, speedOverride] (ParticleInstance& p) {
	p.rotation = WallpaperEngine::Maths::randomVec3 (m_rng, minValue->getVec3 (), maxValue->getVec3 ())
	    * speedOverride->getFloat ();
    };
}

InitializerFunc CParticle::createAngularVelocityRandomInitializer (const AngularVelocityRandomInitializer& init) {
    DynamicValue* minValue = init.min->value.get ();
    DynamicValue* maxValue = init.max->value.get ();
    DynamicValue* exponentValue = init.exponent->value.get ();
    DynamicValue* speedOverride = getInstanceOverride ().speed->value.get ();

    return [this, minValue, maxValue, exponentValue, speedOverride] (ParticleInstance& p) {
	glm::vec3 minVec = minValue->getVec3 ();
	glm::vec3 maxVec = maxValue->getVec3 ();
	float exponent = exponentValue->getFloat ();

	// Apply exponent bias to random distribution
	// exponent = 1: uniform distribution
	// exponent -> 0: bias towards max
	// exponent >= 2: bias towards min
	glm::vec3 result;
	for (int i = 0; i < 3; i++) {
	    float t = WallpaperEngine::Maths::randomFloat (m_rng, 0.0f, 1.0f);
	    t = std::pow (t, exponent);
	    result[i] = minVec[i] + t * (maxVec[i] - minVec[i]);
	}

	p.angularVelocity = result * speedOverride->getFloat ();
    };
}

InitializerFunc CParticle::createPositionOffsetRandomInitializer (const PositionOffsetRandomInitializer& init) {
    const bool perspective = getScene ().getScene ().camera.projection.isPerspective;
    return [this, &init, perspective] (ParticleInstance& p) {
	const glm::vec3 directions = init.directions ? init.directions->value->getVec3 ()
	    : glm::vec3 (1.0f, 1.0f, perspective ? 1.0f : 0.0f);
	const float scale = init.scale ? init.scale->value->getFloat () : (perspective ? 1.0f : 0.001f);
	const float distance = init.distance ? init.distance->value->getFloat () : (perspective ? 0.1f : 100.0f);
	const float time = getScene ().getTime () * init.timeScale->value->getFloat ();
	const bool worldSpace = (m_particle.flags & 1) != 0;
	const auto& birthTransform = m_initializingManualEmission ? m_pendingBirthTransform : m_controlPointTransform;
	const glm::mat3 basis (birthTransform);
	// A collapsed birth transform cannot represent a world offset locally.
	// Keep its particles finite rather than applying a singular inverse.
	if (distance == 0.0f || (worldSpace && glm::determinant (basis) == 0.0f)) return;
	const glm::vec3 position = (worldSpace ? glm::vec3 (birthTransform * glm::vec4 (p.position, 1.0f)) : p.position)
	    * glm::vec3 (1.0f, -1.0f, 1.0f);
	glm::vec3 offset (
	    fractalNoise2D (position.x * scale, time, init.octaves),
	    fractalNoise2D (time, position.y * scale, init.octaves),
	    fractalNoise2D (position.z * scale, -time, init.octaves)
	);
	offset *= directions;
	const glm::vec3 sign = init.sign->value->getVec3 ();
	offset = offset * (glm::vec3 (1.0f) - glm::abs (sign)) + glm::abs (offset) * sign;
	offset *= distance * glm::vec3 (1.0f, -1.0f, 1.0f);
	// The ordinary birth path applies this matrix after all initializers.
	p.position += worldSpace ? glm::inverse (basis) * offset : offset;
    };
}

InitializerFunc CParticle::createTurbulentVelocityRandomInitializer (const TurbulentVelocityRandomInitializer& init) {
    DynamicValue* speedMin = init.speedMin->value.get ();
    DynamicValue* speedMax = init.speedMax->value.get ();
    DynamicValue* offsetVal = init.offset->value.get ();
    DynamicValue* scaleVal = init.scale->value.get ();
    DynamicValue* forwardVal = init.forward->value.get ();
    DynamicValue* timeScaleVal = init.timeScale->value.get ();
    DynamicValue* phaseMinVal = init.phaseMin->value.get ();
    DynamicValue* phaseMaxVal = init.phaseMax->value.get ();
    DynamicValue* rightVal = init.right->value.get ();
    DynamicValue* speedOverride = getInstanceOverride ().speed->value.get ();

    return [this, speedMin, speedMax, offsetVal, scaleVal, forwardVal, timeScaleVal, phaseMinVal, phaseMaxVal, rightVal,
	    speedOverride] (ParticleInstance& p) {
	// Get direction parameters
	glm::vec3 forward = forwardVal->getVec3 ();
	glm::vec3 right = rightVal->getVec3 ();
	// Y-flip for coordinate system conversion
	forward.y = -forward.y;
	right.y = -right.y;

	if (glm::length (forward) > 0.0001f) {
	    forward = glm::normalize (forward);
	} else {
	    // Default forward direction when not specified (up in centered space)
	    forward = glm::vec3 (0.0f, 1.0f, 0.0f);
	}
	if (glm::length (right) > 0.0001f) {
	    right = glm::normalize (right);
	} else {
	    right = glm::vec3 (1.0f, 0.0f, 0.0f);
	}

	float speed = WallpaperEngine::Maths::randomFloat (m_rng, speedMin->getFloat (), speedMax->getFloat ());
	float scale = scaleVal->getFloat ();
	float offset = offsetVal->getFloat ();
	float timeScale = timeScaleVal->getFloat ();
	float phaseMin = phaseMinVal->getFloat ();
	float phaseMax = phaseMaxVal->getFloat ();

	// Sample noise at particle position + time-based offset.
	// timescale shifts the noise field over time so particles spawned at different
	// times get gradually changing directions (creates smooth evolving vapor stream).
	// Position component provides spatial coherence for nearby particles.
	glm::vec3 noisePos = p.position * 0.1f;
	noisePos += glm::vec3 (static_cast<float> (m_time) * timeScale);

	// Phase adds per-particle randomization to noise position
	float phase = WallpaperEngine::Maths::randomFloat (m_rng, phaseMin, phaseMax);
	glm::vec3 samplePos = noisePos + glm::vec3 (phase, phase * 0.7f, phase * 1.3f);

	// Sample curl noise for direction and normalize
	glm::vec3 result = curlNoise (samplePos);
	float len = glm::length (result);
	if (len < 0.0001f) {
	    result = forward;
	} else {
	    result = result / len;
	}

	// Scale limits how far direction can deviate from forward
	if (scale < 2.0f) {
	    float cosAngle = glm::dot (result, forward);
	    float angle = std::acos (glm::clamp (cosAngle, -1.0f, 1.0f)) / glm::pi<float> ();
	    float maxAngle = scale / 2.0f;

	    if (angle > maxAngle && maxAngle > 0.0001f) {
		glm::vec3 axis = glm::cross (result, forward);
		float axisLen = glm::length (axis);
		if (axisLen > 0.0001f) {
		    axis = axis / axisLen;
		    float rotAngle = (angle - maxAngle) * glm::pi<float> ();
		    glm::mat3 rot = glm::mat3 (glm::rotate (glm::mat4 (1.0f), rotAngle, axis));
		    result = rot * result;
		}
	    }
	}

	// Offset rotates result around right axis (tilts up/down)
	if (std::abs (offset) > 0.0001f) {
	    glm::mat3 rot = glm::mat3 (glm::rotate (glm::mat4 (1.0f), -offset, right));
	    result = rot * result;
	}

	// For 2D/orthographic particles (flags & 4 == 0), project direction onto XY plane.
	// curlNoise is 3D but z-drift is meaningless for 2D particles and causes
	// rope segments to diverge in depth, breaking visual connectivity.
	if (!getScene ().getScene ().camera.projection.isPerspective && (m_particle.flags & 4) == 0) {
	    result.z = 0.0f;
	    float len2d = glm::length (result);
	    if (len2d > 0.0001f) {
		result /= len2d;
	    }
	}

	// Apply speed and instance override
	glm::vec3 finalVel = result * speed * speedOverride->getFloat ();

	p.velocity += finalVel;
    };
}

InitializerFunc
CParticle::createMapSequenceBetweenControlPointsInitializer (const MapSequenceBetweenControlPointsInitializer& init) {
    return [this, &init, phase = 0.0f, direction = 1.0f] (ParticleInstance& particle) mutable {
	const auto point = [this] (const int index) {
	    const int clamped = std::clamp (index, 0, PARTICLE_CONTROL_POINT_COUNT - 1);
	    return clamped < static_cast<int> (m_controlPoints.size ())
		? m_controlPoints[clamped].position : glm::vec3 (0.0f);
	};
	initializeParticleBetweenControlPoints (
	    particle, init, point (init.controlPointStart), point (init.controlPointEnd), phase, direction
	);
    };
}

InitializerFunc
CParticle::createMapSequenceAroundControlPointInitializer (const MapSequenceAroundControlPointInitializer& init) {
    DynamicValue* countValue = init.count->value.get ();
    DynamicValue* boundsValue = init.bounds->value.get ();
    DynamicValue* speedMinValue = init.speedMin->value.get ();
    DynamicValue* speedMaxValue = init.speedMax->value.get ();
    DynamicValue* axisValue = init.axis->value.get ();
    DynamicValue* controlPointValue = init.controlPoint->value.get ();
    DynamicValue* speedOverride = getInstanceOverride ().speed->value.get ();
    const bool mirror = init.limitBehavior == "mirror";

    // The official initializer stores a normalized [0, 1] phase and a signed
    // 1/count step. Both are shared by every particle emitted by this initializer.
    return [this, countValue, boundsValue, speedMinValue, speedMaxValue, axisValue, controlPointValue,
	       speedOverride, mirror, phase = 0.0f, direction = 1.0f] (ParticleInstance& p) mutable {
	constexpr float normalizationEpsilon = 1.1920929e-7f;

	const int controlPoint = std::clamp (static_cast<int> (controlPointValue->getFloat ()), 0,
					     PARTICLE_CONTROL_POINT_COUNT - 1);
	const glm::vec3 centerPos = controlPoint < static_cast<int> (m_controlPoints.size ())
	    ? m_controlPoints[controlPoint].position
	    : glm::vec3 (0.0f);

	glm::vec3 axis = axisValue->getVec3 ();
	const float axisLength = glm::length (axis);
	if (axisLength <= normalizationEpsilon) {
	    axis = glm::vec3 (0.0f, 0.0f, 1.0f);
	} else {
	    axis /= axisLength;
	}

	// Wallpaper Engine constructs these two perpendicular vectors when it
	// compiles the initializer. This particular orientation is significant:
	// phase zero starts on basis2 and advances toward basis1.
	glm::vec3 basis1;
	const float axisXYLength = glm::length (glm::vec2 (axis));
	if (axisXYLength <= normalizationEpsilon) {
	    basis1 = glm::vec3 (1.0f, 0.0f, 0.0f);
	} else {
	    basis1 = glm::vec3 (axis.y, -axis.x, 0.0f) / axisXYLength;
	}
	glm::vec3 basis2 = glm::normalize (glm::cross (axis, basis1));
	// Positions and control points use reflected simulation coordinates. Build
	// the authored basis first, then reflect all three vectors to retain phase
	// direction as well as the plane's orientation.
	axis.y = -axis.y;
	basis1.y = -basis1.y;
	basis2.y = -basis2.y;

	const glm::vec2 bounds = boundsValue->getVec2 ();
	const float angle = (bounds.x + phase * (bounds.y - bounds.x)) * glm::two_pi<float> ();
	const float sine = std::sin (angle);
	const float cosine = std::cos (angle);
	const glm::vec3 radialDirection = cosine * basis2 + sine * basis1;
	const glm::vec3 tangentDirection = -sine * basis2 + cosine * basis1;

	// Keep the emitter's radius and its displacement along the selected axis;
	// only its angle around the control point is replaced by the sequence.
	const glm::vec3 relativePosition = p.position - centerPos;
	const float axialDistance = glm::dot (relativePosition, axis);
	const glm::vec3 radialPosition = relativePosition - axialDistance * axis;
	p.position = centerPos + radialDirection * glm::length (radialPosition) + axis * axialDistance;

	// Authored X/Y/Z speed components are tangent/radial/axial respectively.
	const glm::vec3 speed
	    = WallpaperEngine::Maths::randomVec3 (m_rng, speedMinValue->getVec3 (), speedMaxValue->getVec3 ());
	p.velocity += (tangentDirection * speed.x + radialDirection * speed.y + axis * speed.z)
	    * speedOverride->getFloat ();

	// The compiler clamps count to 0.0001, not to an integer. Fractional
	// counts (including shipped values such as 3.02) are intentional.
	const float count = std::max (0.0001f, countValue->getFloat ());
	const float step = direction / count;
	phase += step;
	if (phase > 1.0f) {
	    if (mirror) {
		phase = 1.0f - (phase - 1.0f);
		direction = -direction;
	    } else {
		phase = std::fmod (phase, 1.0f);
	    }
	} else if (phase < 0.0f) {
	    phase = -phase;
	    direction = -direction;
	}
    };
}

// ========== OPERATORS ==========

static float particleOperatorBlend (const float lifetimePosition, glm::vec4 blendTimes) {
    // Native 1401c2a40 expands coincident endpoints and selects the blended
    // opcode only when its lifetime envelope has a meaningful duration.
    blendTimes.x = std::min (blendTimes.x, blendTimes.y - 0.0001f);
    blendTimes.w = std::max (blendTimes.w, blendTimes.z + 0.0001f);
    float weight = 1.0f;
    if ((blendTimes.y > 0.01f || blendTimes.z < 0.99f)
	&& (blendTimes.z - blendTimes.y > 0.01f || blendTimes.y - blendTimes.x > 0.01f
	    || blendTimes.w - blendTimes.z > 0.01f)) {
	weight = std::clamp ((lifetimePosition - blendTimes.x) / (blendTimes.y - blendTimes.x), 0.0f, 1.0f)
	    * std::clamp ((blendTimes.w - lifetimePosition) / (blendTimes.w - blendTimes.z), 0.0f, 1.0f);
    }
    return weight;
}

glm::vec3 WallpaperEngine::Render::Objects::calculateParticleVelocityCap (
    const glm::vec3& velocity, const float maxSpeed, const float lifetimePosition, glm::vec4 blendTimes
) {
    const float speed = glm::length (velocity);
    if (speed == 0.0f) return velocity;
    const float weight = particleOperatorBlend (lifetimePosition, blendTimes);
    // Native 140244790 blends the velocity toward its capped value each tick;
    // it does not blend the limit or multiply by the simulation delta.
    return velocity * (1.0f + std::min (0.0f, maxSpeed / speed - 1.0f) * weight);
}

glm::vec3 WallpaperEngine::Render::Objects::calculateParticleMovementReduction (
    const glm::vec3& velocity, const float distance, const glm::vec2 distanceRange, const glm::vec2 reductions,
    const float deltaTime, const float lifetimePosition, const glm::vec4 blendTimes
) {
    const float span = distanceRange.y == distanceRange.x ? 1.0f : distanceRange.y - distanceRange.x;
    // Native rsqrt(0)*0 followed by MINPS selects the outer endpoint at the exact center.
    const float position = distance == 0.0f ? 1.0f : std::clamp ((distance - distanceRange.x) / span, 0.0f, 1.0f);
    // Native parser 1401cd3d1 also uses a unit delta for equal reduction endpoints.
    const float change = reductions.y == reductions.x ? 1.0f : reductions.y - reductions.x;
    const float damping = std::clamp ((reductions.x + position * change) * deltaTime, 0.0f, 1.0f);
    return velocity * (1.0f - damping * particleOperatorBlend (lifetimePosition, blendTimes));
}

void CParticle::setupOperators () {
    for (const auto& op : m_particle.operators) {
	if (!op) {
	    continue;
	}

	OperatorFunc func;

	if (op->is<MovementOperator> ()) {
	    func = createMovementOperator (*op->as<MovementOperator> ());
	} else if (op->is<CapVelocityOperator> ()) {
	    func = createCapVelocityOperator (*op->as<CapVelocityOperator> ());
	} else if (op->is<ReduceMovementNearControlPointOperator> ()) {
	    func = createReduceMovementNearControlPointOperator (*op->as<ReduceMovementNearControlPointOperator> ());
	} else if (op->is<AngularMovementOperator> ()) {
	    func = createAngularMovementOperator (*op->as<AngularMovementOperator> ());
	} else if (op->is<AlphaFadeOperator> ()) {
	    func = createAlphaFadeOperator (*op->as<AlphaFadeOperator> ());
	} else if (op->is<SizeChangeOperator> ()) {
	    func = createSizeChangeOperator (*op->as<SizeChangeOperator> ());
	} else if (op->is<AlphaChangeOperator> ()) {
	    func = createAlphaChangeOperator (*op->as<AlphaChangeOperator> ());
	} else if (op->is<ColorChangeOperator> ()) {
	    func = createColorChangeOperator (*op->as<ColorChangeOperator> ());
	} else if (op->is<TurbulenceOperator> ()) {
	    func = createTurbulenceOperator (*op->as<TurbulenceOperator> ());
	} else if (op->is<VortexOperator> ()) {
	    func = createVortexOperator (*op->as<VortexOperator> ());
	} else if (op->is<ControlPointAttractOperator> ()) {
	    func = createControlPointAttractOperator (*op->as<ControlPointAttractOperator> ());
	} else if (op->is<OscillateAlphaOperator> ()) {
	    func = createOscillateAlphaOperator (*op->as<OscillateAlphaOperator> ());
	} else if (op->is<OscillateSizeOperator> ()) {
	    func = createOscillateSizeOperator (*op->as<OscillateSizeOperator> ());
	} else if (op->is<OscillatePositionOperator> ()) {
	    func = createOscillatePositionOperator (*op->as<OscillatePositionOperator> ());
	} else {
	    sLog.out ("Unknown operator type");
	}

	if (func) {
	    m_operators.push_back (std::move (func));
	}
    }
}

OperatorFunc CParticle::createMovementOperator (const MovementOperator& op) {
    DynamicValue* dragValue = op.drag->value.get ();
    DynamicValue* gravityValue = op.gravity->value.get ();
    DynamicValue* speedOverride = getInstanceOverride ().speed->value.get ();

    return [dragValue, gravityValue, speedOverride] (
	       std::vector<ParticleInstance>& particles, uint32_t count, const std::vector<ControlPointData>&, float,
	       float dt
	   ) {
	float speed = speedOverride->getFloat ();
	float drag = dragValue->getFloat ();
	glm::vec3 gravity = gravityValue->getVec3 ();
	// Flip gravity Y for centered space
	gravity.y = -gravity.y;

	for (uint32_t i = 0; i < count; i++) {
	    auto& p = particles[i];
	    if (!p.alive) {
		continue;
	    }

	    // Native movement applies gravity before advancing position, then drag.
	    // Otherwise a large drag*dt discards every frame's force without motion.
	    p.velocity += gravity * dt * speed;
	    p.position += p.velocity * dt;

	    // Apply drag (velocity decay)
	    // Clamp to prevent velocity reversal if drag*dt > 1.0
	    float dragFactor = 1.0f - (drag * dt);
	    if (dragFactor < 0.0f) {
		dragFactor = 0.0f;
	    }
	    p.velocity *= dragFactor;
	}
    };
}

OperatorFunc CParticle::createCapVelocityOperator (const CapVelocityOperator& op) {
    DynamicValue* maxSpeedValue = op.maxSpeed ? op.maxSpeed->value.get () : nullptr;
    const float defaultMaxSpeed = getScene ().getScene ().camera.projection.isPerspective ? 1.0f : 100.0f;
    return [maxSpeedValue, defaultMaxSpeed, blendTimes = op.blendTimes] (
	       std::vector<ParticleInstance>& particles, uint32_t count, const std::vector<ControlPointData>&,
	       float, float
	   ) {
	const float maxSpeed = maxSpeedValue ? maxSpeedValue->getFloat () : defaultMaxSpeed;
	for (uint32_t i = 0; i < count; i++) {
	    auto& particle = particles[i];
	    if (particle.alive) {
		particle.velocity = calculateParticleVelocityCap (
		    particle.velocity, maxSpeed, particle.getLifetimePos (), blendTimes
		);
	    }
	}
    };
}

OperatorFunc CParticle::createReduceMovementNearControlPointOperator (const ReduceMovementNearControlPointOperator& op) {
    const bool perspective = getScene ().getScene ().camera.projection.isPerspective;
    return [&op, perspective] (
        std::vector<ParticleInstance>& particles, uint32_t count,
        const std::vector<ControlPointData>& controlPoints, float, float dt
    ) {
        const glm::vec2 distances (
            op.distanceInner ? op.distanceInner->value->getFloat () : (perspective ? 0.5f : 100.0f),
            op.distanceOuter ? op.distanceOuter->value->getFloat () : (perspective ? 1.0f : 350.0f)
        );
        const glm::vec2 reductions (op.reductionInner->value->getFloat (), op.reductionOuter->value->getFloat ());
        const glm::vec3 center = controlPoints[op.controlPoint].position;
        for (uint32_t i = 0; i < count; ++i) {
            auto& particle = particles[i];
            if (!particle.alive) continue;
            particle.velocity = calculateParticleMovementReduction (
                particle.velocity, glm::length (particle.position - center), distances, reductions,
                dt, particle.getLifetimePos (), op.blendTimes
            );
        }
    };
}

void WallpaperEngine::Render::Objects::applyParticleAngularMovement (
    ParticleInstance& particle, const glm::vec3& force, const float drag,
    const float rawDelta, const float forceDelta, const float lifetimeWeight
) {
    // Native opcodes 2/28 accelerate before integrating rotation, then damp
    // velocity. The envelope therefore weights this step's force contribution
    // to rotation twice. Angular force/rotation use raw time; drag uses force time.
    particle.angularVelocity += force * (rawDelta * lifetimeWeight);
    particle.rotation += particle.angularVelocity * (rawDelta * lifetimeWeight);
    const float damping = std::min (drag * forceDelta, 1.0f - std::numeric_limits<float>::epsilon ());
    particle.angularVelocity *= 1.0f - lifetimeWeight * damping;
    // Native preserves accumulated angles; wrapping here changes shader inputs.
}

OperatorFunc CParticle::createAngularMovementOperator (const AngularMovementOperator& op) {
    return [this, &op] (
        std::vector<ParticleInstance>& particles, uint32_t count, const std::vector<ControlPointData>&,
        float, float dt
    ) {
        const float speed = (m_particle.flags & 16) != 0 ? 1.0f : getInstanceOverride ().speed->value->getFloat ();
        const glm::vec3 force = op.force->value->getVec3 () * speed;
        const float drag = op.drag->value->getFloat ();
        for (uint32_t i = 0; i < count; ++i) {
            auto& particle = particles[i];
            if (!particle.alive) continue;
            // The fork currently supplies one delta; native force-clock mapping
            // and the half-step scheduler remain separate parity work.
            applyParticleAngularMovement (
                particle, force, drag, dt, dt, particleOperatorBlend (particle.getLifetimePos (), op.blendTimes)
            );
        }
    };
}

OperatorFunc CParticle::createAlphaFadeOperator (const AlphaFadeOperator& op) {
    m_hasAlphaOperators = true;
    DynamicValue* fadeInTimeValue = op.fadeInTime->value.get ();
    DynamicValue* fadeOutTimeValue = op.fadeOutTime->value.get ();

    return
	[fadeInTimeValue, fadeOutTimeValue] (
	    std::vector<ParticleInstance>& particles, uint32_t count, const std::vector<ControlPointData>&, float, float
	) {
	    float fadeInTime = fadeInTimeValue->getFloat ();
	    float fadeOutTime = fadeOutTimeValue->getFloat ();

	    for (uint32_t i = 0; i < count; i++) {
		auto& p = particles[i];
		if (!p.alive) {
		    continue;
		}

		float life = p.getLifetimePos ();

		if (life <= fadeInTime) {
		    float fade = WallpaperEngine::Maths::fadeValue (life, 0.0f, fadeInTime, 0.0f, 1.0f);
		    p.alpha *= fade;
		} else if (life > fadeOutTime) {
		    float fade = 1.0f - WallpaperEngine::Maths::fadeValue (life, fadeOutTime, 1.0f, 0.0f, 1.0f);
		    p.alpha *= fade;
		}
	    }
	};
}

OperatorFunc CParticle::createSizeChangeOperator (const SizeChangeOperator& op) {
    DynamicValue* startTimeValue = op.startTime->value.get ();
    DynamicValue* endTimeValue = op.endTime->value.get ();
    DynamicValue* startValueValue = op.startValue->value.get ();
    DynamicValue* endValueValue = op.endValue->value.get ();

    return
	[startTimeValue, endTimeValue, startValueValue, endValueValue] (
	    std::vector<ParticleInstance>& particles, uint32_t count, const std::vector<ControlPointData>&, float, float
	) {
	    float startTime = startTimeValue->getFloat ();
	    float endTime = endTimeValue->getFloat ();
	    float startValue = startValueValue->getFloat ();
	    float endValue = endValueValue->getFloat ();

	    for (uint32_t i = 0; i < count; i++) {
		auto& p = particles[i];
		if (!p.alive) {
		    continue;
		}

		float life = p.getLifetimePos ();
		float multiplier = WallpaperEngine::Maths::fadeValue (life, startTime, endTime, startValue, endValue);
		p.size *= multiplier;
	    }
	};
}

OperatorFunc CParticle::createAlphaChangeOperator (const AlphaChangeOperator& op) {
    m_hasAlphaOperators = true;
    DynamicValue* startTimeValue = op.startTime->value.get ();
    DynamicValue* endTimeValue = op.endTime->value.get ();
    DynamicValue* startValueValue = op.startValue->value.get ();
    DynamicValue* endValueValue = op.endValue->value.get ();

    return
	[startTimeValue, endTimeValue, startValueValue, endValueValue] (
	    std::vector<ParticleInstance>& particles, uint32_t count, const std::vector<ControlPointData>&, float, float
	) {
	    float startTime = startTimeValue->getFloat ();
	    float endTime = endTimeValue->getFloat ();
	    float startValue = startValueValue->getFloat ();
	    float endValue = endValueValue->getFloat ();

	    for (uint32_t i = 0; i < count; i++) {
		auto& p = particles[i];
		if (!p.alive) {
		    continue;
		}

		float life = p.getLifetimePos ();
		float multiplier = WallpaperEngine::Maths::fadeValue (life, startTime, endTime, startValue, endValue);
		p.alpha *= multiplier;
	    }
	};
}

OperatorFunc CParticle::createColorChangeOperator (const ColorChangeOperator& op) {
    DynamicValue* startTimeValue = op.startTime->value.get ();
    DynamicValue* endTimeValue = op.endTime->value.get ();
    DynamicValue* startValueValue = op.startValue->value.get ();
    DynamicValue* endValueValue = op.endValue->value.get ();

    return
	[startTimeValue, endTimeValue, startValueValue, endValueValue] (
	    std::vector<ParticleInstance>& particles, uint32_t count, const std::vector<ControlPointData>&, float, float
	) {
	    float startTime = startTimeValue->getFloat ();
	    float endTime = endTimeValue->getFloat ();
	    glm::vec3 startValue = startValueValue->getVec3 ();
	    glm::vec3 endValue = endValueValue->getVec3 ();

	    for (uint32_t i = 0; i < count; i++) {
		auto& p = particles[i];
		if (!p.alive) {
		    continue;
		}

		float life = p.getLifetimePos ();

		glm::vec3 color;
		color.r = WallpaperEngine::Maths::fadeValue (life, startTime, endTime, startValue.r, endValue.r);
		color.g = WallpaperEngine::Maths::fadeValue (life, startTime, endTime, startValue.g, endValue.g);
		color.b = WallpaperEngine::Maths::fadeValue (life, startTime, endTime, startValue.b, endValue.b);

		p.color = p.initial.color * color;
	    }
	};
}

OperatorFunc CParticle::createTurbulenceOperator (const TurbulenceOperator& op) {
    const bool perspective = getScene ().getScene ().camera.projection.isPerspective;
    DynamicValue* scaleValue = op.scale ? op.scale->value.get () : nullptr;
    DynamicValue* speedMinValue = op.speedMin ? op.speedMin->value.get () : nullptr;
    DynamicValue* speedMaxValue = op.speedMax ? op.speedMax->value.get () : nullptr;
    DynamicValue* timeScaleValue = op.timeScale ? op.timeScale->value.get () : nullptr;
    DynamicValue* maskValue = op.mask ? op.mask->value.get () : nullptr;
    DynamicValue* phaseMinValue = op.phaseMin->value.get ();
    DynamicValue* phaseMaxValue = op.phaseMax->value.get ();
    DynamicValue* speedOverride = getInstanceOverride ().speed->value.get ();
    DynamicValue* rateOverride = getInstanceOverride ().rate->value.get ();

    if (m_turbulenceRateSubscriptions.empty ()) {
	m_turbulenceRate = std::max (0.01f, rateOverride->getFloat ());
	const auto subscribe = [this] (const UserSetting& setting) {
	    m_turbulenceRateSubscriptions.emplace_back (Data::Utils::ScopeGuard (setting.value->listen (
		[this] (const DynamicValue&, DynamicValue::UpdateSource) { m_turbulenceRateDirty = true; }
	    )));
	};
	const auto& settings = getInstanceOverride ();
	// Native rate setters do not invalidate derived parameters. These setters
	// do, even for unchanged values; read the final rate when simulation runs.
	for (const auto* setting : { settings.alpha.get (), settings.size.get (), settings.count.get (),
		settings.speed.get (), settings.lifetime.get (), settings.brightness.get (), settings.colorn.get () }) {
	    subscribe (*setting);
	}
	for (const auto& [id, setting] : settings.controlPointOffsets) subscribe (*setting);
    }

    // TODO: Audio processing support
    // DynamicValue* audioModeValue = op.audioProcessingMode->value.get ();
    // DynamicValue* audioBoundsValue = op.audioProcessingBounds->value.get ();
    // DynamicValue* audioExponentValue = op.audioProcessingExponent->value.get ();
    // DynamicValue* audioFreqStartValue = op.audioProcessingFrequencyStart->value.get ();
    // DynamicValue* audioFreqEndValue = op.audioProcessingFrequencyEnd->value.get ();

    return [this, perspective, scaleValue, timeScaleValue, maskValue, speedOverride, speedMinValue, speedMaxValue,
            phaseMinValue, phaseMaxValue, blendTimes = op.blendTimes] (
	       std::vector<ParticleInstance>& particles, uint32_t count, const std::vector<ControlPointData>&,
	       float, float dt
	   ) {
	const float noiseScale = scaleValue ? scaleValue->getFloat () : (perspective ? 0.5f : 0.01f);
	const float timeScale = timeScaleValue ? timeScaleValue->getFloat () : (perspective ? 1.0f : 20.0f);
	// Native samples owner time with the cached rate, not integrated particle time.
	const float timeOffset = (timeScale * m_turbulenceRate) * getScene ().getTime ();
	const glm::vec3 mask = maskValue ? maskValue->getVec3 () : glm::vec3 (1.0f, 1.0f, perspective ? 1.0f : 0.0f);
	// Flag16 skips native's speed updater, but keeps the rate/time updater.
	// Scale endpoints before forming the span, including negative multipliers.
	const float speed = (m_particle.flags & 16) != 0 ? 1.0f : speedOverride->getFloat ();
	const float speedMin = (speedMinValue ? speedMinValue->getFloat () : (perspective ? 1.0f : 500.0f)) * speed;
	const float speedSpan = (speedMaxValue ? speedMaxValue->getFloat () : (perspective ? 5.0f : 1000.0f)) * speed - speedMin;
	const float phaseSpan = phaseMaxValue->getFloat () - phaseMinValue->getFloat ();

	for (size_t i = 0; i < count; ++i) {
	    ParticleInstance& p = particles[i];
	    if (!p.alive) {
		continue;
	    }
	    if (p.oscillationRandom < 0.0f) {
		p.oscillationRandom = WallpaperEngine::Maths::randomFloat (m_rng, 0.0f, 1.0f);
	    }
	    const float turbSpeed = speedMin + p.oscillationRandom * speedSpan;
	    // Native shares one particle fraction with the oscillators and uses
	    // only the phase span; the stored phase minimum is not added.
	    const float phase = p.oscillationRandom * phaseSpan;

	    glm::vec3 noisePos = p.position;
	    noisePos.y = -noisePos.y;
	    noisePos += glm::vec3 (phase + timeOffset);
	    noisePos *= noiseScale;

	    glm::vec3 force (
		simplexNoise3D (noisePos.x, noisePos.y, noisePos.z),
		-simplexNoise3D (noisePos.z, noisePos.x, noisePos.y),
		simplexNoise3D (noisePos.y, noisePos.z, noisePos.x)
	    );
	    // Native keeps the scalar field's magnitude and signed speed; it neither
	    // takes a curl nor normalizes the three cyclic samples.
	    force *= mask * turbSpeed;
	    p.velocity += force * dt * particleOperatorBlend (p.getLifetimePos (), blendTimes);
	}
    };
}

glm::vec3 WallpaperEngine::Render::Objects::calculateParticleVortexV2 (
    const glm::vec3& displacement, const glm::vec3& velocity, const glm::vec3& axis, const int flags,
    const glm::vec2 distances, const glm::vec2 speeds, const float centerForce, const glm::vec4 ring,
    const float rawDt, const float forceDt, const float lifetimeWeight
) {
    if (rawDt <= 0.0f || lifetimeWeight == 0.0f) return glm::vec3 (0.0f);
    const glm::vec3 radial = (flags & 1) != 0
        ? displacement - axis * glm::dot (displacement, axis) : displacement;
    const float distance = glm::length (radial);
    // Native rsqrt is singular at the center. Keep that undefined case finite.
    if (distance <= std::numeric_limits<float>::min ()) return glm::vec3 (0.0f);
    const glm::vec3 predicted = radial + velocity * rawDt;
    const float predictedDistance = glm::length (predicted);
    const bool ringShape = (flags & 4) != 0;
    const float span = ringShape ? ring.z : distances.y - distances.x;
    const float offset = ringShape ? std::abs (ring.x - distance) - ring.y : distance - distances.x;
    const float t = std::clamp (offset * (span == 0.0f ? 1.0f : 1.0f / span), 0.0f, 1.0f);
    float correction = (flags & 2) != 0 && predictedDistance > std::numeric_limits<float>::min ()
        ? (distance / predictedDistance - 1.0f) * centerForce / rawDt : 0.0f;
    if (ringShape) {
        const float pull = 1.0f - t;
        correction += std::copysign (pull == 1.0f ? 0.0f : pull, ring.x - distance) * rawDt * ring.w;
    }
    // Native cross(radial, axis) reverses under our Y reflection. Do not
    // normalize the tangent separately: sphere poles retain their attenuation.
    const glm::vec3 tangent = glm::cross (axis, radial / distance);
    return (tangent * (glm::mix (speeds.x, speeds.y, t) * forceDt) + predicted * correction) * lifetimeWeight;
}

OperatorFunc CParticle::createVortexV2Operator (const VortexOperator& op) {
    const bool perspective = getScene ().getScene ().camera.projection.isPerspective;
    return [this, &op, perspective] (
        std::vector<ParticleInstance>& particles, uint32_t count,
        const std::vector<ControlPointData>& controlPoints, float, float dt
    ) {
        if (op.controlPoint >= static_cast<int> (controlPoints.size ())) return;
        const auto value = [] (const UserSettingUniquePtr& setting, float fallback) {
            return setting ? setting->value->getFloat () : fallback;
        };
        const glm::vec2 distances (value (op.distanceInner, perspective ? 1.0f : 500.0f),
                                  value (op.distanceOuter, perspective ? 2.0f : 650.0f));
        const glm::vec4 ring (value (op.ringRadius, perspective ? 1.0f : 300.0f),
                             value (op.ringWidth, perspective ? .2f : 50.0f),
                             value (op.ringPullDistance, perspective ? .25f : 50.0f),
                             value (op.ringPullForce, perspective ? .05f : 10.0f));
        const float speed = (m_particle.flags & 16) != 0 ? 1.0f : getInstanceOverride ().speed->value->getFloat ();
        const auto& recorder = getScene ().getAudioContext ().getRecorder ();
        auto firstBand = std::min (static_cast<uint32_t> (op.audioProcessingFrequencyStart->value->getInt ()), 15u);
        auto lastBand = std::min (static_cast<uint32_t> (op.audioProcessingFrequencyEnd->value->getInt ()), 15u);
        if (firstBand > lastBand) std::swap (firstBand, lastBand);
        const float audio = calculateParticleAudioResponse (
            recorder.audio16Left, recorder.audio16Right, op.audioProcessingMode->value->getInt (),
            op.audioProcessingBounds->value->getVec2 (), op.audioProcessingExponent->value->getFloat (), firstBand, lastBand
        );
        const glm::vec2 speeds = glm::vec2 (value (op.speedInner, perspective ? 1.0f : 2500.0f),
                                          op.speedOuter->value->getFloat ()) * speed * audio;
        glm::vec3 axis = op.axis->value->getVec3 ();
        axis = glm::dot (axis, axis) < .001f ? glm::vec3 (0, 0, 1) : glm::normalize (axis);
        axis.y = -axis.y;
        if ((m_particle.flags & 1) != 0) axis = glm::mat3 (m_controlPointTransform) * axis;
        const glm::vec3 center = controlPoints[op.controlPoint].position;
        const float centerForce = op.centerForce->value->getFloat ();
        for (uint32_t i = 0; i < count; ++i) {
            auto& particle = particles[i];
            if (!particle.alive) continue;
            // Native has a separately adjusted force clock. The fork currently
            // supplies one simulation delta; clock/scheduler parity is separate.
            particle.velocity += calculateParticleVortexV2 (
                particle.position - center, particle.velocity, axis, op.flags, distances, speeds,
                centerForce, ring, dt, dt, particleOperatorBlend (particle.getLifetimePos (), op.blendTimes)
            );
        }
    };
}

OperatorFunc CParticle::createVortexOperator (const VortexOperator& op) {
    if (op.nativeV2) return createVortexV2Operator (op);
    int controlPoint = op.controlPoint;
    int flags = op.flags;
    DynamicValue* axisValue = op.axis->value.get ();
    DynamicValue* offsetValue = op.offset->value.get ();
    DynamicValue* distanceInnerValue = op.distanceInner->value.get ();
    DynamicValue* distanceOuterValue = op.distanceOuter->value.get ();
    DynamicValue* speedInnerValue = op.speedInner->value.get ();
    DynamicValue* speedOuterValue = op.speedOuter->value.get ();
    DynamicValue* centerForceValue = op.centerForce->value.get ();
    DynamicValue* ringRadiusValue = op.ringRadius->value.get ();
    DynamicValue* ringWidthValue = op.ringWidth->value.get ();
    DynamicValue* ringPullDistanceValue = op.ringPullDistance->value.get ();
    DynamicValue* ringPullForceValue = op.ringPullForce->value.get ();
    DynamicValue* audioModeValue = op.audioProcessingMode->value.get ();
    DynamicValue* speedOverride = getInstanceOverride ().speed->value.get ();

    // Check if audio processing is enabled
    int audioMode = static_cast<int> (audioModeValue->getFloat ());

    // Extract flag bits
    bool infiniteAxis = (flags & 1) != 0;
    bool maintainDistance = (flags & 2) != 0;
    bool ringShape = (flags & 4) != 0;

    return [controlPoint, axisValue, offsetValue, distanceInnerValue, distanceOuterValue, speedInnerValue,
	    speedOuterValue, centerForceValue, ringRadiusValue, ringWidthValue, ringPullDistanceValue,
	    ringPullForceValue, audioMode, infiniteAxis, maintainDistance, ringShape, speedOverride] (
	       std::vector<ParticleInstance>& particles, uint32_t count,
	       const std::vector<ControlPointData>& controlPoints, float, float dt
	   ) {
	// Audio modulation (when implemented, this will sample from audio context)
	float audioAmplitude = 0.0f; // TODO: Sample from AudioContext when audio processing is implemented

	// If audio mode is enabled but no audio, skip vortex entirely
	if (audioMode > 0 && audioAmplitude == 0.0f) {
	    return;
	}

	glm::vec3 axis = axisValue->getVec3 ();
	glm::vec3 offset = offsetValue->getVec3 ();
	float distanceInner = distanceInnerValue->getFloat ();
	float distanceOuter = distanceOuterValue->getFloat ();
	float speedInner = speedInnerValue->getFloat ();
	float speedOuter = speedOuterValue->getFloat ();
	float centerForce = centerForceValue->getFloat ();
	float ringRadius = ringRadiusValue->getFloat ();
	float ringWidth = ringWidthValue->getFloat ();
	float ringPullDistance = ringPullDistanceValue->getFloat ();
	float ringPullForce = ringPullForceValue->getFloat ();

	// Apply audio modulation to speeds
	if (audioMode > 0) {
	    speedInner *= (1.0f + audioAmplitude);
	    speedOuter *= (1.0f + audioAmplitude);
	}

	// Get vortex center from control point
	glm::vec3 center = glm::vec3 (0.0f);
	if (controlPoint >= 0 && controlPoint < static_cast<int> (controlPoints.size ())) {
	    center = controlPoints[controlPoint].position + offset;
	} else {
	    center = offset;
	}

	// Normalize axis
	if (glm::length (axis) > 0.0f) {
	    axis = glm::normalize (axis);
	} else {
	    axis = glm::vec3 (0.0f, 0.0f, 1.0f); // Default to Z-axis
	}

	for (uint32_t i = 0; i < count; i++) {
	    auto& p = particles[i];
	    if (!p.alive) {
		continue;
	    }

	    // Calculate vector from center to particle
	    glm::vec3 toParticle = p.position - center;

	    // For infinite axis mode, project onto plane perpendicular to axis (cylinder shape)
	    // Otherwise use full 3D distance (sphere shape)
	    float axialDistance = 0.0f;
	    glm::vec3 radialVector = toParticle;
	    if (infiniteAxis) {
		// Project out the axis component
		axialDistance = glm::dot (toParticle, axis);
		radialVector = toParticle - axis * axialDistance;
	    }

	    float distance = glm::length (radialVector);

	    // Compute tangent direction (perpendicular to both axis and radial vector)
	    glm::vec3 tangent = glm::cross (axis, radialVector);
	    if (glm::length (tangent) > 0.001f) {
		tangent = glm::normalize (tangent);
	    } else {
		continue; // Particle is on the axis
	    }

	    // Calculate spin speed and apply forces based on mode
	    float speed = 0.0f;
	    glm::vec3 radialForce = glm::vec3 (0.0f);

	    if (ringShape) {
		// Ring mode: hollow center with ring-shaped influence zone
		float ringInner = ringRadius - ringWidth * 0.5f;
		float ringOuter = ringRadius + ringWidth * 0.5f;

		if (distance < ringInner) {
		    // Inside the ring's hollow center - no spin, but may be pulled outward
		    speed = 0.0f;
		} else if (distance <= ringOuter) {
		    // Inside the ring - full effect
		    float t = (distance - ringInner) / ringWidth;
		    speed = glm::mix (speedInner, speedOuter, t);
		} else if (distance <= ringOuter + ringPullDistance) {
		    // Outside ring but within pull distance - attract toward ring
		    float pullT = (distance - ringOuter) / ringPullDistance;
		    speed = speedOuter * (1.0f - pullT);
		    // Pull toward ring
		    if (distance > 0.001f) {
			glm::vec3 towardRing = -glm::normalize (radialVector);
			radialForce = towardRing * ringPullForce * pullT;
		    }
		} else {
		    // Too far from ring - no effect
		    speed = 0.0f;
		}
	    } else {
		// Standard vortex mode
		float disMid = distanceOuter - distanceInner + 0.1f;

		if (disMid < 0 || distance < distanceInner) {
		    speed = speedInner;
		} else if (distance > distanceOuter) {
		    speed = speedOuter;
		} else {
		    float t = (distance - distanceInner) / disMid;
		    speed = glm::mix (speedInner, speedOuter, t);
		}
	    }

	    // Apply tangential velocity (spinning)
	    p.velocity += tangent * speed * dt * speedOverride->getFloat ();

	    // Apply radial force (ring pull)
	    p.velocity += radialForce * dt * speedOverride->getFloat ();

	    // Apply center force when maintain distance is enabled
	    if (maintainDistance && distance > 0.001f) {
		glm::vec3 towardCenter = -glm::normalize (radialVector);
		p.velocity += towardCenter * centerForce * dt * speedOverride->getFloat ();
	    }
	}
    };
}

OperatorFunc CParticle::createControlPointAttractOperator (const ControlPointAttractOperator& op) {
    // The authored defaults use pixels in an orthographic scene and world units
    // in a perspective scene. The project is still being parsed when operators
    // are read, so resolve omitted settings here rather than guessing in the parser.
    const bool isPerspective = getScene ().getScene ().camera.projection.isPerspective;
    const float defaultScale = isPerspective ? 20.0f : 512.0f;
    const float defaultThreshold = isPerspective ? 5.0f : 512.0f;

    return [this, &op, defaultScale, defaultThreshold] (
	       std::vector<ParticleInstance>& particles, uint32_t count,
	       const std::vector<ControlPointData>& controlPoints, float, float dt
	   ) {
	const float speed = (m_particle.flags & 16) != 0 ? 1.0f : getInstanceOverride ().speed->value->getFloat ();
	const float scale = (op.scale ? op.scale->value->getFloat () : defaultScale) * speed;
	const float threshold = op.threshold ? op.threshold->value->getFloat () : defaultThreshold;

	// Get control point position
	if (op.controlPoint < 0 || op.controlPoint >= static_cast<int> (controlPoints.size ())) {
	    return;
	}

	// Native uses only the selected control point; origin/offset do not move it.
	const glm::vec3 center = controlPoints[op.controlPoint].position;

	// Apply attraction force to all particles within threshold
	for (uint32_t i = 0; i < count; i++) {
	    auto& p = particles[i];
	    if (!p.alive) {
		continue;
	    }

	    p.velocity += calculateControlPointAttraction (
		center - p.position, scale, threshold, dt, (op.flags & 2) != 0,
		particleOperatorBlend (p.getLifetimePos (), op.blendTimes)
	    );
	}
    };
}

float WallpaperEngine::Render::Objects::calculateParticleOscillationMultiplier (
    const ParticleInstance& particle, const glm::vec2 frequencyRange, const glm::vec2 phaseRange,
    const glm::vec2 scaleRange, const glm::vec4 blendTimes
) {
    // Native opcodes 8/30 (alpha) and 9/31 (size) use the same particle random fraction for frequency,
    // phase and amplitude. Phase offsets age before multiplication by frequency.
    const float random = particle.oscillationRandom;
    const float frequency = glm::mix (frequencyRange.x, frequencyRange.y, random);
    const float phase = glm::mix (phaseRange.x, phaseRange.y, random);
    const float minimum = scaleRange.x;
    const float amplitude = (scaleRange.y - minimum) * random;
    const float multiplier = minimum + amplitude * 0.5f * (1.0f + std::sin ((particle.age + phase) * frequency));
    const float weight = particleOperatorBlend (particle.getLifetimePos (), blendTimes);
    return 1.0f + (multiplier - 1.0f) * weight;
}

OperatorFunc CParticle::createOscillateAlphaOperator (const OscillateAlphaOperator& op) {
    m_hasAlphaOperators = true;
    return [this, &op] (
        std::vector<ParticleInstance>& particles, uint32_t count, const std::vector<ControlPointData>&, float, float
    ) {
        const glm::vec2 frequencies (op.frequencyMin->value->getFloat (), op.frequencyMax->value->getFloat ());
        const glm::vec2 phases (op.phaseMin->value->getFloat (), op.phaseMax->value->getFloat ());
        const glm::vec2 scales (op.scaleMin->value->getFloat (), op.scaleMax->value->getFloat ());
        for (uint32_t i = 0; i < count; ++i) {
            auto& particle = particles[i];
            if (!particle.alive) continue;
            if (particle.oscillationRandom < 0.0f) {
                particle.oscillationRandom = WallpaperEngine::Maths::randomFloat (m_rng, 0.0f, 1.0f);
            }
            particle.alpha *= calculateParticleOscillationMultiplier (particle, frequencies, phases, scales, op.blendTimes);
        }
    };
}

OperatorFunc CParticle::createOscillateSizeOperator (const OscillateSizeOperator& op) {
    return [this, &op] (
        std::vector<ParticleInstance>& particles, uint32_t count, const std::vector<ControlPointData>&, float, float
    ) {
        const glm::vec2 frequencies (op.frequencyMin->value->getFloat (), op.frequencyMax->value->getFloat ());
        const glm::vec2 phases (op.phaseMin->value->getFloat (), op.phaseMax->value->getFloat ());
        const glm::vec2 scales (op.scaleMin->value->getFloat (), op.scaleMax->value->getFloat ());
        for (uint32_t i = 0; i < count; ++i) {
            auto& particle = particles[i];
            if (!particle.alive) continue;
            if (particle.oscillationRandom < 0.0f) {
                particle.oscillationRandom = WallpaperEngine::Maths::randomFloat (m_rng, 0.0f, 1.0f);
            }
            particle.size *= calculateParticleOscillationMultiplier (particle, frequencies, phases, scales, op.blendTimes);
        }
    };
}

glm::vec3 WallpaperEngine::Render::Objects::calculateParticlePositionOscillation (
    const ParticleInstance& particle, const glm::vec2 frequencyRange, const glm::vec2 phaseRange,
    const glm::vec2 scaleRange, const glm::vec3 mask, const glm::vec4 blendTimes, const float deltaTime
) {
    // Native opcodes 7/29 integrate the finite sine difference, including a
    // negative previous age at birth. X/Z share a wave; Y offsets time by r*2pi.
    const float random = particle.oscillationRandom;
    const float frequency = glm::mix (frequencyRange.x, frequencyRange.y, random);
    const float time = particle.age + glm::mix (phaseRange.x, phaseRange.y, random);
    const float yTime = time + random * glm::two_pi<float> ();
    const float amplitude = glm::mix (scaleRange.x, scaleRange.y, random)
        * particleOperatorBlend (particle.getLifetimePos (), blendTimes);
    const float xz = std::sin (time * frequency) - std::sin ((time - deltaTime) * frequency);
    const float y = std::sin (yTime * frequency) - std::sin ((yTime - deltaTime) * frequency);
    return glm::vec3 (xz, y, xz) * mask * amplitude;
}

OperatorFunc CParticle::createOscillatePositionOperator (const OscillatePositionOperator& op) {
    const float defaultScaleMax = getScene ().getScene ().camera.projection.isPerspective ? 0.5f : 10.0f;
    return [this, &op, defaultScaleMax] (
        std::vector<ParticleInstance>& particles, uint32_t count, const std::vector<ControlPointData>&, float,
        float dt
    ) {
        // Native updater 1401d1908 scales frequency endpoints by instance speed;
        // particle flag 0x10 suppresses that updater, not the oscillation itself.
        const float speed = (m_particle.flags & 16) != 0 ? 1.0f : getInstanceOverride ().speed->value->getFloat ();
        const glm::vec2 frequencies = glm::vec2 (op.frequencyMin->value->getFloat (), op.frequencyMax->value->getFloat ()) * speed;
        const glm::vec2 phases (op.phaseMin->value->getFloat (), op.phaseMax->value->getFloat ());
        const glm::vec2 scales (op.scaleMin->value->getFloat (), op.scaleMax ? op.scaleMax->value->getFloat () : defaultScaleMax);
        // Authored displacement is Y-up; particle simulation coordinates are Y-down.
        const glm::vec3 mask = op.mask->value->getVec3 () * glm::vec3 (1.0f, -1.0f, 1.0f);
        for (uint32_t i = 0; i < count; ++i) {
            auto& particle = particles[i];
            if (!particle.alive) continue;
            if (particle.oscillationRandom < 0.0f) {
                particle.oscillationRandom = WallpaperEngine::Maths::randomFloat (m_rng, 0.0f, 1.0f);
            }
            particle.position += calculateParticlePositionOscillation (particle, frequencies, phases, scales, mask, op.blendTimes, dt);
        }
    };
}

// ========== RENDERING ==========

void CParticle::setupPass () {
    if (!m_particle.material || !m_particle.material->material || m_particle.material->material->passes.empty ()) {
	sLog.error ("No valid material for particle ", m_particle.name);
	return;
    }

    const auto& firstPass = **m_particle.material->material->passes.begin ();

    // Build override with particle-specific combos
    m_passOverride = std::make_unique<ImageEffectPassOverride> ();
    m_passOverride->combos["THICKFORMAT"] = 1;
    if (m_useRopeRenderer) {
	m_passOverride->shaderOverride = "genericropeparticle";
    }
    if (m_spritesheetFrames > 0) {
	m_passOverride->combos["SPRITESHEET"] = 1;
    }
    if (m_useTrailRenderer) {
	m_passOverride->combos["TRAILRENDERER"] = 1;
    }

    // Force texture 0 to use the input (particle texture) rather than the shader's
    // default "util/white" annotation, which would override it in setupRenderTexture()
    m_passBinds = { { 0, "previous" } };

    // Check if material uses REFRACT combo
    auto refractIt = firstPass.combos.find ("REFRACT");
    m_hasRefract = refractIt != firstPass.combos.end () && refractIt->second != 0;

    // Create the FBO provider for CPass
    m_passFBOProvider = std::make_shared<FBOProvider> (this);

    // For REFRACT: create a copy FBO that shadows _rt_FullFrameBuffer.
    // The REFRACT shader reads g_Texture3 (= _rt_FullFrameBuffer) while we render TO the scene FBO.
    // Reading from the same FBO being rendered to is undefined behavior in OpenGL, causing
    // black reads on NVIDIA. By placing a copy FBO with the same name in our FBOProvider,
    // CPass resolves g_Texture3 to the copy instead. We blit the scene content before each render.
    if (m_hasRefract) {
	const glm::vec2 size = getScene ().getOutputSize ();
	m_refractFBO = m_passFBOProvider->create (
	    "_rt_FullFrameBuffer", this->getScene ().getColorFormat (), TextureFlags_ClampUVs, 1.0f, size, size
	);
    }

    // Create CPass with the WP particle shader
    m_pass = new Effects::CPass (*this, m_passFBOProvider, firstPass, *m_passOverride, m_passBinds, std::nullopt);

    // Set destination to scene FBO and input to particle texture
    m_pass->setDestination (getScene ().getFBO ());
    m_pass->setInput (getTexture ());

    // Set matrix pointers - CPass will dereference these each frame
    m_pass->setModelViewProjectionMatrix (&m_mvpMatrix);
    m_pass->setModelViewProjectionMatrixInverse (&m_mvpMatrixInverse);
    m_pass->setModelMatrix (&m_modelMatrix);
    m_pass->setViewProjectionMatrix (&m_viewProjectionMatrix);

    // NOTE: the VAO/VBO/EBO are created lazily by uploadGeometryBuffers() on the first
    // render: VAOs are not shared between GL contexts, so they cannot be created here
    // when this constructor runs on the async switch worker's build context

    setupGeometryCallbacks ();
    setupParticleUniforms ();
}

void CParticle::setupVao () {
    GLint prevVAO = 0;
    GLint prevArrayBuffer = 0;
    glGetIntegerv (GL_VERTEX_ARRAY_BINDING, &prevVAO);
    glGetIntegerv (GL_ARRAY_BUFFER_BINDING, &prevArrayBuffer);

    // A scene fog toggle can change the linked attribute locations. Rebuild only
    // the VAO, retaining the particle geometry already uploaded to these buffers.
    if (m_vao != 0) glDeleteVertexArrays (1, &m_vao);
    glGenVertexArrays (1, &m_vao);
    if (m_vbo == 0) glGenBuffers (1, &m_vbo);
    if (m_ebo == 0) glGenBuffers (1, &m_ebo);

    glBindVertexArray (m_vao);
    glBindBuffer (GL_ARRAY_BUFFER, m_vbo);
    glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, m_ebo);

    const GLuint program = m_pass->getProgramID ();
    m_vaoShaderRevision = m_pass->getShaderRevision ();

    if (m_useRopeRenderer) {
	// Rope vertex layout: 7 attributes, 26 floats/vertex, stride=104 bytes
	// a_PositionVec4(4) + a_TexCoordVec4(4) + a_TexCoordVec4C1(4) + a_TexCoordVec4C2(4)
	// + a_TexCoordVec4C3(4) + a_TexCoordC4(2) + a_Color(4) = 26
	const GLsizei stride = sizeof (float) * ROPE_FLOATS_PER_VERTEX;

	const GLint loc0 = glGetAttribLocation (program, "a_PositionVec4");
	const GLint loc1 = glGetAttribLocation (program, "a_TexCoordVec4");
	const GLint loc2 = glGetAttribLocation (program, "a_TexCoordVec4C1");
	const GLint loc3 = glGetAttribLocation (program, "a_TexCoordVec4C2");
	const GLint loc4 = glGetAttribLocation (program, "a_TexCoordVec4C3");
	const GLint loc5 = glGetAttribLocation (program, "a_TexCoordC4");
	const GLint loc6 = glGetAttribLocation (program, "a_Color");

	if (loc0 >= 0) {
	    glEnableVertexAttribArray (loc0);
	    glVertexAttribPointer (loc0, 4, GL_FLOAT, GL_FALSE, stride, (void*)(sizeof (float) * 0));
	}
	if (loc1 >= 0) {
	    glEnableVertexAttribArray (loc1);
	    glVertexAttribPointer (loc1, 4, GL_FLOAT, GL_FALSE, stride, (void*)(sizeof (float) * 4));
	}
	if (loc2 >= 0) {
	    glEnableVertexAttribArray (loc2);
	    glVertexAttribPointer (loc2, 4, GL_FLOAT, GL_FALSE, stride, (void*)(sizeof (float) * 8));
	}
	if (loc3 >= 0) {
	    glEnableVertexAttribArray (loc3);
	    glVertexAttribPointer (loc3, 4, GL_FLOAT, GL_FALSE, stride, (void*)(sizeof (float) * 12));
	}
	if (loc4 >= 0) {
	    glEnableVertexAttribArray (loc4);
	    glVertexAttribPointer (loc4, 4, GL_FLOAT, GL_FALSE, stride, (void*)(sizeof (float) * 16));
	}
	if (loc5 >= 0) {
	    glEnableVertexAttribArray (loc5);
	    glVertexAttribPointer (loc5, 2, GL_FLOAT, GL_FALSE, stride, (void*)(sizeof (float) * 20));
	}
	if (loc6 >= 0) {
	    glEnableVertexAttribArray (loc6);
	    glVertexAttribPointer (loc6, 4, GL_FLOAT, GL_FALSE, stride, (void*)(sizeof (float) * 22));
	}
    } else {
	// Sprite vertex layout: 5 attributes, 17 floats/vertex, stride=68 bytes
	// a_Position(3) + a_TexCoordVec4(4) + a_Color(4) + a_TexCoordVec4C1(4) + a_TexCoordC2(2) = 17
	const GLsizei stride = sizeof (float) * SPRITE_FLOATS_PER_VERTEX;

	const GLint loc0 = glGetAttribLocation (program, "a_Position");
	const GLint loc1 = glGetAttribLocation (program, "a_TexCoordVec4");
	const GLint loc2 = glGetAttribLocation (program, "a_Color");
	const GLint loc3 = glGetAttribLocation (program, "a_TexCoordVec4C1");
	const GLint loc4 = glGetAttribLocation (program, "a_TexCoordC2");

	if (loc0 >= 0) {
	    glEnableVertexAttribArray (loc0);
	    glVertexAttribPointer (loc0, 3, GL_FLOAT, GL_FALSE, stride, (void*)(sizeof (float) * 0));
	}
	if (loc1 >= 0) {
	    glEnableVertexAttribArray (loc1);
	    glVertexAttribPointer (loc1, 4, GL_FLOAT, GL_FALSE, stride, (void*)(sizeof (float) * 3));
	}
	if (loc2 >= 0) {
	    glEnableVertexAttribArray (loc2);
	    glVertexAttribPointer (loc2, 4, GL_FLOAT, GL_FALSE, stride, (void*)(sizeof (float) * 7));
	}
	if (loc3 >= 0) {
	    glEnableVertexAttribArray (loc3);
	    glVertexAttribPointer (loc3, 4, GL_FLOAT, GL_FALSE, stride, (void*)(sizeof (float) * 11));
	}
	if (loc4 >= 0) {
	    glEnableVertexAttribArray (loc4);
	    glVertexAttribPointer (loc4, 2, GL_FLOAT, GL_FALSE, stride, (void*)(sizeof (float) * 15));
	}
    }

    glBindVertexArray (prevVAO);
    glBindBuffer (GL_ARRAY_BUFFER, prevArrayBuffer);
}

void CParticle::uploadGeometryBuffers (GLsizeiptr vertexBytes, GLsizeiptr indexBytes) {
    // The first particle may be emitted several frames after setup. Create the GL
    // objects before uploading; calling glBufferData while m_vbo/m_ebo are still 0
    // is GL_INVALID_OPERATION in a core profile.
    if (m_vao == 0) {
	setupVao ();
    }

    GLint prevVAO = 0;
    GLint prevArrayBuffer = 0;
    glGetIntegerv (GL_VERTEX_ARRAY_BINDING, &prevVAO);
    glGetIntegerv (GL_ARRAY_BUFFER_BINDING, &prevArrayBuffer);

    // GL_ELEMENT_ARRAY_BUFFER is VAO state. Bind the particle VAO while updating
    // both buffers so the upload does not detach or replace another pass's EBO.
    glBindVertexArray (m_vao);
    glBindBuffer (GL_ARRAY_BUFFER, m_vbo);
    glBufferData (GL_ARRAY_BUFFER, vertexBytes, m_vertices.data (), GL_DYNAMIC_DRAW);
    glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData (GL_ELEMENT_ARRAY_BUFFER, indexBytes, m_indices.data (), GL_DYNAMIC_DRAW);

    glBindVertexArray (prevVAO);
    glBindBuffer (GL_ARRAY_BUFFER, prevArrayBuffer);
}

void CParticle::setupGeometryCallbacks () {
    m_pass->setGeometryCallback (
	// Setup attribs: save current VAO, bind particle VAO
	[this] () {
	    // created lazily: VAOs are not shared between GL contexts (async builds)
	    if (m_vao == 0 || m_vaoShaderRevision != m_pass->getShaderRevision ()) {
		setupVao ();
	    }
	    glGetIntegerv (GL_VERTEX_ARRAY_BINDING, &m_prevVAO);
	    glBindVertexArray (m_vao);
	},
	// Draw geometry: indexed rendering
	[this] () { glDrawElements (GL_TRIANGLES, m_activeIndexCount, GL_UNSIGNED_INT, nullptr); },
	// Cleanup: restore previous VAO
	[this] () { glBindVertexArray (m_prevVAO); }
    );
}

void CParticle::setupParticleUniforms () {
    // Add particle-specific uniforms from common_particles.h that CPass doesn't provide
    // These are pointer-based: CPass reads the current value each frame
    m_pass->addUniform ("g_ModelMatrixInverse", &m_modelMatrixInverse);
    m_pass->addUniform ("g_OrientationUp", &m_orientationUp);
    m_pass->addUniform ("g_OrientationRight", &m_orientationRight);
    m_pass->addUniform ("g_OrientationForward", &m_orientationForward);
    m_pass->addUniform ("g_ViewUp", &m_viewUp);
    m_pass->addUniform ("g_ViewRight", &m_viewRight);
    m_pass->addUniform ("g_EyePosition", &m_eyePosition);
    m_pass->addUniform ("g_RenderVar0", &m_renderVar0);
    m_pass->addUniform ("g_RenderVar1", &m_renderVar1);

    // REFRACT: set g_RefractAmount (shader default 0.05, may not be applied by CPass's parameter system)
    if (m_hasRefract) {
	m_pass->addUniform ("g_RefractAmount", &m_refractAmount);
    }
}

void CParticle::updateMatrices () {
    const float width = getScene ().getCamera ().getWidth ();
    const float height = getScene ().getCamera ().getHeight ();
    const glm::mat4 flipY = glm::scale (glm::mat4 (1.0f), glm::vec3 (1.0f, -1.0f, 1.0f));

    // Scene layer transforms use a top-left origin and Y-down coordinates, while
    // particles are simulated around a centered, Y-up origin. Convert the complete
    // parent chain between those spaces; using only this layer's origin left child
    // particles near (0, 0) instead of around their parent emitter.
    const glm::mat4 sceneToParticle
	= glm::translate (glm::mat4 (1.0f), glm::vec3 (-width * 0.5f, height * 0.5f, 0.0f)) * flipY;
    const bool is3D = getScene ().getScene ().camera.projection.isPerspective;
    // Keep the world-to-render transform separate from the emitter transform.
    // World-space births already include the emitter/attachment transform and
    // must not follow later layer movement or inherit its size a second time.
    if (m_particleParent) {
	m_particleParent->updateMatrices ();
	m_worldModelMatrix = m_particleParent->m_worldModelMatrix;
    } else {
	m_modelMatrix = (is3D ? glm::mat4 (1.0f) : sceneToParticle) * flipY;
	if (!is3D) this->applyParallaxToModelMatrix ();
	m_worldModelMatrix = m_modelMatrix;
    }
    m_modelMatrix = (m_particle.flags & 1) != 0 ? m_worldModelMatrix
	: m_worldModelMatrix * flipY * particleWorldMatrix () * flipY;
    m_modelMatrixInverse = glm::inverse (m_modelMatrix);

    this->updateParticleViewProjection ();
    m_mvpMatrix = m_viewProjectionMatrix * m_modelMatrix;
    m_mvpMatrixInverse = glm::inverse (m_mvpMatrix);

    // Image quads map texture top toward -Y in the reflected 2D scene FBO.
    // Sprite tangents must use the same basis for asymmetric textures.
    m_orientationUp = glm::vec3 (0.0f, is3D ? 1.0f : -1.0f, 0.0f);
    m_orientationRight = glm::vec3 (1.0f, 0.0f, 0.0f);
    m_orientationForward = glm::vec3 (0.0f, 0.0f, 1.0f);
    m_viewUp = glm::vec3 (0.0f, 1.0f, 0.0f);
    m_viewRight = glm::vec3 (1.0f, 0.0f, 0.0f);

    if (is3D) {
	// Sprite corners are expanded in particle-local space. Express the camera's
	// basis there while preserving the layer's in-plane roll and authored size.
	const glm::mat4 cameraWorld = glm::inverse (getScene ().getCamera ().getLookAt ());
	const glm::mat3 viewToLocal = calculateBillboardParticleOrientation (m_modelMatrixInverse, cameraWorld, 0.0f);
	const float roll = (m_particle.flags & 1) != 0 ? 0.0f
	    : m_particle.angles->evaluateVec3 (getScene ().getTime ()).z;
	const glm::mat3 cameraToLocal = calculateBillboardParticleOrientation (
	    m_modelMatrixInverse, cameraWorld, roll
	);
	m_orientationRight = cameraToLocal[0];
	m_orientationUp = cameraToLocal[1];
	m_orientationForward = cameraToLocal[2];
	m_viewRight = viewToLocal[0];
	m_viewUp = viewToLocal[1];
    }

    if (!m_particle.renderers.empty () && m_particle.renderers[0].orientation == "fixed") {
	const auto& renderer = m_particle.renderers[0];
	// Recover the authored layer transform before crossing the simulation's Y
	// reflection. View tangents remain camera-facing for refraction and lighting.
	glm::mat3 authoredModel = glm::mat3 (m_modelMatrix * flipY);
	if (!is3D) {
	    authoredModel = glm::mat3 (flipY) * authoredModel;
	}
	const glm::mat3 orientation = glm::mat3 (flipY)
	    * calculateFixedParticleOrientation (renderer.axis, authoredModel, (renderer.flags & 1) != 0);
	m_orientationRight = orientation[0];
	m_orientationUp = orientation[1];
	m_orientationForward = orientation[2];
    }

    this->updateParticleRenderVars ();
}

void CParticle::applyParallaxToModelMatrix () {
    if (!getScene ().getScene ().camera.parallax.enabled->value->getBool ()
	|| getScene ().getContext ().getApp ().getContext ().settings.mouse.disableparallax) {
	return;
    }

    const glm::vec3 parallaxOffset (this->resolveParallaxOffset (), 0.0f);
    // Parallax translates the complete layer subtree in world space. Pre-multiplying
    // keeps the camera offset from being scaled or rotated by a particle parent.
    m_modelMatrix = glm::translate (glm::mat4 (1.0f), parallaxOffset) * m_modelMatrix;
}

void CParticle::updateParticleViewProjection () {
    if (getScene ().getScene ().camera.projection.isPerspective) {
	const auto& camera = getScene ().getCamera ();
	m_viewProjectionMatrix = camera.getProjection () * camera.getLookAt ();
	m_eyePosition = camera.getEye ();
    } else if ((m_particle.flags & 4) != 0) {
	// Perspective particles use a dedicated perspective projection
	float width = getScene ().getCamera ().getWidth ();
	float height = getScene ().getCamera ().getHeight ();
	float aspect = width / height;
	float fov = glm::radians (getScene ().getCamera ().getFov ());
	float nearz = getScene ().getCamera ().getNearZ ();
	float farz = getScene ().getCamera ().getFarZ ();

	glm::mat4 perspectiveProj = glm::perspective (fov, aspect, nearz, farz);
	const glm::vec2 framing = getScene ().getCamera ().getViewportScale ();
	perspectiveProj[0][0] *= framing.x;
	perspectiveProj[1][1] *= framing.y;
	glm::mat4 perspectiveView
	    = glm::lookAt (glm::vec3 (0.0f, 0.0f, 1000.0f), glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (0.0f, 1.0f, 0.0f));

	m_viewProjectionMatrix = perspectiveProj * perspectiveView;
	m_eyePosition = glm::vec3 (0.0f, 0.0f, 1000.0f);
    } else {
	// Orthographic projection from scene camera
	m_viewProjectionMatrix = getScene ().getCamera ().getProjection () * getScene ().getCamera ().getLookAt ();
	// For 2D/orthographic scenes the camera eye is at (0,0,0). The shader's
	// ComputeParticleTrailTangents uses cross(eyeDirection, velocity) to
	// compute the trail ribbon width. With eye at z=0 and particles at z=0,
	// eyeDirection is purely in XY — the cross product yields a Z-only vector
	// that is invisible under orthographic projection. Place the eye at z=1000
	// so the cross product produces a visible XY perpendicular direction.
	m_eyePosition = glm::vec3 (0.0f, 0.0f, 1000.0f);
    }
}

void CParticle::updateParticleRenderVars () {
    // Rope-trail geometry already contains the current, partially completed segment.
    // A time offset of one maps every independent history from head UV 0 to tail UV 1.
    // Sprite trails use the authored length limits directly in genericparticle.
    m_renderVar0 = m_useRopeTrailRenderer ? glm::vec4 (0.0f, 0.0f, 1.0f, 0.0f)
					  : glm::vec4 (m_trailLength, m_trailMaxLength, m_trailMinLength, 0.0f);

    if (m_spritesheetFrames > 0 && m_spritesheetCols > 0 && m_spritesheetRows > 0) {
	float frameWidth = 1.0f / static_cast<float> (m_spritesheetCols);
	float frameHeight = 1.0f / static_cast<float> (m_spritesheetRows);
	float textureRatio = 1.0f;
	if (const auto texture = getTexture ()) {
	    // Use atlas dimensions (from resolution vec4) for textureRatio, NOT getRealWidth/Height
	    // which returns per-frame dimensions for animated textures. The shader needs the
	    // per-frame pixel aspect ratio: (atlasH * frameHeight) / (atlasW * frameWidth).
	    const glm::vec4* res = texture->getResolution ();
	    float w = res->x; // atlas/GL texture width
	    float h = res->y; // atlas/GL texture height
	    if (w > 0.0f) {
		textureRatio = (h * frameHeight) / (w * frameWidth);
	    }
	}
	m_renderVar1 = glm::vec4 (frameWidth, frameHeight, static_cast<float> (m_spritesheetFrames), textureRatio);
    } else {
	// No spritesheet - texture ratio is height/width
	float textureRatio = 1.0f;
	if (const auto texture = getTexture ()) {
	    float w = static_cast<float> (texture->getRealWidth ());
	    float h = static_cast<float> (texture->getRealHeight ());
	    if (w > 0.0f) {
		textureRatio = h / w;
	    }
	}
	m_renderVar1 = glm::vec4 (0.0f, 0.0f, 0.0f, textureRatio);
    }
}

void CParticle::renderSprites () {
    if (m_particleCount == 0 || m_pass == nullptr) {
	return;
    }

    // Count alive particles
    uint32_t aliveCount = 0;
    for (uint32_t i = 0; i < m_particleCount; i++) {
	if (m_particles[i].alive) {
	    aliveCount++;
	}
    }

    if (aliveCount == 0) {
	return;
    }

    // Build vertex data in WP shader layout:
    // a_Position(3) + a_TexCoordVec4(uv.x, uv.y, rotZ, size)(4) + a_Color(4)
    //   + a_TexCoordVec4C1(vel.x, vel.y, vel.z, lifetime)(4) + a_TexCoordC2(rotX, rotY)(2) = 17 floats
    uint32_t vertexIndex = 0;
    uint32_t indexOffset = 0;
    const bool is3D = getScene ().getScene ().camera.projection.isPerspective;
    const bool perspectiveBillboard = is3D
	&& (m_particle.renderers.empty () || m_particle.renderers[0].orientation != "fixed");
    const float brightness = getScene ().isHdr () ? getInstanceOverride ().brightness->value->getFloat () : 1.0f;

    for (uint32_t i = 0; i < m_particleCount; i++) {
	const auto& p = m_particles[i];
	if (!p.alive) {
	    continue;
	}

	const glm::vec3 renderRotation = convertParticleRotationForRender (p.rotation, perspectiveBillboard || !is3D);

	// Skip particles with invalid values
	if (!std::isfinite (p.position.x) || !std::isfinite (p.position.y) || !std::isfinite (p.position.z)
	    || !std::isfinite (p.size) || p.size <= 0.0f || p.size > 10000.0f) {
	    continue;
	}

	// Compute the lifetime value for the WP shader's ComputeSpriteFrame.
	// The shader computes: floor(frac(lifetime) * numFrames) to get current frame,
	// and frac(lifetime * numFrames) for the blend factor between frames.
	// We encode the CPU-computed p.frame (which accounts for sequenceMultiplier
	// and animation mode) into the lifetime value the shader expects.
	float lifetime = p.getLifetimePos ();

	if (m_spritesheetFrames > 0 && p.frame >= 0.0f) {
	    if (m_particle.animationMode == "randomframe") {
		// Center within the frame to avoid floating-point edge cases
		lifetime = (p.frame + 0.5f) / static_cast<float> (m_spritesheetFrames);
	    } else {
		// Encode frame index + fractional blend: shader reconstructs via
		// floor(lifetime * numFrames) = current frame,
		// frac(lifetime * numFrames) = blend toward next frame
		lifetime = p.frame / static_cast<float> (m_spritesheetFrames);
	    }
	}

	auto addVertex = [&] (float u, float v) {
	    const uint32_t base = vertexIndex * SPRITE_FLOATS_PER_VERTEX;
	    // a_Position (vec3)
	    m_vertices[base + 0] = p.position.x;
	    m_vertices[base + 1] = p.position.y;
	    m_vertices[base + 2] = p.position.z;
	    // a_TexCoordVec4 (vec4: uv.x, uv.y, rotZ, size)
	    m_vertices[base + 3] = u;
	    m_vertices[base + 4] = v;
	    m_vertices[base + 5] = renderRotation.z;
	    m_vertices[base + 6] = p.size;
	    // a_Color (vec4: r, g, b, a)
	    m_vertices[base + 7] = p.color.r * brightness;
	    m_vertices[base + 8] = p.color.g * brightness;
	    m_vertices[base + 9] = p.color.b * brightness;
	    m_vertices[base + 10] = p.alpha;
	    // a_TexCoordVec4C1 (vec4: vel.x, vel.y, vel.z, lifetime)
	    m_vertices[base + 11] = p.velocity.x;
	    m_vertices[base + 12] = p.velocity.y;
	    m_vertices[base + 13] = p.velocity.z;
	    m_vertices[base + 14] = lifetime;
	    // a_TexCoordC2 (vec2: rotX, rotY)
	    m_vertices[base + 15] = renderRotation.x;
	    m_vertices[base + 16] = renderRotation.y;
	    vertexIndex++;
	};

	// 4 vertices for quad corners
	uint32_t baseVertex = vertexIndex;
	addVertex (0.0f, 1.0f); // 0: Bottom-left
	addVertex (1.0f, 1.0f); // 1: Bottom-right
	addVertex (1.0f, 0.0f); // 2: Top-right
	addVertex (0.0f, 0.0f); // 3: Top-left

	// 6 indices forming 2 triangles
	m_indices[indexOffset++] = baseVertex + 0;
	m_indices[indexOffset++] = baseVertex + 1;
	m_indices[indexOffset++] = baseVertex + 2;
	m_indices[indexOffset++] = baseVertex + 2;
	m_indices[indexOffset++] = baseVertex + 3;
	m_indices[indexOffset++] = baseVertex + 0;
    }

    m_activeIndexCount = static_cast<GLsizei> (indexOffset);
    if (m_activeIndexCount == 0) {
	return;
    }

#if !NDEBUG
    std::string str = "Particles ";
    str += this->getParticle ().name + " (" + std::to_string (this->getId ()) + ", " + this->getParticle ().particleFile
	+ ")";
    glPushDebugGroup (GL_DEBUG_SOURCE_APPLICATION, 0, -1, str.c_str ());
#endif

    uploadGeometryBuffers (
	static_cast<GLsizeiptr> (vertexIndex * SPRITE_FLOATS_PER_VERTEX * sizeof (float)),
	static_cast<GLsizeiptr> (indexOffset * sizeof (uint32_t))
    );

    // Update matrices and uniform data
    updateMatrices ();

    // For REFRACT: blit current scene content into the copy FBO before rendering.
    // This gives the shader a snapshot of what's behind the particles for refraction,
    // without a feedback loop (rendering to scene FBO while reading from copy FBO).
    if (m_hasRefract && m_refractFBO) {
	auto sceneFBO = getScene ().getActiveRenderTarget ();
	GLint w = static_cast<GLint> (sceneFBO->getRealWidth ());
	GLint h = static_cast<GLint> (sceneFBO->getRealHeight ());
	m_refractFBO->resize (w, h);
	glBindFramebuffer (GL_READ_FRAMEBUFFER, sceneFBO->getFramebuffer ());
	glBindFramebuffer (GL_DRAW_FRAMEBUFFER, m_refractFBO->getFramebuffer ());
	glBlitFramebuffer (0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }

    // The shader's ComputeParticleTrailTangents produces a right vector with a Z component
    // (from cross(eyeDirection, velocity) where eyeDirection has XY offset from model transform).
    // For 2D/ortho particles at z≈0, the ortho near plane sits at ndc.z=-1 — any Z offset from
    // the right vector pushes vertices past the near plane, causing half the quad to be clipped.
    // GL_DEPTH_CLAMP prevents near/far clipping by clamping depth instead.
    glEnable (GL_DEPTH_CLAMP);

    // The perspective projection or upright 2D sprite tangents reflect Y.
    // Trail tangents come from velocity and retain their 2D winding.
    const bool reflectedWinding = is3D ? getScene ().getCamera ().isYFlipped () : !m_useTrailRenderer;
    if (reflectedWinding) glFrontFace (GL_CW);

    // CPass::render() handles: FBO binding, texture setup, uniforms, blending, draw call, cleanup
    m_pass->render ();

    if (reflectedWinding) glFrontFace (GL_CCW);
    glDisable (GL_DEPTH_CLAMP);

#if !NDEBUG
    glPopDebugGroup ();
#endif
}

void CParticle::renderRopeTrail () {
    if (m_particleCount == 0 || m_pass == nullptr) {
	return;
    }

    const float brightness = getScene ().isHdr () ? getInstanceOverride ().brightness->value->getFloat () : 1.0f;
    const int subdivision = std::max (1, m_ropeSubdivision);
    uint32_t vertexIndex = 0;
    uint32_t indexOffset = 0;
    bool bufferFull = false;
    std::vector<ParticleInstance::TrailPoint> points;
    std::vector<glm::vec3> splinePositions;
    std::vector<float> splineSizes;
    std::vector<glm::vec4> splineColors;
    points.reserve (static_cast<size_t> (m_ropeSegments + 1));

    auto catmullRom = [] (const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3,
			  float t) -> glm::vec3 {
	const float t2 = t * t;
	const float t3 = t2 * t;
	return 0.5f
	    * ((2.0f * p1) + (-p0 + p2) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2
	       + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
    };

    for (uint32_t particleIndex = 0; particleIndex < m_particleCount && !bufferFull; particleIndex++) {
	const auto& particle = m_particles[particleIndex];
	if (!particle.isAlive () || !particle.trailLastFrameValid) {
	    continue;
	}

	points.clear ();

	auto appendPoint = [&] (const ParticleInstance::TrailPoint& point) {
	if (!std::isfinite (point.position.x) || !std::isfinite (point.position.y)
	    || !std::isfinite (point.position.z)) {
		return;
	    }

	    if (!points.empty ()) {
		const glm::vec3 delta = point.position - points.back ().position;
		if (glm::dot (delta, delta) <= 0.000001f) {
		    // Preserve the newest visual state without emitting a zero-length
		    // segment, whose normalized tangent would be undefined in the shader.
		    points.back () = point;
		    return;
		}
	    }
	    points.push_back (point);
	};

	// The stock TRAILRENDERER shader starts UV 0 at the live head, followed
	// by its recent history. Authored shaft textures narrow toward UV 1;
	// oldest-first geometry put their wide end at the tail instead.
	appendPoint (
	    ParticleInstance::TrailPoint {
		.position = particle.position,
	    }
	);
	for (auto point = particle.trailHistory.rbegin (); point != particle.trailHistory.rend (); ++point) {
	    appendPoint (*point);
	}

	if (points.size () < 2) {
	    continue;
	}

	const uint32_t segmentCount = static_cast<uint32_t> (points.size () - 1);
	const uint32_t totalPoints = segmentCount * subdivision + 1;
	splinePositions.resize (totalPoints);
	splineSizes.resize (totalPoints);
	splineColors.resize (totalPoints);

	for (uint32_t segment = 0; segment < segmentCount; segment++) {
	    const auto& point1 = points[segment];
	    const auto& point2 = points[segment + 1];
	    const auto& point0 = (segment > 0) ? points[segment - 1] : point1;
	    const auto& point3 = (segment + 2 < points.size ()) ? points[segment + 2] : point2;

	    for (int step = 0; step < subdivision; step++) {
		const float t = static_cast<float> (step) / static_cast<float> (subdivision);
		const uint32_t index = segment * subdivision + step;
		splinePositions[index]
		    = catmullRom (point0.position, point1.position, point2.position, point3.position, t);
		// Wallpaper Engine's rope history contains positions only. Visual state
		// remains attached to the live particle so alpha/size operators fade the
		// complete trail instead of leaving bright historical samples behind.
		splineSizes[index] = particle.size;
		splineColors[index] = glm::vec4 (particle.color * brightness, particle.alpha);
	    }
	}

	const auto& lastPoint = points.back ();
	splinePositions.back () = lastPoint.position;
	splineSizes.back () = particle.size;
	splineColors.back () = glm::vec4 (particle.color * brightness, particle.alpha);

	if (m_ropeFadeAlpha || m_ropeFadeSize) {
	    const float denominator = static_cast<float> (totalPoints - 1);
	    for (uint32_t pointIndex = 0; pointIndex < totalPoints; pointIndex++) {
		const float tailFade = 1.0f - static_cast<float> (pointIndex) / denominator;
		splineColors[pointIndex].a
		    = calculateRopeTrailVisualValue (particle.alpha, tailFade, m_ropeFadeAlpha);
		splineSizes[pointIndex]
		    = calculateRopeTrailVisualValue (particle.size, tailFade, m_ropeFadeSize);
	    }
	}

	const uint32_t subSegmentCount = totalPoints - 1;
	const float uvScale = (m_ropeUVScale > 0.0f) ? m_ropeUVScale : 1.0f;
	const float trailLength = static_cast<float> (subSegmentCount) / uvScale + 1.0f;

	for (uint32_t segment = 0; segment < subSegmentCount; segment++) {
	    if ((vertexIndex + 4) * ROPE_FLOATS_PER_VERTEX > m_vertices.size ()
		|| indexOffset + 6 > m_indices.size ()) {
		bufferFull = true;
		break;
	    }

	    const glm::vec3& positionStart = splinePositions[segment];
	    const glm::vec3& positionEnd = splinePositions[segment + 1];
	    const glm::vec3& positionPrevious = (segment > 0) ? splinePositions[segment - 1] : positionStart;
	    const glm::vec3& positionAfter = (segment + 2 < totalPoints) ? splinePositions[segment + 2] : positionEnd;
	    const float sizeStart = splineSizes[segment];
	    const float sizeEnd = splineSizes[segment + 1];
	    const glm::vec4& colorStart = splineColors[segment];
	    const glm::vec4& colorEnd = splineColors[segment + 1];

	    auto addVertex = [&] (float uvX, float uvY) {
		const uint32_t base = vertexIndex * ROPE_FLOATS_PER_VERTEX;
		m_vertices[base + 0] = positionStart.x;
		m_vertices[base + 1] = positionStart.y;
		m_vertices[base + 2] = positionStart.z;
		m_vertices[base + 3] = sizeStart;
		m_vertices[base + 4] = positionEnd.x;
		m_vertices[base + 5] = positionEnd.y;
		m_vertices[base + 6] = positionEnd.z;
		m_vertices[base + 7] = trailLength;
		m_vertices[base + 8] = positionPrevious.x;
		m_vertices[base + 9] = positionPrevious.y;
		m_vertices[base + 10] = positionPrevious.z;
		m_vertices[base + 11] = static_cast<float> (segment);
		m_vertices[base + 12] = positionAfter.x;
		m_vertices[base + 13] = positionAfter.y;
		m_vertices[base + 14] = positionAfter.z;
		m_vertices[base + 15] = sizeEnd;
		m_vertices[base + 16] = colorEnd.r;
		m_vertices[base + 17] = colorEnd.g;
		m_vertices[base + 18] = colorEnd.b;
		m_vertices[base + 19] = colorEnd.a;
		m_vertices[base + 20] = uvX;
		m_vertices[base + 21] = uvY;
		m_vertices[base + 22] = colorStart.r;
		m_vertices[base + 23] = colorStart.g;
		m_vertices[base + 24] = colorStart.b;
		m_vertices[base + 25] = colorStart.a;
		vertexIndex++;
	    };

	    const uint32_t baseVertex = vertexIndex;
	    addVertex (0.0f, 0.0f);
	    addVertex (1.0f, 0.0f);
	    addVertex (1.0f, 1.0f);
	    addVertex (0.0f, 1.0f);

	    m_indices[indexOffset++] = baseVertex + 0;
	    m_indices[indexOffset++] = baseVertex + 1;
	    m_indices[indexOffset++] = baseVertex + 2;
	    m_indices[indexOffset++] = baseVertex + 2;
	    m_indices[indexOffset++] = baseVertex + 3;
	    m_indices[indexOffset++] = baseVertex + 0;
	}
    }

    m_activeIndexCount = static_cast<GLsizei> (indexOffset);
    if (m_activeIndexCount == 0) {
	return;
    }

#if !NDEBUG
    std::string debugName = "Rope trail particles ";
    debugName += this->getParticle ().name + " (" + std::to_string (this->getId ()) + ", "
	+ this->getParticle ().particleFile + ")";
    glPushDebugGroup (GL_DEBUG_SOURCE_APPLICATION, 0, -1, debugName.c_str ());
#endif

    uploadGeometryBuffers (
	static_cast<GLsizeiptr> (vertexIndex * ROPE_FLOATS_PER_VERTEX * sizeof (float)),
	static_cast<GLsizeiptr> (indexOffset * sizeof (uint32_t))
    );
    updateMatrices ();

    if (m_hasRefract && m_refractFBO) {
	auto sceneFBO = getScene ().getActiveRenderTarget ();
	const GLint width = static_cast<GLint> (sceneFBO->getRealWidth ());
	const GLint height = static_cast<GLint> (sceneFBO->getRealHeight ());
	m_refractFBO->resize (width, height);
	glBindFramebuffer (GL_READ_FRAMEBUFFER, sceneFBO->getFramebuffer ());
	glBindFramebuffer (GL_DRAW_FRAMEBUFFER, m_refractFBO->getFramebuffer ());
	glBlitFramebuffer (0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }

    glEnable (GL_DEPTH_CLAMP);
    m_pass->render ();
    glDisable (GL_DEPTH_CLAMP);

#if !NDEBUG
    glPopDebugGroup ();
#endif
}

void CParticle::renderRope () {
    if (m_useRopeTrailRenderer) {
	renderRopeTrail ();
	return;
    }

    if (m_particleCount < 2 || m_pass == nullptr) {
	return;
    }

    // Array is already in spawn order (oldest at index 0) thanks to order-preserving
    // compaction in update(). All particles in [0, m_particleCount) are alive.
    const uint32_t aliveCount = m_particleCount;
    const float brightness = getScene ().isHdr () ? getInstanceOverride ().brightness->value->getFloat () : 1.0f;

    // Build vertex data with Catmull-Rom spline subdivision.
    // Each segment between consecutive particles is subdivided into m_ropeSubdivision
    // sub-segments for smooth curves instead of harsh corners at particle positions.
    //
    // Rope vertex layout (26 floats per vertex, THICKFORMAT):
    // [0-3]   a_PositionVec4:   startPos.xyz, sizeStart
    // [4-7]   a_TexCoordVec4:   endPos.xyz, trailLength
    // [8-11]  a_TexCoordVec4C1: CP0.xyz, trailPosition
    // [12-15] a_TexCoordVec4C2: CP1.xyz, sizeEnd
    // [16-19] a_TexCoordVec4C3: colorEnd.rgba
    // [20-21] a_TexCoordC4:     uvs.xy
    // [22-25] a_Color:          colorStart.rgba

    const uint32_t numSegments = aliveCount - 1;
    const int subdivision = std::max (1, m_ropeSubdivision);

    // Catmull-Rom spline evaluation
    auto catmullRom = [] (const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3,
			  float t) -> glm::vec3 {
	float t2 = t * t, t3 = t2 * t;
	return 0.5f
	    * ((2.0f * p1) + (-p0 + p2) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2
	       + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
    };

    // First pass: evaluate spline to get all interpolated points
    const uint32_t totalPoints = numSegments * subdivision + 1;
    // Store position, size, color (rgba) per point = 3 + 1 + 4 = 8 floats
    std::vector<glm::vec3> splinePositions (totalPoints);
    std::vector<float> splineSizes (totalPoints);
    std::vector<glm::vec4> splineColors (totalPoints); // rgba

    for (uint32_t i = 0; i < numSegments; i++) {
	const auto& p1 = m_particles[i];
	const auto& p2 = m_particles[i + 1];
	const auto& p0 = (i > 0) ? m_particles[i - 1] : p1;
	const auto& p3 = (i + 2 < aliveCount) ? m_particles[i + 2] : p2;

	for (int k = 0; k < subdivision; k++) {
	    float t = static_cast<float> (k) / static_cast<float> (subdivision);
	    uint32_t idx = i * subdivision + k;

	    splinePositions[idx] = catmullRom (p0.position, p1.position, p2.position, p3.position, t);
	    splineSizes[idx] = glm::mix (p1.size, p2.size, t);
	    splineColors[idx] = glm::mix (glm::vec4 (p1.color * brightness, p1.alpha),
					glm::vec4 (p2.color * brightness, p2.alpha), t);
	}
    }
    // Last point is the final particle
    {
	const auto& pLast = m_particles[aliveCount - 1];
	splinePositions[totalPoints - 1] = pLast.position;
	splineSizes[totalPoints - 1] = pLast.size;
	splineColors[totalPoints - 1] = glm::vec4 (pLast.color * brightness, pLast.alpha);
    }

    // Second pass: build quads from consecutive spline points.
    // The shader computes UV.v from trailPosition / (trailLength - 1), consuming
    // 1/(trailLength-1) of UV space per quad. Express trailLength and trailPosition
    // in sub-segment units so each sub-segment quad gets the correct UV slice.
    // UV scale divides the effective length, making UVs exceed [0,1] → texture repeats.
    uint32_t vertexIndex = 0;
    uint32_t indexOffset = 0;
    const uint32_t totalSubSegments = totalPoints - 1;
    const float uvScale = (m_ropeUVScale > 0.0f) ? m_ropeUVScale : 1.0f;
    const float trailLength = static_cast<float> (totalSubSegments) / uvScale + 1.0f;
    const float usableLength = trailLength - 1.0f;

    // UV smoothing: distribute UV proportional to arc length instead of uniform index.
    // Per wiki: only when all particle lifetimes match and scrolling is disabled.
    const bool useSmoothing = m_ropeUVSmoothing && m_uniformLifetimes && !m_ropeUVScrolling;
    std::vector<float> cumulativeArcLength;
    float totalArcLength = 0.0f;

    if (useSmoothing) {
	cumulativeArcLength.resize (totalPoints, 0.0f);
	for (uint32_t i = 1; i < totalPoints; i++) {
	    totalArcLength += glm::distance (splinePositions[i], splinePositions[i - 1]);
	    cumulativeArcLength[i] = totalArcLength;
	}
    }

    // UV scrolling: shift UV along the rope over time (1 UV cycle per second)
    float scrollOffset = 0.0f;
    if (m_ropeUVScrolling && usableLength > 0.0f) {
	scrollOffset = std::fmod (static_cast<float> (g_Time), 10000.0f) * usableLength;
    }

    for (uint32_t s = 0; s < totalSubSegments; s++) {
	const glm::vec3& posStart = splinePositions[s];
	const glm::vec3& posEnd = splinePositions[s + 1];
	float sizeStart = splineSizes[s];
	float sizeEnd = splineSizes[s + 1];
	const glm::vec4& colorStart = splineColors[s];
	const glm::vec4& colorEnd = splineColors[s + 1];

	// Neighboring points for shader tangent computation (CP0/CP1)
	const glm::vec3& posPrev = (s > 0) ? splinePositions[s - 1] : posStart;
	const glm::vec3& posAfter = (s + 2 < totalPoints) ? splinePositions[s + 2] : posEnd;

	// Compute trailPosition for UV mapping
	float trailPosition;
	if (useSmoothing && totalArcLength > 0.0f) {
	    // Arc-length parameterization: map cumulative distance to sub-segment space
	    trailPosition = cumulativeArcLength[s] / totalArcLength * static_cast<float> (totalSubSegments);
	} else {
	    trailPosition = static_cast<float> (s);
	}
	trailPosition += scrollOffset;

	auto addRopeVertex = [&] (float uvX, float uvY) {
	    const uint32_t base = vertexIndex * ROPE_FLOATS_PER_VERTEX;

	    // a_PositionVec4: startPos.xyz, sizeStart
	    m_vertices[base + 0] = posStart.x;
	    m_vertices[base + 1] = posStart.y;
	    m_vertices[base + 2] = posStart.z;
	    m_vertices[base + 3] = sizeStart;

	    // a_TexCoordVec4: endPos.xyz, trailLength
	    m_vertices[base + 4] = posEnd.x;
	    m_vertices[base + 5] = posEnd.y;
	    m_vertices[base + 6] = posEnd.z;
	    m_vertices[base + 7] = trailLength;

	    // a_TexCoordVec4C1: CP0.xyz (neighbor before start), trailPosition
	    m_vertices[base + 8] = posPrev.x;
	    m_vertices[base + 9] = posPrev.y;
	    m_vertices[base + 10] = posPrev.z;
	    m_vertices[base + 11] = trailPosition;

	    // a_TexCoordVec4C2: CP1.xyz (neighbor after end), sizeEnd
	    m_vertices[base + 12] = posAfter.x;
	    m_vertices[base + 13] = posAfter.y;
	    m_vertices[base + 14] = posAfter.z;
	    m_vertices[base + 15] = sizeEnd;

	    // a_TexCoordVec4C3: colorEnd.rgba
	    m_vertices[base + 16] = colorEnd.r;
	    m_vertices[base + 17] = colorEnd.g;
	    m_vertices[base + 18] = colorEnd.b;
	    m_vertices[base + 19] = colorEnd.a;

	    // a_TexCoordC4: uvs.xy
	    m_vertices[base + 20] = uvX;
	    m_vertices[base + 21] = uvY;

	    // a_Color: colorStart.rgba
	    m_vertices[base + 22] = colorStart.r;
	    m_vertices[base + 23] = colorStart.g;
	    m_vertices[base + 24] = colorStart.b;
	    m_vertices[base + 25] = colorStart.a;

	    vertexIndex++;
	};

	// Quad: 4 vertices (left/right at start/end of segment)
	uint32_t baseVertex = vertexIndex;
	addRopeVertex (0.0f, 0.0f); // left at start
	addRopeVertex (1.0f, 0.0f); // right at start
	addRopeVertex (1.0f, 1.0f); // right at end
	addRopeVertex (0.0f, 1.0f); // left at end

	// 2 triangles
	m_indices[indexOffset++] = baseVertex + 0;
	m_indices[indexOffset++] = baseVertex + 1;
	m_indices[indexOffset++] = baseVertex + 2;
	m_indices[indexOffset++] = baseVertex + 2;
	m_indices[indexOffset++] = baseVertex + 3;
	m_indices[indexOffset++] = baseVertex + 0;
    }

    m_activeIndexCount = static_cast<GLsizei> (indexOffset);
    if (m_activeIndexCount == 0) {
	return;
    }

#if !NDEBUG
    std::string str = "Rope particles ";
    str += this->getParticle ().name + " (" + std::to_string (this->getId ()) + ", " + this->getParticle ().particleFile
	+ ")";
    glPushDebugGroup (GL_DEBUG_SOURCE_APPLICATION, 0, -1, str.c_str ());
#endif

    uploadGeometryBuffers (
	static_cast<GLsizeiptr> (vertexIndex * ROPE_FLOATS_PER_VERTEX * sizeof (float)),
	static_cast<GLsizeiptr> (indexOffset * sizeof (uint32_t))
    );

    // Update matrices and uniform data
    updateMatrices ();

    // For REFRACT: blit current scene content into the copy FBO before rendering
    if (m_hasRefract && m_refractFBO) {
	auto sceneFBO = getScene ().getActiveRenderTarget ();
	GLint w = static_cast<GLint> (sceneFBO->getRealWidth ());
	GLint h = static_cast<GLint> (sceneFBO->getRealHeight ());
	m_refractFBO->resize (w, h);
	glBindFramebuffer (GL_READ_FRAMEBUFFER, sceneFBO->getFramebuffer ());
	glBindFramebuffer (GL_DRAW_FRAMEBUFFER, m_refractFBO->getFramebuffer ());
	glBlitFramebuffer (0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }

    glEnable (GL_DEPTH_CLAMP);
    m_pass->render ();
    glDisable (GL_DEPTH_CLAMP);

#if !NDEBUG
    glPopDebugGroup ();
#endif
}
