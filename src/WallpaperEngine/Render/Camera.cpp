#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Camera.h"

#include <cmath>

using namespace WallpaperEngine;
using namespace WallpaperEngine::Render;

Camera::Camera (Wallpapers::CScene& scene, const SceneData::Camera& camera) :
    m_width (0), m_height (0), m_camera (camera), m_scene (scene) {
    this->m_defaultTransform = CameraTransform {
	.center = camera.configuration.center,
	.eye = camera.configuration.eye,
	.up = camera.configuration.up,
	.fov = camera.projection.fov->value->getFloat (),
	.zoom = camera.projection.zoom->value->getFloat (),
    };
    this->m_transform = this->m_defaultTransform;
    this->m_lookat = glm::lookAt (this->getEye (), this->getCenter (), this->getUp ());
}

Camera::~Camera () = default;

const glm::vec3& Camera::getCenter () const { return this->m_transform.center; }

const glm::vec3& Camera::getEye () const { return this->m_transform.eye; }

const glm::vec3& Camera::getUp () const { return this->m_transform.up; }

const glm::mat4& Camera::getProjection () const { return this->m_projection; }

const glm::mat4& Camera::getLookAt () const { return this->m_lookat; }

bool Camera::isOrthogonal () const { return this->m_isOrthogonal; }

Wallpapers::CScene& Camera::getScene () const { return this->m_scene; }

float Camera::getWidth () const { return this->m_width; }

float Camera::getHeight () const { return this->m_height; }

float Camera::getFov () const { return this->m_transform.fov; }

float Camera::getNearZ () const { return this->m_camera.projection.nearz->value->getFloat (); }

float Camera::getFarZ () const { return this->m_camera.projection.farz->value->getFloat (); }

float Camera::getZoom () const { return this->m_transform.zoom; }

CameraTransform Camera::getDefaultTransform () const { return this->m_defaultTransform; }

void Camera::setDefaultTransform (const CameraTransform& transform, const bool apply) {
    this->m_defaultTransform = transform;
    if (apply) {
	this->setTransform (transform);
    }
}

CameraTransform Camera::objectTransform (const glm::mat4& world, const float fov, const float zoom) {
    const glm::vec3 eye = glm::vec3 (world[3]);
    const glm::mat3 orientation = glm::mat3 (world);
    const auto normalizedOr = [] (const glm::vec3& value, const glm::vec3& fallback) {
	const float length = glm::length (value);
	return length > 0.000001f ? value / length : fallback;
    };
    const glm::vec3 forward = normalizedOr (
	orientation * glm::vec3 (0.0f, 0.0f, -1.0f), glm::vec3 (0.0f, 0.0f, -1.0f)
    );
    const glm::vec3 up
	= normalizedOr (orientation * glm::vec3 (0.0f, 1.0f, 0.0f), glm::vec3 (0.0f, 1.0f, 0.0f));

    return CameraTransform {
	.center = eye + forward,
	.eye = eye,
	.up = up,
	.fov = fov,
	.zoom = zoom,
    };
}

void Camera::setTransform (const CameraTransform& transform) {
    this->m_transform = transform;
    this->updateMatrices ();
}

void Camera::resetTransform () {
    // Keep live user-property changes to the resting projection values.
    this->m_defaultTransform.fov = this->m_camera.projection.fov->value->getFloat ();
    this->m_defaultTransform.zoom = this->m_camera.projection.zoom->value->getFloat ();
    this->m_transform = this->m_defaultTransform;
    this->updateMatrices ();
}

glm::vec3 Camera::screenToWorld (const glm::vec2& normalizedPosition) const {
    return projectScreenPosition (
	normalizedPosition, this->m_isOrthogonal, this->m_width, this->m_height, this->m_projection, this->m_lookat,
	this->m_isYFlipped
    );
}

glm::vec3 Camera::projectScreenPosition (
    const glm::vec2& normalizedPosition, const bool orthogonal, const float width, const float height,
    const glm::mat4& projection, const glm::mat4& view, const bool projectionYFlipped
) {
    if (orthogonal) {
	// Orthographic scene scripts use the editor canvas coordinate system rather
	// than centered OpenGL coordinates (for example, a 256x256 scene has its
	// center at 128,128).
	return glm::vec3 (normalizedPosition.x * width, normalizedPosition.y * height, 0.0f);
    }

    glm::vec2 ndc = normalizedPosition * 2.0f - 1.0f;
    if (projectionYFlipped) {
	// The perspective projection is mirrored before the scene FBO is flipped at
	// presentation. Undo that render-only mirror for physical cursor coordinates.
	ndc.y = -ndc.y;
    }

    const glm::mat4 inverseViewProjection = glm::inverse (projection * view);
    const auto unproject = [&inverseViewProjection, &ndc] (const float clipZ) {
	glm::vec4 world = inverseViewProjection * glm::vec4 (ndc, clipZ, 1.0f);
	if (std::abs (world.w) > 0.000001f) {
	    world /= world.w;
	}
	return glm::vec3 (world);
    };

    const glm::vec3 nearPoint = unproject (-1.0f);
    const glm::vec3 farPoint = unproject (1.0f);
    const glm::vec3 ray = farPoint - nearPoint;
    if (std::abs (ray.z) <= 0.000001f) {
	// A camera looking exactly parallel to Z=0 has no unique intersection.
	// Preserve useful x/y coordinates and keep the API's unsupported z at zero.
	return glm::vec3 (nearPoint.x, nearPoint.y, 0.0f);
    }

    const glm::vec3 world = nearPoint + ray * (-nearPoint.z / ray.z);
    return glm::vec3 (world.x, world.y, 0.0f);
}

void Camera::setOrthogonalProjection (const float width, const float height) {
    this->m_width = width;
    this->m_height = height;
    this->m_isOrthogonal = true;
    this->m_isYFlipped = false;
    this->updateMatrices ();
}

void Camera::setPerspectiveProjection (const float width, const float height, const bool flipY) {
    this->m_width = width;
    this->m_height = height;
    this->m_isOrthogonal = false;
    this->m_isYFlipped = flipY;
    this->updateMatrices ();
}

bool Camera::isYFlipped () const { return this->m_isYFlipped; }

void Camera::updateMatrices () {
    this->m_lookat = glm::lookAt (this->getEye (), this->getCenter (), this->getUp ());

    if (this->m_isOrthogonal) {
	const float nearz = this->m_camera.projection.nearz->value->getFloat ();
	const float farz = this->m_camera.projection.farz->value->getFloat ();
	const float depth = glm::max (glm::abs (nearz), glm::abs (farz));
	const float zoom = glm::max (this->getZoom (), 0.0001f);
	const float halfWidth = this->m_width / (2.0f * zoom);
	const float halfHeight = this->m_height / (2.0f * zoom);
	this->m_projection = glm::ortho<float> (-halfWidth, halfWidth, -halfHeight, halfHeight, -depth, depth);
	this->m_projection = glm::translate (this->m_projection, this->getEye ());
	return;
    }

    const float nearz = glm::max (this->getNearZ (), 0.0001f);
    const float farz = this->getFarZ ();
    this->m_projection = glm::perspective (
	glm::radians (glm::clamp (this->getFov (), 1.0f, 179.0f)), this->m_width / this->m_height, nearz, farz
    );

    // Wallpaper Engine applies zoom to the projection scale. This is equivalent
    // to the standard optical FOV conversion and keeps authored FOV animation
    // independent from the path's zoom channel.
    const float zoom = glm::max (this->getZoom (), 0.0001f);
    this->m_projection[0][0] *= zoom;
    this->m_projection[1][1] *= zoom;
    if (this->m_isYFlipped) {
	this->m_projection[1][1] *= -1.0f;
    }
}
