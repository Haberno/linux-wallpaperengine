#pragma once

#include <cstdlib>
#include <map>
#include <string>
#include <unordered_set>

#include "WallpaperEngine/Logging/Log.h"

namespace WallpaperEngine::Data::Utils {
/**
 * Opt-in reverse-engineering aid, enabled with WPE_JSON_TELEMETRY=1 (disabled = zero logging).
 *
 * Wallpaper Engine's formats are undocumented: the only way to find features we do not
 * implement yet is noticing keys the parsers never read. JsonExtensions::require/optional
 * report every key name the parsers consume to recordAccess(); once a top-level document
 * (project/scene/effect/material/model) is fully parsed, scan() walks the original JSON and
 * reports every key no parser ever asked for. Aggregated over a corpus of wallpapers this
 * yields a frequency-ranked list of unimplemented fields.
 *
 * Precision notes:
 *  - Matching is by key NAME, not path: a key consumed in one context is considered known
 *    everywhere. That trades precision for zero per-node bookkeeping, which is fine for a
 *    discovery tool: genuinely unknown fields have names nothing else uses.
 *  - Parsers that bypass the require/optional funnel with raw nlohmann access (find/contains/
 *    value) are covered by the static rawAccessKeys() allowlist below; helper lambdas that
 *    look up caller-provided field names call recordAccess() with the runtime name instead.
 *    Remaining false positives should be resolved by reading the parser before assuming a
 *    feature gap.
 *  - Containers whose children are author-chosen names (user properties, shader combos...)
 *    are never descended into: their keys are data, not schema.
 */
class JsonTelemetry {
public:
    static bool enabled () {
	static const bool value = std::getenv ("WPE_JSON_TELEMETRY") != nullptr;

	return value;
    }

    /// called by JsonExtensions for every key the parsers ask for
    static void recordAccess (const std::string& key) {
	if (!enabled ()) {
	    return;
	}

	accessedKeys ().insert (key);
    }

    /**
     * Reports every key in @param document that no parser has asked for so far.
     * Must run AFTER the document has been fully parsed.
     *
     * @param document The original JSON document that was handed to the parsers
     * @param label Identifies the document in the report (usually the filename)
     */
    template <typename JsonT> static void scan (const JsonT& document, const std::string& label) {
	if (!enabled ()) {
	    return;
	}

	auto report = std::map<std::string, int> {};

	walk (document, "", report);

	if (report.empty ()) {
	    sLog.out ("JSON telemetry [", label, "]: no unknown keys");
	    return;
	}

	sLog.out ("JSON telemetry [", label, "]: ", report.size (), " unknown key path(s):");

	for (const auto& [path, count] : report) {
	    sLog.out ("    ", path, " (x", count, ")");
	}
    }

private:
    template <typename JsonT>
    static void walk (const JsonT& node, const std::string& path, std::map<std::string, int>& report) {
	if (node.is_object ()) {
	    for (const auto& cur : node.items ()) {
		const auto childPath = path.empty () ? cur.key () : path + "." + cur.key ();

		if (!accessedKeys ().contains (cur.key ()) && !rawAccessKeys ().contains (cur.key ())) {
		    // do not descend: children of an unconsumed key are unconsumed by definition,
		    // reporting the subtree root keeps the report readable
		    report [childPath]++;
		} else if (!dynamicKeyContainers ().contains (cur.key ())) {
		    walk (cur.value (), childPath, report);
		}
	    }
	} else if (node.is_array ()) {
	    const auto childPath = path + "[]";

	    for (const auto& cur : node) {
		walk (cur, childPath, report);
	    }
	}
    }

    static std::unordered_set<std::string>& accessedKeys () {
	static auto keys = std::unordered_set<std::string> {};

	return keys;
    }

    /**
     * Keys the parsers consume through raw nlohmann access instead of require/optional.
     * Keep in sync with `grep -n '\.find ("\|\.contains ("\|\.value ("' src/WallpaperEngine/Data`
     */
    static const std::unordered_set<std::string>& rawAccessKeys () {
	static const auto keys = std::unordered_set<std::string> {
	    // WallpaperParser / CameraPathParser
	    "camera", "zoom", "transforms", "orthogonalprojection", "fov", "path",
	    // ObjectParser object type discriminators
	    "image", "sound", "particle", "text", "model", "light", "shape",
	    "id", "name", "parallaxDepth", "dependencies", "segments", "offset", "type",
	    // ObjectParser particle sections
	    "emitter", "initializer", "operator", "renderer", "controlpoint", "material",
	    "animationmode", "sequencemultiplier", "maxcount", "starttime", "flags",
	    // TextureParser sprite sheets
	    "spritesheetsequences", "frames", "width", "height", "duration",
	};

	return keys;
    }

    /// containers whose child keys are author-chosen names, not schema
    static const std::unordered_set<std::string>& dynamicKeyContainers () {
	static const auto keys = std::unordered_set<std::string> {
	    "properties",
	    "combos",
	    "constantshadervalues",
	    "scriptproperties",
	};

	return keys;
    }
};
} // namespace WallpaperEngine::Data::Utils
