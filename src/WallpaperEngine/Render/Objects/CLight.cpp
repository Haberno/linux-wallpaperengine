#include "CLight.h"

#include <glm/geometric.hpp>

using namespace WallpaperEngine;
using namespace WallpaperEngine::Render::Objects;

CLight::CLight (Wallpapers::CScene& scene, const Light& light) :
    CObject (scene, light), ScriptableObject (scene, light), m_light (light) {
    this->registerProperty ("color", *light.color->value);
    this->registerProperty ("intensity", *light.intensity->value);
    this->registerProperty ("radius", *light.radius->value);
}

const Light& CLight::getLight () const { return this->m_light; }

glm::vec3 CLight::getWorldPosition () const {
    return glm::vec3 (this->resolveWorldMatrix ()[3]);
}

glm::vec3 CLight::getWorldDirection () const {
    // base forward is +X: matches the sun tracking script in workshop scene 3589454154,
    // which steers the light with a yaw of (-atan2(z, x) - 180°) towards the origin
    return glm::normalize (glm::mat3 (this->resolveWorldMatrix ()) * glm::vec3 (1.0f, 0.0f, 0.0f));
}

glm::vec3 CLight::getPremultipliedColor () const {
    if (!this->m_light.groupVisible->value->getBool ()) {
	return glm::vec3 (0.0f);
    }

    return this->m_light.color->value->getVec3 () * this->m_light.intensity->value->getFloat ();
}
