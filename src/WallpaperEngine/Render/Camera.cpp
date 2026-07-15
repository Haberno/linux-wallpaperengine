#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Camera.h"

using namespace WallpaperEngine;
using namespace WallpaperEngine::Render;

Camera::Camera (Wallpapers::CScene& scene, const SceneData::Camera& camera) :
    m_width (0), m_height (0), m_camera (camera), m_scene (scene) {
    // get the lookat position
    // TODO: ENSURE THIS IS ONLY USED WHEN NOT DOING AN ORTOGRAPHIC CAMERA AS IT THROWS OFF POINTS
    this->m_lookat = glm::lookAt (this->getEye (), this->getCenter (), this->getUp ());
}

Camera::~Camera () = default;

const glm::vec3& Camera::getCenter () const { return this->m_camera.configuration.center; }

const glm::vec3& Camera::getEye () const { return this->m_camera.configuration.eye; }

const glm::vec3& Camera::getUp () const { return this->m_camera.configuration.up; }

const glm::mat4& Camera::getProjection () const { return this->m_projection; }

const glm::mat4& Camera::getLookAt () const { return this->m_lookat; }

bool Camera::isOrthogonal () const { return this->m_isOrthogonal; }

Wallpapers::CScene& Camera::getScene () const { return this->m_scene; }

float Camera::getWidth () const { return this->m_width; }

float Camera::getHeight () const { return this->m_height; }

float Camera::getFov () const { return this->m_camera.projection.fov->value->getFloat (); }

float Camera::getNearZ () const { return this->m_camera.projection.nearz->value->getFloat (); }

float Camera::getFarZ () const { return this->m_camera.projection.farz->value->getFloat (); }

void Camera::setOrthogonalProjection (const float width, const float height) {
    this->m_width = width;
    this->m_height = height;

    float nearz = this->m_camera.projection.nearz->value->getFloat ();
    float farz = this->m_camera.projection.farz->value->getFloat ();

    // Scene wallpapers use signed Z coordinates for otherwise-2D layers, and X/Y
    // rotations can make a single quad cross Z=0.  A conventional positive near
    // plane clips the half that rotates towards the camera.  Wallpaper Engine's
    // orthographic scene volume needs to cover both sides of the authoring plane;
    // nearz remains relevant to perspective cameras only.
    const float depth = glm::max (glm::abs (nearz), glm::abs (farz));
    this->m_projection
	= glm::ortho<float> (-width / 2.0, width / 2.0, -height / 2.0, height / 2.0, -depth, depth);
    this->m_projection = glm::translate (this->m_projection, this->getEye ());
    this->m_isOrthogonal = true;
}

void Camera::setPerspectiveProjection (const float width, const float height, const bool flipY) {
    this->m_width = width;
    this->m_height = height;

    const float nearz = glm::max (this->getNearZ (), 0.0001f);
    const float farz = this->getFarZ ();

    // fov is treated as the vertical field of view; view transform is the lookAt
    // computed in the constructor from the scene's eye/center/up
    this->m_projection = glm::perspective (glm::radians (this->getFov ()), width / height, nearz, farz);

    // outputs that present the scene framebuffer with a vertical flip (see
    // Output::renderVFlip) need the scene rendered mirrored for the flip to undo it;
    // the 2D pipeline bakes the same compensation into its object coordinates
    if (flipY) {
	this->m_projection[1][1] *= -1.0f;
    }

    this->m_isOrthogonal = false;
    this->m_isYFlipped = flipY;
}

bool Camera::isYFlipped () const { return this->m_isYFlipped; }
