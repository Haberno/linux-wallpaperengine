#include "MaterialParser.h"

#include "TextureParser.h"
#include "WallpaperEngine/Data/Model/Material.h"
#include "WallpaperEngine/Data/Model/Project.h"
#include "WallpaperEngine/Data/Parsers/ShaderConstantParser.h"
#include "WallpaperEngine/Data/Utils/JsonTelemetry.h"
#include "WallpaperEngine/FileSystem/Container.h"

using namespace WallpaperEngine::Data::Parsers;
using namespace WallpaperEngine::Data::Model;

MaterialUniquePtr
MaterialParser::load (const Project& project, const std::string& filename, const bool modelDepthDefaults) {
    const auto materialJson
	= WallpaperEngine::Data::JSON::parseCompatible (project.assetLocator->readString (filename), filename);

    auto result = parse (materialJson, filename, project, modelDepthDefaults);

    WallpaperEngine::Data::Utils::JsonTelemetry::scan (materialJson, filename);

    return result;
}

MaterialUniquePtr MaterialParser::parse (
    const JSON& it, const std::string& filename, const Project& project, const bool modelDepthDefaults
) {
    return std::make_unique<Material> (Material {
	.filename = filename,
	.passes = parsePasses (
	    it.require ("passes", "Material must have passes to render"), project, modelDepthDefaults
	),
    });
}

std::vector<MaterialPassUniquePtr>
MaterialParser::parsePasses (const JSON& it, const Project& project, const bool modelDepthDefaults) {
    std::vector<MaterialPassUniquePtr> result = {};

    if (!it.is_array ()) {
	return result;
    }

    for (const auto& cur : it) {
	result.push_back (parsePass (cur, project, modelDepthDefaults));
    }

    return result;
}

MaterialPassUniquePtr
MaterialParser::parsePass (const JSON& it, const Project& project, const bool modelDepthDefaults) {
    const auto textures = it.optional ("textures");
    const auto usertextures = it.optional ("usertextures");
    const auto combos = it.optional ("combos");
    const auto constants = it.optional ("constantshadervalues");

    // Image/effect passes historically default to an overlay with no depth state.
    // Opaque 3D model materials use the opposite defaults in Wallpaper Engine: many
    // imported models omit both keys and rely on depth testing/writes for occlusion.
    const std::string depthDefault = modelDepthDefaults ? "enabled" : "disabled";

    return std::make_unique<MaterialPass> (MaterialPass {
	// TODO: REMOVE THIS UGLY STD::STRING CREATION
	.blending = parseBlendMode (it.optional ("blending", std::string ("normal"))),
	.cullmode = parseCullMode (it.optional ("cullmode", std::string ("nocull"))),
	.depthtest = parseDepthtestMode (it.optional ("depthtest", depthDefault)),
	.depthwrite = parseDepthwriteMode (it.optional ("depthwrite", depthDefault)),
	.shader = it.require<std::string> ("shader", "Material pass must have a shader"),
	.textures = textures.has_value () ? TextureParser::parseTextureMap (*textures) : TextureMap {},
	.usertextures = usertextures.has_value () ? TextureParser::parseTextureMap (*usertextures) : TextureMap {},
	.combos = combos.has_value () ? parseCombos (*combos) : ComboMap {},
	.constants = constants.has_value () ? ShaderConstantParser::parse (*constants, project) : ShaderConstantMap {},
    });
}

std::map<std::string, int> MaterialParser::parseCombos (const JSON& it) {
    std::map<std::string, int> result = {};

    if (!it.is_object ()) {
	return result;
    }

    for (const auto& cur : it.items ()) {
	result.emplace (cur.key (), cur.value ());
    }

    return result;
}

BlendingMode MaterialParser::parseBlendMode (const std::string& mode) {
    if (mode == "normal") {
	return BlendingMode_Normal;
    }

    if (mode == "additive") {
	return BlendingMode_Additive;
    }

    if (mode == "translucent") {
	return BlendingMode_Translucent;
    }

    if (mode == "alphatocoverage") {
	return BlendingMode_AlphaToCoverage;
    }

    sLog.error ("Unknown blending mode: ", mode, " defaulting to normal");
    return BlendingMode_Normal;
}

CullingMode MaterialParser::parseCullMode (const std::string& mode) {
    if (mode == "nocull") {
	return CullingMode_Disable;
    }

    if (mode == "normal") {
	return CullingMode_Normal;
    }

    sLog.error ("Unknown culling mode: ", mode, " defaulting to nocull");
    return CullingMode_Disable;
}

DepthtestMode MaterialParser::parseDepthtestMode (const std::string& mode) {
    if (mode == "disabled") {
	return DepthtestMode_Disabled;
    }

    if (mode == "enabled") {
	return DepthtestMode_Enabled;
    }

    sLog.error ("Unknown depthtest mode: ", mode, " defaulting to disabled");
    return DepthtestMode_Disabled;
}

DepthwriteMode MaterialParser::parseDepthwriteMode (const std::string& mode) {
    if (mode == "disabled") {
	return DepthwriteMode_Disabled;
    }

    if (mode == "enabled") {
	return DepthwriteMode_Enabled;
    }

    sLog.error ("Unknown depthwrite mode: ", mode, " defaulting to disabled");
    return DepthwriteMode_Disabled;
}
