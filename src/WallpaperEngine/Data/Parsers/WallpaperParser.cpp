#include "WallpaperParser.h"

#include <glm/gtc/matrix_transform.hpp>

#include "ObjectParser.h"
#include "WallpaperEngine/Data/Model/Project.h"
#include "WallpaperEngine/Data/Model/Wallpaper.h"
#include "WallpaperEngine/FileSystem/Container.h"
#include "WallpaperEngine/Logging/Log.h"

using namespace WallpaperEngine::Data::Parsers;

WallpaperUniquePtr WallpaperParser::parse (const JSON& file, Project& project) {
    switch (project.type) {
	case Project::Type_Scene:
	    return parseScene (file, project);
	case Project::Type_Video:
	    return parseVideo (file, project);
	case Project::Type_Web:
	    return parseWeb (file, project);
	default:
	    sLog.exception ("Unexpected project type value found... This is likely a bug");
    }
}

SceneUniquePtr WallpaperParser::parseScene (const JSON& file, Project& project) {
    const auto scene = JSON::parse (project.assetLocator->readString (file));
    const auto camera = scene.require ("camera", "Scenes must have a camera section");
    const auto general = scene.require ("general", "Scenes must have a general section");
    // 2D scenes carry an orthogonal projection block; 3D scenes set it to null (or omit it)
    // and keep fov/nearz/farz/zoom at the general level instead
    const auto projectionIt = general.find ("orthogonalprojection");
    const bool isPerspective
	= projectionIt == general.end () || projectionIt->is_null () || !projectionIt->is_object ();
    const auto projection = isPerspective ? JSON::object () : *projectionIt;
    const auto objects = scene.require ("objects", "Scenes must have an objects section");
    const auto& properties = project.properties;
    // 3D scenes author the projection values in "general", 2D scenes in "camera"
    const auto& projectionSource = isPerspective ? general : camera;

    // TODO: FIND IF THESE DEFAULTS ARE SENSIBLE OR NOT AND PERFORM PROPER VALIDATION WHEN CAMERA PREVIEW AND CAMERA
    // PARALLAX ARE PRESENT

    // 3D scenes ship their runtime camera as a dedicated object in the objects list
    // ("camera": "default"): origin is the eye, and angles orient a camera that looks down
    // -Z with +Y up by default. The top-level "camera" block only stores the editor's last
    // viewport, so it is just the fallback for scenes without a camera object (verified
    // against workshop 3589454154, whose 4K UI plane and preview render only line up with
    // the camera object's pose). Camera path animations ("path") are not implemented.
    glm::vec3 eye = camera.require<glm::vec3> ("eye", "Camera must have an eye position");
    glm::vec3 center = camera.require<glm::vec3> ("center", "Camera must have a center position");
    glm::vec3 up = camera.require<glm::vec3> ("up", "Camera must have an up position");
    auto cameraObject = std::optional<JSON> {};

    if (isPerspective) {
	for (const auto& cur : objects) {
	    if (!cur.is_object () || cur.find ("camera") == cur.end ()) {
		continue;
	    }

	    const auto origin = cur.optional ("origin", glm::vec3 (0.0f));
	    const auto angles = cur.optional ("angles", glm::vec3 (0.0f));

	    // same rotation order and radian units as object transforms (see CObject)
	    glm::mat4 rotation = glm::mat4 (1.0f);
	    rotation = glm::rotate (rotation, angles.z, glm::vec3 (0.0f, 0.0f, 1.0f));
	    rotation = glm::rotate (rotation, angles.y, glm::vec3 (0.0f, 1.0f, 0.0f));
	    rotation = glm::rotate (rotation, angles.x, glm::vec3 (1.0f, 0.0f, 0.0f));

	    eye = origin;
	    center = origin + glm::vec3 (rotation * glm::vec4 (0.0f, 0.0f, -1.0f, 0.0f));
	    up = glm::vec3 (rotation * glm::vec4 (0.0f, 1.0f, 0.0f, 0.0f));
	    cameraObject = cur;

	    if (cur.find ("path") != cur.end ()) {
		sLog.error ("Camera path animations are not supported yet, using the camera's resting pose");
	    }

	    break;
	}
    }

    // the camera object's own fov/zoom win over the general section when authored
    const JSON& fovSource
	= cameraObject.has_value () && cameraObject->find ("fov") != cameraObject->end () ? *cameraObject : projectionSource;
    const JSON& zoomSource
	= cameraObject.has_value () && cameraObject->find ("zoom") != cameraObject->end () ? *cameraObject : projectionSource;

    return std::make_unique <Scene> (
        WallpaperData {
            .filename = "",
            .project = project
        }, SceneData {
            .colors = {
                .ambient  = general.user ("ambientcolor", properties, glm::vec3 (0.0f)),
                .skylight = general.user ("skylightcolor", properties, glm::vec3 (0.0f)),
                .clear = general.user ("clearcolor", properties, glm::vec3 (1.0f)),
            },
            .camera = {
                .fade = general.user ("camerafade", properties, false),
                .preview = general.optional ("camerapreview", false),
                .bloom = {
                    .enabled = general.user ("bloom", properties, false),
                    .strength = general.user ("bloomstrength", properties, 0.0f),
                    .threshold = general.user ("bloomthreshold", properties, 0.0f),
                },
                .parallax = {
                    .enabled = general.user ("cameraparallax", properties, false),
                    .amount = general.user ("cameraparallaxamount", properties, 0.5f),
                    .delay = general.user ("cameraparallaxdelay", properties, 0.1f),
                    .mouseInfluence = general.user ("cameraparallaxmouseinfluence", properties, 0.5f),
                },
                .shake = {
                    .enabled = general.user ("camerashake", properties, false),
                    .amplitude = general.user ("camerashakeamplitude", properties, 0.0f),
                    .roughness = general.user ("camerashakeroughness", properties, 0.0f),
                    .speed = general.user ("camerashakespeed", properties, 0.0f),
                },
                .configuration = {
                    .center = center,
                    .eye = eye,
                    .up = up,
                },
                .projection = {
                    .width  = isPerspective || projection.optional ("auto", false) ? 0 : projection.require <int> ("width",  "Projection must have a width"),
                    .height = isPerspective || projection.optional ("auto", false) ? 0 : projection.require <int> ("height", "Projection must have a height"),
                    .isAuto = !isPerspective && projection.optional ("auto", false),
                    .isPerspective = isPerspective,
                    .nearz = projectionSource.user ("nearz", properties, 0.0f),
                    .farz = projectionSource.user ("farz", properties, 1000.0f),
                    .fov = fovSource.user ("fov", properties, 50.0f),
                    .zoom = zoomSource.user ("zoom", properties, 1.0f)
                }
            },
            .objects = parseObjects (objects, project),
        }
    );
}

VideoUniquePtr WallpaperParser::parseVideo (const JSON& file, Project& project) {
    return std::make_unique<Video> (WallpaperData { .filename = file, .project = project });
}

WebUniquePtr WallpaperParser::parseWeb (const JSON& file, Project& project) {
    return std::make_unique<Web> (WallpaperData {
	.filename = file,
	.project = project,
    });
}

ObjectList WallpaperParser::parseObjects (const JSON& objects, const Project& project) {
    ObjectList result = {};

    for (const auto& cur : objects) {
	result.emplace_back (ObjectParser::parse (cur, project));
    }

    return result;
}
