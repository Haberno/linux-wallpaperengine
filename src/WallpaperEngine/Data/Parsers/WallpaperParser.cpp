#include "WallpaperParser.h"

#include <glm/gtc/matrix_transform.hpp>
#include <map>
#include <set>

#include "ObjectParser.h"
#include "CameraPathParser.h"
#include "MaterialParser.h"
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
    bool hasModelLayer = false;
    for (const auto& object : objects) {
	const auto model = object.find ("model");
	if (model != object.end () && !model->is_null ()) {
	    hasModelLayer = true;
	    break;
	}
    }
    const auto& properties = project.properties;
    // 3D scenes author the projection values in "general", 2D scenes in "camera"
    const auto& projectionSource = isPerspective ? general : camera;
    // Native applies the general table (fov/nearz/farz) to 2D scenes as well; perspective
    // particles in 2D scenes depend on it. Keep the camera block as the legacy fallback.
    const auto projectionValueSource = [&general, &projectionSource] (const char* key) -> const JSON& {
	return general.find (key) != general.end () ? general : projectionSource;
    };
    // Runtime zoom is also authored in general by 2D scenes. Reading only the
    // editor camera silently drops their overscan and exposes clear color when
    // parallax moves a canvas-sized background. Keep the legacy camera fallback.
    const auto& zoomSource = general.find ("zoom") != general.end () ? general : projectionSource;

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
    // Debug aid (WPE_CAMERA_QUEUE=sequence|random): forces every camera path queue
    // so a shot order can be compared against the reference without reshuffling.
    static const char* queueOverride = std::getenv ("WPE_CAMERA_QUEUE");
    const auto queueMode = [] (std::string authored) {
	return queueOverride != nullptr ? std::string (queueOverride) : std::move (authored);
    };
    std::vector<int> cameraObjectIds;
    std::vector<SceneData::Camera::ObjectProjection> cameraObjectProjections;
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
	    cameraPaths.push_back (
		CameraPathSource { .objectId = std::nullopt, .queueMode = queueMode ("sequence"), .paths = paths }
	    );
	}
    }
    // Camera paths exist in two scene formats: older/global scenes list path files
    // in camera.paths, while newer editor output attaches a path directly to one
    // or more camera objects. Both forms enable the renderer's camera fade.
    for (const auto& cur : objects) {
	if (!cur.is_object () || cur.find ("camera") == cur.end ()) {
	    continue;
	}
	if (const auto id = cur.optional<int> ("id"); id.has_value ()) {
	    cameraObjectIds.push_back (*id);
	    cameraObjectProjections.push_back (SceneData::Camera::ObjectProjection {
		.id = *id,
		.fov = cur.find ("fov") != cur.end () ? cur.user ("fov", properties, 50.0f) : nullptr,
		.zoom = cur.find ("zoom") != cur.end () ? cur.user ("zoom", properties, 1.0f) : nullptr,
	    });
	}
	if (const auto path = cur.optional<std::string> ("path"); path.has_value ()) {
	    const auto& paths = loadCameraPathFile (*path);
	    if (!paths.empty ()) {
		cameraPaths.push_back (CameraPathSource {
		    .objectId = cur.optional<int> ("id"),
		    .queueMode = queueMode (cur.optional<std::string> ("queuemode", "sequence")),
		    .paths = paths,
		});
	    }
	}
    }

    auto result = std::make_unique <Scene> (
        WallpaperData {
            .filename = "",
            .project = project
        }, SceneData {
            .version = scene.optional ("version", 0),
            .hdr = general.optional ("hdr", false),
            .hasModelLayer = hasModelLayer,
            .transparentSorting = general.optional ("transparentsorting", false),
            .customSortOrder = general.optional ("customsortorder", false),
            .clearEnabled = general.user ("clearenabled", properties, true),
            .colors = {
                .ambient  = general.user ("ambientcolor", properties, glm::vec3 (0.0f)),
                .skylight = general.user ("skylightcolor", properties, glm::vec3 (0.0f)),
                .clear = general.user ("clearcolor", properties, glm::vec3 (1.0f)),
            },
	    .physics = {
		.gravityDirection = general.user ("gravitydirection", properties, glm::vec3 (0.0f, -1.0f, 0.0f)),
		.gravityStrength = general.user ("gravitystrength", properties, 1.0f),
		.windEnabled = general.user ("windenabled", properties, false),
		.windDirection = general.user ("winddirection", properties, glm::vec3 (.707f, .707f, 0.0f)),
		.windStrength = general.user ("windstrength", properties, 1.0f),
	    },
	    .fog = {
		.distance = {
		    .enabled = general.user ("fogdistance", properties, false),
		    .color = general.user ("fogdistancecolor", properties, glm::vec3 (0.0f)),
		    .start = general.user ("fogdistancestart", properties, 1.0f),
		    .end = general.user ("fogdistanceend", properties, 5.0f),
		    .startDensity = general.user ("fogdistancestartdensity", properties, 0.0f),
		    .endDensity = general.user ("fogdistanceenddensity", properties, 1.0f),
		},
		.height = {
		    .enabled = general.user ("fogheight", properties, false),
		    .color = general.user ("fogheightcolor", properties, glm::vec3 (0.0f)),
		    .start = general.user ("fogheightstart", properties, 1.0f),
		    .end = general.user ("fogheightend", properties, -3.0f),
		    .startDensity = general.user ("fogheightstartdensity", properties, 0.0f),
		    .endDensity = general.user ("fogheightenddensity", properties, 1.0f),
		},
	    },
            .camera = {
                .fade = general.user ("camerafade", properties, false),
		.preview = general.optional ("camerapreview", false),
		.paths = std::move (cameraPaths),
		.objectIds = std::move (cameraObjectIds),
		.objectProjections = std::move (cameraObjectProjections),
                .bloom = {
                    .enabled = general.user ("bloom", properties, false),
                    .strength = general.user ("bloomstrength", properties, 2.0f),
                    .threshold = general.user ("bloomthreshold", properties, 0.65f),
                    .tint = general.user ("bloomtint", properties, glm::vec3 (1.0f)),
                    .hdr = {
                        .strength = general.user ("bloomhdrstrength", properties, 2.0f),
                        .threshold = general.user ("bloomhdrthreshold", properties, 1.0f),
                        .feather = general.user ("bloomhdrfeather", properties, 0.1f),
                        .scatter = general.user ("bloomhdrscatter", properties, 1.619f),
                        .iterations = general.user ("bloomhdriterations", properties, 8),
                    },
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
                    .nearz = projectionValueSource ("nearz").user ("nearz", properties, isPerspective ? 0.1f : 0.0f),
                    .farz = projectionValueSource ("farz").user ("farz", properties, 1000.0f),
		    .fov = projectionValueSource ("fov").user ("fov", properties, 50.0f),
		    .perspectiveOverrideFov = general.user ("perspectiveoverridefov", properties, 95.0f),
		    .zoom = zoomSource.user ("zoom", properties, 1.0f)
                }
            },
            .objects = parseObjects (objects, project),
        }
    );

    if (const auto precache = scene.optional ("precache"); precache && precache->is_array ()) {
	std::set<std::string> loaded;
	for (const auto& entry : *precache) {
	    if (!entry.is_string ()) continue;
	    const auto filename = entry.get<std::string> ();
	    // Native scene loading only dispatches the nine-byte materials prefix.
	    if (!filename.starts_with ("materials") || !loaded.insert (filename).second) continue;
	    try {
		result->precachedMaterials.emplace_back (MaterialParser::load (project, filename));
	    } catch (const std::exception& error) {
		sLog.error ("Cannot precache material ", filename, ": ", error.what ());
	    }
	}
    }

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
    const ObjectParser::ModelCacheScope modelCache;
    ObjectList result = {};

    for (const auto& cur : objects) {
	result.emplace_back (ObjectParser::parse (cur, project));
    }

    return result;
}
