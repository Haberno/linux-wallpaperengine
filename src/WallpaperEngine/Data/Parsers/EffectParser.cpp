#include "EffectParser.h"
#include "MaterialParser.h"

#include "WallpaperEngine/Data/Model/DynamicValue.h"
#include "WallpaperEngine/Data/Model/Effect.h"
#include "WallpaperEngine/Data/Model/Material.h"
#include "WallpaperEngine/Data/Model/Project.h"
#include "WallpaperEngine/Data/Utils/JsonTelemetry.h"
#include "WallpaperEngine/FileSystem/Container.h"

using namespace WallpaperEngine::Data::Parsers;
using namespace WallpaperEngine::Data::Model;

namespace {
bool conditionsPass (const JSON& it, const JSON& combos) {
    const auto conditions = it.optional ("conditions");
    if (!conditions.has_value () || !conditions->is_array ()) return true;

    // Native conditions AND the array's objects and each object's combo keys.
    // Unsupported expected types are ignored; absent/nonnumeric values are zero.
    for (const auto& condition : *conditions) {
	if (!condition.is_object ()) continue;
	for (const auto& [name, expected] : condition.items ()) {
	    int value = 0;
	    std::string operation;
	    if (expected.is_number ()) {
		value = expected.get<int> ();
	    } else if (expected.is_object ()) {
		const auto comparison = expected.optional ("value");
		if (comparison.has_value () && comparison->is_number ()) value = comparison->get<int> ();
		const auto op = expected.optional ("op");
		if (op.has_value () && op->is_string ()) operation = op->get<std::string> ();
	    } else {
		continue;
	    }
	    const auto combo = combos.find (name);
	    const int actual = combo != combos.end () && combo->is_number () ? combo->get<int> () : 0;
	    const bool matches = operation == "ge" ? actual >= value : operation == "gt" ? actual > value
		: operation == "le" ? actual <= value : operation == "lt" ? actual < value : actual == value;
	    if (!matches) return false;
	}
    }
    return true;
}
}

EffectUniquePtr EffectParser::load (const Project& project, const std::string& filename, const JSON& combos) {
    const auto effectJson
	= WallpaperEngine::Data::JSON::parseCompatible (project.assetLocator->readString (filename), filename);

    auto result = parse (effectJson, project, combos);

    WallpaperEngine::Data::Utils::JsonTelemetry::scan (effectJson, filename);

    return result;
}

EffectUniquePtr EffectParser::parse (const JSON& it, const Project& project, const JSON& combos) {
    const auto dependencies = it.optional ("dependencies");
    const auto fbos = it.optional ("fbos");

    return std::make_unique<Effect> (Effect {
	.name = it.optional<std::string> ("name", ""),
	.description = it.optional<std::string> ("description", ""),
	.group = it.optional<std::string> ("group", ""),
	.preview = it.optional<std::string> ("preview", ""),
	.dependencies = dependencies.has_value () ? parseDependencies (*dependencies) : std::vector<std::string> {},
	.passes = parseEffectPasses (it.require ("passes", "Effect file must have passes"), project, combos),
	.fbos = fbos.has_value () ? parseFBOs (*fbos, combos) : std::vector<FBOUniquePtr> {},
    });
}

std::vector<std::string> EffectParser::parseDependencies (const JSON& it) {
    std::vector<std::string> result = {};

    if (!it.is_array ()) {
	return result;
    }

    for (const auto& cur : it) {
	result.push_back (cur);
    }

    return result;
}

std::vector<EffectPassUniquePtr> EffectParser::parseEffectPasses (const JSON& it, const Project& project, const JSON& combos) {
    std::vector<EffectPassUniquePtr> result = {};

    if (!it.is_array ()) {
	return result;
    }

    for (const auto& cur : it) {
	if (!conditionsPass (cur, combos)) {
	    result.push_back (std::make_unique<EffectPass> (EffectPass { .enabled = false }));
	    continue;
	}
	const auto binds = cur.optional ("bind");
	const auto command = cur.optional ("command");
	const auto material = cur.optional ("material");

	// TODO: CAN TARGET BE SET IF MATERIAL IS SET?

	result.push_back (
	    std::make_unique<EffectPass> (EffectPass {
		.material = material.has_value () ? MaterialParser::load (project, *material)
						  : std::optional<MaterialUniquePtr> {},
		.binds = binds.has_value () ? parseBinds (binds.value (), combos) : std::map<int, std::string> {},
		.command = command.has_value () ? (command.value () == "copy" ? Command_Copy : Command_Swap)
						: std::optional<PassCommandType> {},
		.source = command.has_value ()
		    ? cur.require<std::string> ("source", "Effect command must have a source")
		    : cur.optional<std::string> ("source"),
		.target = command.has_value ()
		    ? cur.require<std::string> ("target", "Effect command must have a target")
		    : cur.optional<std::string> ("target"),
	    })
	);
    }

    return result;
}

std::map<int, std::string> EffectParser::parseBinds (const JSON& it, const JSON& combos) {
    std::map<int, std::string> result = {};

    if (!it.is_array ()) {
	return result;
    }

    for (const auto& cur : it) {
	if (!conditionsPass (cur, combos)) continue;
	result.emplace (
	    cur.require ("index", "Texture binds must have an index"),
	    cur.require ("name", "Texture bind must name the FBO that should be used")
	);
    }

    return result;
}

std::vector<FBOUniquePtr> EffectParser::parseFBOs (const JSON& it, const JSON& combos) {
    std::vector<FBOUniquePtr> result = {};

    if (!it.is_array ()) {
	return result;
    }

    for (const auto& cur : it) {
	if (!conditionsPass (cur, combos)) continue;
	result.push_back (
	    std::make_unique<FBO> (FBO {
		.name = cur.require<std::string> ("name", "FBO must have a name"),
		.format = cur.optional<std::string> ("format", "rgba8888"),
		.scale = cur.optional ("scale", 1.0f),
		.unique = cur.optional ("unique", false),
		.fit = cur.optional ("fit", 0),
	    })
	);
    }

    return result;
}
