#include "WallpaperParser.h"

#include <glm/gtc/matrix_transform.hpp>
#include <map>

#include "ObjectParser.h"
#include "CameraPathParser.h"
#include "WallpaperEngine/Data/Model/Project.h"
#include "WallpaperEngine/Data/Model/Wallpaper.h"
#include "WallpaperEngine/Data/Utils/JsonTelemetry.h"
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
    const std::string sceneFilename = file;
    const auto scene = WallpaperEngine::Data::JSON::parseCompatible (
	project.assetLocator->readString (sceneFilename), sceneFilename
    );
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
    // ("camera": "default"). The top-level camera block is the editor viewport and remains
    // the fallback for scenes without a camera layer; CScene follows the selected layer's
    // live world transform (including parent rigs and SceneScript) once objects exist.
    glm::vec3 eye = camera.require<glm::vec3> ("eye", "Camera must have an eye position");
    glm::vec3 center = camera.require<glm::vec3> ("center", "Camera must have a center position");
    glm::vec3 up = camera.require<glm::vec3> ("up", "Camera must have an up position");
    std::vector<CameraPathSource> cameraPaths;
    std::vector<int> cameraObjectIds;
    std::map<std::string, std::vector<CameraPath>> parsedPathFiles;
    const auto loadCameraPathFile = [&project, &parsedPathFiles] (const std::string& path) -> const std::vector<CameraPath>& {
	const auto existing = parsedPathFiles.find (path);
	if (existing != parsedPathFiles.end ()) {
	    return existing->second;
	}

	std::vector<CameraPath> paths;
	try {
	    paths = CameraPathParser::parse (
		WallpaperEngine::Data::JSON::parseCompatible (project.assetLocator->readString (path), path)
	    );
	} catch (const std::exception& e) {
	    sLog.error ("Cannot parse camera path file ", path, ": ", e.what ());
	}
	return parsedPathFiles.emplace (path, std::move (paths)).first->second;
    };

    for (const auto& path : camera.optional<std::vector<std::string>> ("paths").value_or (
	     std::vector<std::string> {}
         )) {
	const auto& paths = loadCameraPathFile (path);
	if (!paths.empty ()) {
	    cameraPaths.push_back (CameraPathSource { .objectId = std::nullopt, .queueMode = "sequence", .paths = paths });
	}
    }
    auto cameraObject = std::optional<JSON> {};

    // Camera paths exist in two scene formats: older/global scenes list path files
    // in camera.paths, while newer editor output attaches a path directly to one
    // or more camera objects. Both forms enable the renderer's camera fade.
    for (const auto& cur : objects) {
	if (!cur.is_object () || cur.find ("camera") == cur.end ()) {
	    continue;
	}
	if (const auto id = cur.optional<int> ("id"); id.has_value ()) {
	    cameraObjectIds.push_back (*id);
	}
	if (const auto path = cur.optional<std::string> ("path"); path.has_value ()) {
	    const auto& paths = loadCameraPathFile (*path);
	    if (!paths.empty ()) {
		cameraPaths.push_back (CameraPathSource {
		    .objectId = cur.optional<int> ("id"),
		    .queueMode = cur.optional<std::string> ("queuemode", "sequence"),
		    .paths = paths,
		});
	    }
	}
    }

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

	    break;
	}
    }

    // the camera object's own fov/zoom win over the general section when authored
    const JSON& fovSource
	= cameraObject.has_value () && cameraObject->find ("fov") != cameraObject->end () ? *cameraObject : projectionSource;
    const JSON& zoomSource
	= cameraObject.has_value () && cameraObject->find ("zoom") != cameraObject->end () ? *cameraObject : projectionSource;

    auto result = std::make_unique <Scene> (
        WallpaperData {
            .filename = "",
            .project = project
        }, SceneData {
            .transparentSorting = general.optional ("transparentsorting", false),
            .customSortOrder = general.optional ("customsortorder", false),
            .colors = {
                .ambient  = general.user ("ambientcolor", properties, glm::vec3 (0.0f)),
                .skylight = general.user ("skylightcolor", properties, glm::vec3 (0.0f)),
                .clear = general.user ("clearcolor", properties, glm::vec3 (1.0f)),
            },
	    .fog = {
		.distance = {
		    .enabled = general.user ("fogdistance", properties, false),
		    .color = general.user ("fogdistancecolor", properties, glm::vec3 (0.0f)),
		    .start = general.user ("fogdistancestart", properties, 0.0f),
		    .end = general.user ("fogdistanceend", properties, 1.0f),
		    .startDensity = general.user ("fogdistancestartdensity", properties, 0.0f),
		    .endDensity = general.user ("fogdistanceenddensity", properties, 1.0f),
		},
		.height = {
		    .enabled = general.user ("fogheight", properties, false),
		    .color = general.user ("fogheightcolor", properties, glm::vec3 (0.0f)),
		    .start = general.user ("fogheightstart", properties, 0.0f),
		    .end = general.user ("fogheightend", properties, 1.0f),
		    .startDensity = general.user ("fogheightstartdensity", properties, 0.0f),
		    .endDensity = general.user ("fogheightenddensity", properties, 1.0f),
		},
	    },
            .camera = {
                .fade = general.user ("camerafade", properties, false),
		.preview = general.optional ("camerapreview", false),
		.paths = std::move (cameraPaths),
		.objectIds = std::move (cameraObjectIds),
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

    WallpaperEngine::Data::Utils::JsonTelemetry::scan (scene, file.get<std::string> ());

    return result;
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
