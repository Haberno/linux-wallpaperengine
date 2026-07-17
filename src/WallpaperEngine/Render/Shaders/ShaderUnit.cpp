#include "ShaderUnit.h"

#include "WallpaperEngine/Logging/Log.h"
#include <exception>
#include <mutex>
#include <regex>
#include <set>
#include <stack>
#include <string>
#include <unordered_map>
#include <utility>

#include "GLSLContext.h"
#include "WallpaperEngine/Assets/AssetLoadException.h"
#include "WallpaperEngine/Render/Shaders/Variables/ShaderVariable.h"
#include "WallpaperEngine/Render/Shaders/Variables/ShaderVariableFloat.h"
#include "WallpaperEngine/Render/Shaders/Variables/ShaderVariableInteger.h"
#include "WallpaperEngine/Render/Shaders/Variables/ShaderVariableVector2.h"
#include "WallpaperEngine/Render/Shaders/Variables/ShaderVariableVector3.h"
#include "WallpaperEngine/Render/Shaders/Variables/ShaderVariableVector4.h"

#include "WallpaperEngine/Data/Builders/VectorBuilder.h"
#include "WallpaperEngine/FileSystem/Container.h"

#define SHADER_HEADER(filename)                                                                                        \
    "#version 330\n"                                                                                                   \
    "// ======================================================\n"                                                      \
    "// Processed shader "                                                                                             \
	+ filename                                                                                                     \
	+ "\n"                                                                                                         \
	  "// ======================================================\n"                                                \
	  "precision highp float;\n"                                                                                   \
	  "#define mul(x, y) ((y) * (x))\n"                                                                            \
	  "#define max(x, y) max (y, x)\n"                                                                             \
	  "#define lerp mix\n"                                                                                         \
	  "#define frac fract\n"                                                                                       \
	  "#define CAST2(x) (vec2(x))\n"                                                                               \
	  "#define CAST3(x) (vec3(x))\n"                                                                               \
	  "#define CAST4(x) (vec4(x))\n"                                                                               \
	  "#define CAST3X3(x) (mat3(x))\n"                                                                             \
	  "#define CASTF(x) (float(x))\n"                                                                              \
	  "#define CASTU(x) (uint(x))\n"                                                                               \
	  "#define float2 vec2\n"                                                                                      \
	  "#define float3 vec3\n"                                                                                      \
	  "#define float4 vec4\n"                                                                                      \
	  "#define int2 ivec2\n"                                                                                       \
	  "#define int3 ivec3\n"                                                                                       \
	  "#define int4 ivec4\n"                                                                                       \
	  "#define saturate(x) (clamp(x, 0.0, 1.0))\n"                                                                 \
	  "#define texSample2D texture\n"                                                                              \
	  "#define texSample2DLod textureLod\n"                                                                        \
	  "#define sampler2DComparison sampler2DShadow\n"                                                              \
	  "#define texSample2DCompare(s, uv, reference) vec4(texture((s), vec3((uv), (reference))))\n"                   \
	  "#define log10(x) (log2(x) * 0.301029995663981)\n"                                                           \
	  "#define atan2 atan\n"                                                                                       \
	  "#define fmod(x, y) ((x)-(y)*trunc((x)/(y)))\n"                                                              \
	  "#define ddx dFdx\n"                                                                                         \
	  "#define ddy(x) dFdy(-(x))\n"                                                                                \
	  "#define GLSL 1\n\n";
#define FRAGMENT_SHADER_DEFINES                                                                                        \
    "out vec4 out_FragColor;\n"                                                                                        \
    "#define varying in\n"
#define VERTEX_SHADER_DEFINES                                                                                          \
    "#define attribute in\n"                                                                                           \
    "#define varying out\n"
#define DEFINE_COMBO(name, value) "#define " + name + " " + std::to_string (value) + "\n";

using namespace WallpaperEngine::Render;
using namespace WallpaperEngine::Data::Builders;
using namespace WallpaperEngine::Render::Shaders;

ShaderUnit::ShaderUnit (
    const GLSLContext::UnitType type, std::string file, std::string content, const AssetLocator& assetLocator,
    const ShaderConstantMap& constants, const TextureMap& passTextures, const TextureMap& overrideTextures,
    const ComboMap& combos, const ComboMap& overrideCombos
) :
    m_type (type), m_file (std::move (file)), m_content (std::move (content)), m_combos (combos),
    m_overrideCombos (overrideCombos), m_constants (constants), m_passTextures (passTextures),
    m_overrideTextures (overrideTextures), m_link (nullptr), m_assetLocator (assetLocator) {
    // pre-process the shader so the units are clear
    this->preprocess ();
}

void ShaderUnit::preprocess () {
    this->m_preprocessed = this->m_content;
    this->m_includes = "";

    this->preprocessIncludes ();
    this->preprocessRequires ();
    this->preprocessVariables ();
    this->resolveComboRequires ();
    this->stripOrphanedEndifs ();

    // replace gl_FragColor with the equivalent
    const std::string from = "gl_FragColor";
    const std::string to = "out_FragColor";

    size_t start_pos = 0;
    while ((start_pos = this->m_preprocessed.find (from, start_pos)) != std::string::npos) {
	this->m_preprocessed.replace (start_pos, from.length (), to);
	start_pos += to.length (); // Handles case where 'to' is a substring of 'from'
    }
}

void ShaderUnit::preprocessVariables () {
    size_t start = 0, end = 0;
    while ((end = this->m_preprocessed.find ('\n', start)) != std::string::npos) {
	// Extract a line from the string
	std::string line = this->m_preprocessed.substr (start, end - start);
	const size_t combo = line.find ("// [COMBO] ");
	const size_t uniform = line.find ("uniform ");
	const size_t comment = line.find ("// ");
	const size_t semicolon = line.find (';');

	if (combo != std::string::npos) {
	    this->parseComboConfiguration (line.substr (combo + strlen ("// [COMBO] ")), 0);
	} else if (
	    uniform != std::string::npos && comment != std::string::npos && semicolon != std::string::npos &&
	    // this check ensures that the comment is after the semicolon (so it's not a commented-out line)
	    // this needs further refining as it's not taking into account block comments
	    semicolon < comment
	) {
	    // uniforms with comments should never have a value assigned, use this fact to detect the required parts
	    const size_t last_space = line.find_last_of (' ', semicolon);

	    if (last_space != std::string::npos) {
		const size_t previous_space = line.find_last_of (' ', last_space - 1);

		if (previous_space != std::string::npos) {
		    // extract type and name
		    std::string type = line.substr (previous_space + 1, last_space - previous_space - 1);
		    std::string name = line.substr (last_space + 1, semicolon - last_space - 1);
		    std::string json = line.substr (comment + 2);

		    this->parseParameterConfiguration (type, name, json);
		}
	    }
	}

	// Move to the next line
	start = end + 1;
    }
}

namespace {
// memoization cache for the include+ifdef preprocessing passes: their result only
// depends on the unit's source text and the container includes resolve from, so
// repeated builds (combo variants, wallpaper re-switches) can skip the string work
struct PreprocessedIncludes {
    std::string preprocessed;
    std::string includes;
};

std::mutex sIncludeCacheMutex;
std::unordered_map<std::string, PreprocessedIncludes> sIncludeCache;

std::string buildIncludeCacheKey (const std::string& locatorIdentity, const std::string& file, const std::string& content) {
    std::string key = locatorIdentity;
    key += '|';
    key += file;
    key += '|';
    key += std::to_string (std::hash<std::string> {} (content));
    return key;
}
} // namespace

void ShaderUnit::preprocessIncludes () {
    // check the memo cache first, keyed on the locator's mount fingerprint (stable across
    // engine restarts of the same project), the unit's file and its source text
    const std::string cacheKey = buildIncludeCacheKey (this->m_assetLocator.identity (), this->m_file, this->m_preprocessed);

    {
	std::lock_guard lock (sIncludeCacheMutex);
	if (const auto found = sIncludeCache.find (cacheKey); found != sIncludeCache.end ()) {
	    this->m_preprocessed = found->second.preprocessed;
	    this->m_includes = found->second.includes;
	    return;
	}
    }

    size_t start = 0, end = 0;
    // prepare the include content
    while ((start = this->m_preprocessed.find ("#include", end)) != std::string::npos) {
	// TODO: CHECK FOR ERRORS HERE, MALFORMED INCLUDES WILL NOT BE PROPERLY HANDLED
	const size_t quoteStart = this->m_preprocessed.find_first_of ('"', start) + 1;
	const size_t quoteEnd = this->m_preprocessed.find_first_of ('"', quoteStart);
	const std::string filename = this->m_preprocessed.substr (quoteStart, quoteEnd - quoteStart);

	// some includes might not be present
	// and that should not be treated as an error mainly because these could come from
	// commented out content
	std::string content;

	try {
	    content += "// begin of include from file ";
	    content += filename;
	    content += "\n";
	    content += this->m_assetLocator.includeShader (filename);
	    content += "\n// end of included from file ";
	    content += filename;
	    content += "\n";
	} catch (AssetLoadException&) {
	    content += "// tried including file ";
	    content += filename;
	    content += " but was not found\n";
	}

	// replace the first two letters with a comment so the filelength doesn't change
	this->m_preprocessed = this->m_preprocessed.replace (start, 2, "//");

	this->m_includes += content;

	// go to the end of the line
	end = start;
    }

    // ensure the included files do not include other files
    end = 0;

    // then apply includes in-place
    while ((start = this->m_includes.find ("#include", end)) != std::string::npos) {
	const size_t lineEnd = this->m_includes.find_first_of ('\n', start);
	// TODO: CHECK FOR ERRORS HERE, MALFORMED INCLUDES WILL NOT BE PROPERLY HANDLED
	const size_t quoteStart = this->m_includes.find_first_of ('"', start) + 1;
	const size_t quoteEnd = this->m_includes.find_first_of ('"', quoteStart);
	const std::string filename = this->m_includes.substr (quoteStart, quoteEnd - quoteStart);

	// some includes might not be present
	// and that should not be treated as an error mainly because these could come from
	// commented out content
	std::string content;

	try {
	    content = "// begin of include from file ";
	    content += filename;
	    content += "\n";
	    content += this->m_assetLocator.includeShader (filename);
	    content += "\n// end of included from file ";
	    content += filename;
	    content += "\n";
	} catch (AssetLoadException&) {
	    content = "// tried including file ";
	    content += filename;
	    content += " but was not found\n";
	}

	// file contents ready, replace things
	this->m_includes = this->m_includes.replace (start, lineEnd - start, content);

	// go back to the beginning of the line to properly continue detecting things
	end = start;
    }

    // search for the main function and add the includes before that for now
    end = 0;
    bool includesAdded = false;

    // finally, try to place the include contents before the main function
    while ((start = this->m_preprocessed.find (" main", end)) != std::string::npos) {
	char value = this->m_preprocessed.at (start + 5);

	end = start + 5;

	if (value != ' ' && value != '(') {
	    continue;
	}

	// main located, search for uniforms and find the latest one available
	size_t lastAttribute = this->m_preprocessed.rfind ("attribute", start);
	size_t lastVarying = this->m_preprocessed.rfind ("varying", start);
	size_t lastUniform = this->m_preprocessed.rfind ("uniform", start);
	size_t latest = lastAttribute;

	if (latest == std::string::npos) {
	    latest = lastVarying;
	} else if (latest < lastVarying && lastVarying != std::string::npos) {
	    latest = lastVarying;
	}

	if (latest == std::string::npos) {
	    latest = lastUniform;
	} else if (latest < lastUniform && lastUniform != std::string::npos) {
	    latest = lastUniform;
	}

	if (latest < start) {
	    // find the end of the current line
	    latest = this->m_preprocessed.find ('\n', latest);
	} else {
	    // find the end of the previous line
	    latest = this->m_preprocessed.rfind ('\n', start);
	}

	// update the function start to point to the end of the previous line
	// as this will be used to determine the position of the includes
	start = this->m_preprocessed.rfind ('\n', start);

	// keeps track of the start and end of ifdefs to look for the right
	// place to put the includes in
	std::stack<size_t> ifdefStack;

	// start looking for #if and #endif results and add to the stack so we find the start of the current chain of
	// ifdefs and use that as point

	// for this we'll use regex
	static const std::regex ifdef (R"((#if|#endif))");
	std::smatch match;
	size_t current = 0;

	while (
	    std::regex_search (this->m_preprocessed.cbegin () + current, this->m_preprocessed.cend (), match, ifdef)) {
	    current += match.position ();

	    // if it's opening an #ifdef keep track of the start of the block
	    // and that's it
	    if (this->m_preprocessed.substr (current, 3) == "#if") {
		// go to the next character so the regex doesn't match with the same thing again
		ifdefStack.push (current++);
		continue;
	    }

	    // go to the next character so the regex doesn't match with the same thing again
	    current++;

	    // most likely a syntax error, but we'll ignore it for now...
	    if (ifdefStack.empty ()) {
		continue;
	    }

	    size_t stackStart = ifdefStack.top ();
	    ifdefStack.pop ();

	    if (latest > stackStart && latest <= current) {
		// The insertion point is inside a conditional block.
		// Move to BEFORE the #if so includes are available to all branches
		// (e.g. genericropeparticle.vert has #if GS_ENABLED wrapping two main() functions).
		size_t beforeIfdef = this->m_preprocessed.rfind ('\n', stackStart);
		latest = (beforeIfdef != std::string::npos) ? beforeIfdef : 0;
	    }
	}

	// no more matches, get the one that happens the earliest
	// TODO: IS THIS GOOD ENOUGH? MAYBE WE SHOULD BE GETTING THE FIRST #IF BLOCK INSTEAD?
	latest = std::min (latest, start);

	// finally insert it there
	this->m_preprocessed.insert (latest + 1, this->m_includes + '\n');
	includesAdded = true;
	break;
    }

    if (!includesAdded) {
	sLog.exception ("Could not find where to place includes for shader unit ", this->m_file);
    }

    // memoize the result for the next build of this unit. the key embeds the
    // asset locator address, so entries go permanently stale once a project is
    // unloaded (every switch loads a fresh locator) — without a bound this map
    // grows by several MB per switch. hits only matter within a single build,
    // so a wholesale clear on overflow is cheap and keeps memory bounded
    {
	std::lock_guard lock (sIncludeCacheMutex);
	if (sIncludeCache.size () >= 1024)
	    sIncludeCache.clear ();
	sIncludeCache.emplace (cacheKey, PreprocessedIncludes { this->m_preprocessed, this->m_includes });
    }
}

void ShaderUnit::preprocessRequires () {
    size_t start = 0, end = 0;

    while ((start = this->m_preprocessed.find ("#require", end)) != std::string::npos) {
	const size_t lineEnd = this->m_preprocessed.find_first_of ('\n', start);

	const size_t nameStart = start + std::string ("#require ").length ();

	if (nameStart >= lineEnd) {
	    sLog.error ("Malformed #require directive (no module name) in shader ", this->m_file);
	    end = lineEnd;
	    continue;
	}

	std::string moduleName = this->m_preprocessed.substr (nameStart, lineEnd - nameStart);

	while (!moduleName.empty () && (moduleName.back () == ' ' || moduleName.back () == '\r')) {
	    moduleName.pop_back ();
	}

	if (moduleName.empty ()) {
	    sLog.error ("Malformed #require directive (empty module name) in shader ", this->m_file);
	    end = lineEnd;
	    continue;
	}

	sLog.out ("Resolving require module: ", moduleName, " in shader ", this->m_file);

	std::string moduleCode = this->resolveRequireModule (moduleName);

	// comment out the #require directive
	this->m_preprocessed = this->m_preprocessed.replace (start, 2, "//");

	if (!moduleCode.empty ()) {
	    // insert the generated code directly into m_preprocessed at the #require location
	    // (m_includes was already consumed by preprocessIncludes, so appending there would be lost)
	    this->m_preprocessed.insert (start, moduleCode);
	    end = start + moduleCode.length ();
	} else {
	    end = lineEnd;
	}
    }
}

std::string ShaderUnit::resolveRequireModule (const std::string& moduleName) const {
    if (moduleName == "LightingV1") {
	return this->generateLightingV1 ();
    }

    sLog.error ("Unknown #require module: ", moduleName, " in shader ", this->m_file);
    return "";
}

std::string ShaderUnit::generateLightingV1 () const {
    // PerformLighting_V1 is dynamically generated by Wallpaper Engine based on the scene's
    // light sources: an unrolled block per light, calling the ComputePBRLightShadow* helpers
    // from common_pbr_2.h. CPass passes the scene's light counts in through the combo maps.
    // Shadow-capable spotlights use the stock common_pbr_2.h atlas lookup; all other
    // light types retain an unshadowed factor of 1.0.
    const auto comboValue = [this] (const std::string& name) -> int {
	if (const auto it = this->m_overrideCombos.find (name); it != this->m_overrideCombos.end ()) {
	    return it->second;
	}

	if (const auto it = this->m_combos.find (name); it != this->m_combos.end ()) {
	    return it->second;
	}

	return 0;
    };

    const int directionalLights = comboValue ("LIGHTS_DIRECTIONAL");
    const int pointLights = comboValue ("LIGHTS_POINT");
    const int spotLights = comboValue ("LIGHTS_SPOT");
    const int spotShadowLights = comboValue ("LIGHTS_SPOT_SHADOW");
    const int tubeLights = comboValue ("LIGHTS_TUBE");

    std::string code = "// begin of generated module LightingV1\n";

    if (directionalLights > 0) {
	code += "uniform vec4 g_LDirectional_Direction[" + std::to_string (directionalLights) + "];\n";
	code += "uniform vec4 g_LDirectional_Color[" + std::to_string (directionalLights) + "];\n";
    }

    if (pointLights > 0) {
	code += "uniform vec4 g_LPoint_Origin[" + std::to_string (pointLights) + "];\n";
	code += "uniform vec4 g_LPoint_Color[" + std::to_string (pointLights) + "];\n";
    }

    if (spotLights > 0) {
	code += "uniform vec4 g_LSpot_Origin[" + std::to_string (spotLights) + "];\n";
	code += "uniform vec4 g_LSpot_Direction[" + std::to_string (spotLights) + "];\n";
	code += "uniform vec4 g_LSpot_Color[" + std::to_string (spotLights) + "];\n";
	code += "uniform vec4 g_LSpot_Exponent[" + std::to_string (spotLights) + "];\n";
	if (spotShadowLights > 0) {
	    code += "uniform mat4 g_LFeature_ShadowProjection[" + std::to_string (spotLights) + "];\n";
	    code += "uniform vec4 g_LFeature_ShadowProjectionTransform[" + std::to_string (spotLights)
		+ "];\n";
	    code += "uniform float g_LSpot_ShadowEnabled[" + std::to_string (spotLights) + "];\n";
	}
    }

    if (tubeLights > 0) {
	code += "uniform vec4 g_LTube_OriginA[" + std::to_string (tubeLights) + "];\n";
	code += "uniform vec4 g_LTube_OriginB[" + std::to_string (tubeLights) + "];\n";
	code += "uniform vec4 g_LTube_Color[" + std::to_string (tubeLights) + "];\n";
    }

    code += "vec3 PerformLighting_V1(vec3 worldPos, vec3 albedo, vec3 normal, vec3 viewDir,\n"
	    "    vec3 specularTint, vec3 baseReflectance, float roughness, float metallic)\n"
	    "{\n"
	    "    vec3 light = vec3(0.0);\n";

    for (int i = 0; i < pointLights; i++) {
	const std::string index = std::to_string (i);

	code += "    {\n"
		"        vec3 lightDelta = g_LPoint_Origin[" + index + "].xyz - worldPos;\n"
		"        light += ComputePBRLightShadow(normal, lightDelta, viewDir, albedo,\n"
		"            g_LPoint_Color[" + index + "].rgb, g_LPoint_Color[" + index + "].w,\n"
		"            g_LPoint_Origin[" + index + "].w, specularTint, baseReflectance,\n"
		"            roughness, metallic, 1.0);\n"
		"    }\n";
    }

    for (int i = 0; i < spotLights; i++) {
	const std::string index = std::to_string (i);

	code += "    {\n"
		"        vec3 lightDelta = g_LSpot_Origin[" + index + "].xyz - worldPos;\n"
		"        float spotCookie = -dot(normalize(lightDelta), g_LSpot_Direction[" + index + "].xyz);\n"
		"        spotCookie = smoothstep(g_LSpot_Direction[" + index + "].w,\n"
		"            g_LSpot_Origin[" + index + "].w, spotCookie);\n";
	if (spotShadowLights > 0) {
	    code += "        float shadowFactor = 1.0;\n"
		    "        if (g_LSpot_ShadowEnabled[" + index + "] > 0.5) {\n"
		    "            vec3 projectedCoords = CalculateProjectedCoords(worldPos,\n"
		    "                g_LFeature_ShadowProjection[" + index + "]);\n"
		    // CalculateProjectedCoords follows Direct3D texture orientation and depth.
		    // The atlas is rasterized by OpenGL, so restore bottom-left Y and [0,1] Z.
		    "            projectedCoords.y = 1.0 - projectedCoords.y;\n"
		    "            projectedCoords.z = projectedCoords.z * 0.5 + 0.5 - 0.0015;\n"
		    "            if (all(greaterThanEqual(projectedCoords, vec3(0.0))) &&\n"
		    "                all(lessThanEqual(projectedCoords, vec3(1.0)))) {\n"
		    "                shadowFactor = PerformShadowMapping(projectedCoords,\n"
		    "                    g_LFeature_ShadowProjectionTransform[" + index + "]);\n"
		    "            }\n"
		    "        }\n";
	} else {
	    code += "        float shadowFactor = 1.0;\n";
	}
	code +=
		"        light += ComputePBRLightShadow(normal, lightDelta, viewDir, albedo,\n"
		"            g_LSpot_Color[" + index + "].rgb * spotCookie, g_LSpot_Color[" + index + "].w,\n"
		"            g_LSpot_Exponent[" + index + "].x, specularTint, baseReflectance,\n"
		"            roughness, metallic, shadowFactor);\n"
		"    }\n";
    }

    for (int i = 0; i < tubeLights; i++) {
	const std::string index = std::to_string (i);

	code += "    {\n"
		"        vec3 lightDelta = PointSegmentDelta(worldPos, g_LTube_OriginA[" + index
		+ "].xyz, g_LTube_OriginB[" + index + "].xyz);\n"
		"        light += ComputePBRLightShadow(normal, lightDelta, viewDir, albedo,\n"
		"            g_LTube_Color[" + index + "].rgb, g_LTube_Color[" + index + "].w,\n"
		"            g_LTube_OriginA[" + index + "].w, specularTint, baseReflectance,\n"
		"            roughness, metallic, 1.0);\n"
		"    }\n";
    }

    for (int i = 0; i < directionalLights; i++) {
	const std::string index = std::to_string (i);

	code += "    light += ComputePBRLightShadowInfinite(normal, g_LDirectional_Direction[" + index + "].xyz,\n"
		"        viewDir, albedo, g_LDirectional_Color[" + index + "].rgb, specularTint,\n"
		"        baseReflectance, roughness, metallic, 1.0);\n";
    }

    code += "    return light;\n"
	    "}\n"
	    "// end of generated module LightingV1\n";

    return code;
}

std::string ShaderUnit::applyLinkedVaryingCompatibility (std::string source) const {
    if (this->m_type != GLSLContext::UnitType_Vertex || this->m_link == nullptr) {
	return source;
    }

    static const std::regex fragmentVec4Varying (R"(\bvarying\s+vec4\s+([A-Za-z_][A-Za-z0-9_]*)\s*;)");
    std::smatch varyingMatch;
    std::string linked = this->m_link->m_preprocessed;
    size_t linkedOffset = 0;

    while (std::regex_search (linked.cbegin () + linkedOffset, linked.cend (), varyingMatch, fragmentVec4Varying)) {
	const std::string name = varyingMatch[1].str ();
	linkedOffset += varyingMatch.position () + varyingMatch.length ();

	const std::regex vertexVec2Decl ("\\bvarying\\s+vec2\\s+" + name + "\\s*;");
	const std::regex vertexVec4Decl ("\\bvarying\\s+vec4\\s+" + name + "\\s*;");
	const bool vertexHasVec2 = std::regex_search (source, vertexVec2Decl);
	const bool vertexHasVec4 = std::regex_search (source, vertexVec4Decl);

	// Skip entirely if this varying isn't declared in the vertex shader at all.
	if (!vertexHasVec2 && !vertexHasVec4) {
	    continue;
	}

	// If the vertex shader already declares this varying as vec4 (even inside a combo
	// branch), the shader handles its own type variants — leave it untouched. The
	// second pass below handles any vec4->vec2 read contexts independently.
	if (vertexHasVec4) {
	    continue;
	}

	// Upgrade vec2 declaration to vec4 and wrap bare assignments.
	source = std::regex_replace (source, vertexVec2Decl, "varying vec4 " + name + ";");

	const std::regex assignment ("(^|\\n)([ \\t]*)" + name + "\\s*=\\s*([^;\\n]+);");
	std::smatch assignmentMatch;
	size_t offset = 0;
	while (std::regex_search (source.cbegin () + offset, source.cend (), assignmentMatch, assignment)) {
	    const std::string prefix = assignmentMatch[1].str ();
	    const std::string indent = assignmentMatch[2].str ();
	    const std::string expression = assignmentMatch[3].str ();
	    const std::string replacement = prefix + indent + name + " = vec4(" + expression + ", 0.0, 1.0);";
	    const size_t position = offset + assignmentMatch.position ();
	    source.replace (position, assignmentMatch.length (), replacement);
	    offset = position + replacement.length ();
	}

	// After upgrading the declaration and wrapping assignments, any remaining
	// unswizzled read of `name` in an arithmetic expression is now a vec4
	// where the surrounding code expects a vec2. Add .xy to each such read.
	//
	// A "read context" occurrence is: \bname\b NOT followed by [.([=]
	// (which would indicate a swizzle, index, call, or assignment LHS)
	// and NOT immediately preceded by a GLSL type keyword (declaration).
	static const auto isWordChar = [] (char c) {
	    return std::isalnum (static_cast<unsigned char> (c)) || c == '_';
	};
	static const auto isGlslType = [] (const std::string& w) {
	    static const std::set<std::string> types = {
		"vec2", "vec3", "vec4", "float", "int", "uint", "bool",
		"mat2", "mat3", "mat4", "mat4x3", "mat3x3",
		"sampler2D", "uvec4", "ivec2", "ivec3", "ivec4",
		"varying", "uniform", "attribute", "in", "out",
	    };
	    return types.count (w) > 0;
	};

	size_t pos = 0;
	while ((pos = source.find (name, pos)) != std::string::npos) {
	    const size_t afterName = pos + name.size ();

	    // Must be a word boundary on both sides
	    if ((pos > 0 && isWordChar (source[pos - 1])) ||
		(afterName < source.size () && isWordChar (source[afterName]))) {
		pos = afterName;
		continue;
	    }

	    // Skip if followed by swizzle '.', index '[', call '(', or assignment '='
	    size_t j = afterName;
	    while (j < source.size () && source[j] == ' ') j++;
	    const char nextChar = j < source.size () ? source[j] : '\0';
	    if (nextChar == '.' || nextChar == '[' || nextChar == '(' || nextChar == '=') {
		pos = afterName;
		continue;
	    }

	    // Skip if immediately preceded by a GLSL type keyword (it's a declaration)
	    size_t k = pos;
	    while (k > 0 && source[k - 1] == ' ') k--;
	    if (k > 0 && isWordChar (source[k - 1])) {
		size_t wordEnd = k;
		while (k > 0 && isWordChar (source[k - 1])) k--;
		if (isGlslType (source.substr (k, wordEnd - k))) {
		    pos = afterName;
		    continue;
		}
	    }

	    source.insert (afterName, ".xy");
	    pos = afterName + 3;
	}
    }

    // SECOND PASS: fix unswizzled reads of vec4 identifiers in vec2 assignment contexts.
    // Covers: (1) varyings already declared as vec4 in the vertex shader,
    //         (2) local vec4 variables (e.g. from mat4 multiply results).
    // WE's own GLSL compiler permits implicit vec4->vec2 truncation; standard glslang
    // does not — so we must insert explicit .xy swizzles.
    {
	static const std::regex anyVec4Re (R"(\bvec4\s+([A-Za-z_][A-Za-z0-9_]*)\b)");
	std::sregex_iterator it (source.cbegin (), source.cend (), anyVec4Re);
	std::sregex_iterator end;
	std::set<std::string> processed;
	for (; it != end; ++it) {
	    const std::string vname = (*it)[1].str ();
	    if (!processed.insert (vname).second) continue;
	    // Find lines of the form "vec2 var = ... vname ..." where vname has no swizzle.
	    // Lazy [^\n]*? so we find the first unswizzled occurrence after the '='.
	    const std::regex vec2Line ("(vec2\\s+\\w+\\s*=[^\\n]*?)\\b(" + vname + ")\\b(?![.\\[\\(])");
	    const std::string patched = std::regex_replace (source, vec2Line, "$1$2.xy");
	    if (patched != source) {
		source = patched;
	    }
	}
    }

    return source;
}

std::string ShaderUnit::applyFragmentTexCoordCompatibility (std::string source) const {
    if (this->m_type != GLSLContext::UnitType_Fragment) {
	return source;
    }

    const std::string original = source;
    static const std::regex wideTexCoordDecl (R"(\bvarying\s+vec[34]\s+v_TexCoord\s*;)");
    static const std::regex narrowTexCoordDecl (R"(\bvarying\s+vec2\s+v_TexCoord\s*;)");

    if (std::regex_search (source, narrowTexCoordDecl) && this->m_link != nullptr
	&& std::regex_search (this->m_link->m_preprocessed, wideTexCoordDecl)) {
	// the linked vertex shader outputs a wider v_TexCoord, so glslang widens this input at
	// link time and every bare (vec2) use of it becomes an implicit vec4 truncation; widen
	// the declaration ourselves and qualify all uses with .xy. declarations are shielded
	// first: shaders like genericparticle declare both widths in different #if branches
	static const std::regex anyTexCoordDecl (R"(\bvarying\s+(vec[234])\s+v_TexCoord\s*;)");
	static const std::regex bareTexCoordUse (R"(\bv_TexCoord\b(?!\s*\.))");
	static const std::regex shieldedNarrowDecl (R"(\bvarying\s+vec2\s+V_TEXCOORD_DECL\s*;)");
	static const std::regex shieldedDecl ("V_TEXCOORD_DECL");
	source = std::regex_replace (source, anyTexCoordDecl, "varying $1 V_TEXCOORD_DECL;");
	source = std::regex_replace (source, bareTexCoordUse, "v_TexCoord.xy");
	source = std::regex_replace (source, shieldedNarrowDecl, "varying vec4 v_TexCoord;");
	source = std::regex_replace (source, shieldedDecl, "v_TexCoord");
    } else if (std::regex_search (source, wideTexCoordDecl)) {
	static const std::regex texCoordBeforeCast2 (R"(\bv_TexCoord\b(\s*[-+*/]\s*CAST2\s*\())");
	static const std::regex cast2BeforeTexCoord (R"((CAST2\s*\([^)]+\)\s*[-+*/]\s*)\bv_TexCoord\b)");
	// HLSL allows implicit truncation on assignment (vec2 x = v_TexCoord), GLSL does not
	static const std::regex vec2AssignTexCoord (R"((\bvec2\s+[A-Za-z_][A-Za-z0-9_]*\s*=\s*)v_TexCoord\s*;)");

	source = std::regex_replace (source, texCoordBeforeCast2, "v_TexCoord.xy$1");
	source = std::regex_replace (source, cast2BeforeTexCoord, "$1v_TexCoord.xy");
	source = std::regex_replace (source, vec2AssignTexCoord, "$1v_TexCoord.xy;");
    }

    if (source != original) {
	sLog.out ("Applied fragment TexCoord vec2 compatibility in ", this->m_file);
    }

    return source;
}

std::string ShaderUnit::applyFragmentWritableVaryings (std::string source) const {
    if (this->m_type != GLSLContext::UnitType_Fragment) {
	return source;
    }

    // Only apply when the fragment shader actually WRITES to v_TexCoord
    // (matches v_TexCoord = and v_TexCoord.xy = etc., but not ==).
    // Shaders that only read v_TexCoord don't need the writable local alias.
    static const std::regex writePattern (R"(\bv_TexCoord(?:\.[xyzwrgba]+)?\s*=[^=])");
    if (!std::regex_search (source, writePattern)) {
	return source;
    }

    // Build an uppercase combo map so we can evaluate #if/#ifdef/#ifndef guards the same way
    // the GLSL compiler does (combos are emitted as uppercase #defines). Insertion order follows
    // this unit's own combo precedence (see resolveComboRequires's `effective` lambda): promoted
    // > override > material > discovered, since emplace keeps the first value seen per key.
    std::map<std::string, int> upperCombos;
    const auto insertUpper = [&] (const ComboMap& map) {
	for (const auto& [k, v] : map) {
	    std::string up;
	    std::ranges::transform (k, std::back_inserter (up), ::toupper);
	    upperCombos.emplace (up, v);
	}
    };
    insertUpper (this->m_promotedCombos);
    insertUpper (this->m_overrideCombos);
    insertUpper (this->m_combos);
    insertUpper (this->m_discoveredCombos);

    // Walk the source line-by-line tracking #if/#else/#endif to find the
    // varying <type> v_TexCoord declaration in the ACTIVE combo branch.
    static const std::regex ifRe     (R"(^\s*#if\s+(\w+))");
    static const std::regex ifdefRe  (R"(^\s*#ifdef\s+(\w+))");
    static const std::regex ifndefRe (R"(^\s*#ifndef\s+(\w+))");
    static const std::regex elseRe   (R"(^\s*#else\b)");
    static const std::regex endifRe  (R"(^\s*#endif\b)");
    static const std::regex varyDecl (R"(\bvarying\s+(\S+)\s+v_TexCoord\s*;)");

    std::vector<bool> activeStack = {true};
    std::string foundType;
    size_t pos = 0;

    while (pos < source.size () && foundType.empty ()) {
	const size_t lineEnd = source.find ('\n', pos);
	const size_t end     = (lineEnd == std::string::npos) ? source.size () : lineEnd;
	const std::string line = source.substr (pos, end - pos);
	std::smatch m;

	if (std::regex_search (line, m, ifRe)) {
	    std::string up;
	    std::ranges::transform (m[1].str (), std::back_inserter (up), ::toupper);
	    const int val = upperCombos.count (up) ? upperCombos.at (up) : 0;
	    activeStack.push_back (activeStack.back () && (val != 0));
	} else if (std::regex_search (line, m, ifdefRe)) {
	    std::string up;
	    std::ranges::transform (m[1].str (), std::back_inserter (up), ::toupper);
	    activeStack.push_back (activeStack.back () && (upperCombos.count (up) > 0));
	} else if (std::regex_search (line, m, ifndefRe)) {
	    std::string up;
	    std::ranges::transform (m[1].str (), std::back_inserter (up), ::toupper);
	    activeStack.push_back (activeStack.back () && (upperCombos.count (up) == 0));
	} else if (std::regex_search (line, elseRe)) {
	    if (activeStack.size () >= 2) {
		const bool parentActive = activeStack[activeStack.size () - 2];
		activeStack.back () = parentActive && !activeStack.back ();
	    }
	} else if (std::regex_search (line, endifRe)) {
	    if (activeStack.size () > 1) {
		activeStack.pop_back ();
	    }
	} else if (activeStack.back () && std::regex_search (line, m, varyDecl)) {
	    foundType = m[1].str ();
	}

	pos = (lineEnd == std::string::npos) ? source.size () : lineEnd + 1;
    }

    if (foundType.empty ()) {
	return source;
    }

    // Inject a local writable alias at the top of main().
    // GLSL scoping: the RHS v_TexCoord resolves to the outer-scope (read-only) input
    // before the local variable enters scope — the local copy is then freely writable.
    const size_t mainPos = source.find ("void main");
    if (mainPos == std::string::npos) {
	return source;
    }
    const size_t bracePos = source.find ('{', mainPos);
    if (bracePos == std::string::npos) {
	return source;
    }

    source.insert (bracePos + 1, "\n    " + foundType + " v_TexCoord = v_TexCoord;");
    return source;
}

std::string ShaderUnit::applyFloatTernaryCompatibility (std::string source) const {
    // HLSL allows float/int as ternary conditions; GLSL 330 requires bool.
    // Match a bare identifier (no operators before it) used as a ternary condition in:
    //   assignment:    = ident ?
    //   return:        return ident ?
    //   argument:      ( ident ?
    //   comma-sep:     , ident ?
    // and wrap it with bool() so GLSL accepts it.  bool(bool_val) is a no-op, so
    // wrapping an already-bool identifier is harmless.
    static const std::regex floatTernaryRe (
        R"(((?:=\s*|return\s+|,\s*|\(\s*))([A-Za-z_][A-Za-z0-9_]*)\s*\?)"
    );
    const std::string patched = std::regex_replace (source, floatTernaryRe, "$1bool($2) ?");
    if (patched != source) {
	sLog.out ("Applied float-ternary bool() cast in ", this->m_file);
    }
    return patched;
}

void ShaderUnit::parseComboConfiguration (const std::string& content, const int defaultValue) {
    JSON data;
    try {
	data = JSON::parse (content);
    } catch (const std::exception& e) {
	sLog.error ("Cannot parse combo metadata in shader ", this->m_file, ": ", e.what ());
	return;
    }
    const auto combo = data.require<std::string> ("combo", "cannot parse combo information");
    // ignore type as it seems to be used only on the editor
    // const auto type = data.find ("type");
    const auto defvalue = data.find ("default");

    // Record the "require" dependency, e.g. {"combo":"RIMLIGHTING","require":{"LIGHTING":1}}. Wallpaper Engine
    // only compiles a combo's code path when its requirements hold, so an enabled combo forces its requirement
    // on (resolved later in resolveComboRequires once every combo's value is known).
    if (const auto require = data.find ("require"); require != data.end () && require->is_object ()) {
	ComboMap requirements;
	for (const auto& [name, value] : require->items ()) {
	    if (value.is_number_integer ()) {
		requirements.emplace (name, value.get<int> ());
	    }
	}
	if (!requirements.empty ()) {
	    this->m_comboRequires.emplace (combo, std::move (requirements));
	}
    }

    // check the combos
    const auto entry = this->m_combos.find (combo);
    const auto entryOverride = this->m_overrideCombos.find (combo);

    // add the combo to the found list
    this->m_usedCombos.emplace (combo, true);

    // if the combo was not found in the predefined values this means that the default value in the JSON data can be
    // used so only define the ones that are not already defined
    if (entry == this->m_combos.end () && entryOverride == this->m_overrideCombos.end ()) {
	// if no combo is defined just load the default settings
	if (defvalue == data.end ()) {
	    // TODO: PROPERLY SUPPORT EMPTY COMBOS
	    this->m_discoveredCombos.emplace (combo, defaultValue);
	} else if (defvalue->is_number_float ()) {
	    sLog.exception ("float combos are not supported in shader ", this->m_file, ". ", combo);
	} else if (defvalue->is_number_integer ()) {
	    this->m_discoveredCombos.emplace (combo, defvalue->get<int> ());
	} else if (defvalue->is_string ()) {
	    sLog.exception ("string combos are not supported in shader ", this->m_file, ". ", combo);
	} else {
	    sLog.exception ("cannot parse combo information ", combo, ". unknown type for ", defvalue->dump ());
	}
    }
}

void ShaderUnit::resolveComboRequires () {
    if (this->m_comboRequires.empty ()) {
	return;
    }

    // Effective value of a combo across the precedence chain (promoted > override > material > discovered).
    const auto effective = [this] (const std::string& name) -> int {
	if (const auto it = this->m_promotedCombos.find (name); it != this->m_promotedCombos.end ()) {
	    return it->second;
	}
	if (const auto it = this->m_overrideCombos.find (name); it != this->m_overrideCombos.end ()) {
	    return it->second;
	}
	if (const auto it = this->m_combos.find (name); it != this->m_combos.end ()) {
	    return it->second;
	}
	if (const auto it = this->m_discoveredCombos.find (name); it != this->m_discoveredCombos.end ()) {
	    return it->second;
	}
	return 0;
    };

    // Fixed-point iteration so transitive requirements (A requires B requires C) all resolve. The combo set
    // is tiny, so the bounded loop is cheap; the guard just prevents a cyclic annotation from spinning.
    constexpr int kMaxComboRequireIterations = 16;
    bool changed = true;
    for (int guard = 0; changed && guard < kMaxComboRequireIterations; ++guard) {
	changed = false;
	for (const auto& [combo, requirements] : this->m_comboRequires) {
	    if (effective (combo) == 0) {
		continue; // combo disabled -> its requirements do not apply
	    }
	    for (const auto& [required, value] : requirements) {
		if (effective (required) != value) {
		    this->m_promotedCombos.insert_or_assign (required, value);
		    changed = true;
		}
	    }
	}
    }
}

void ShaderUnit::parseParameterConfiguration (
    const std::string& type, const std::string& name, const std::string& content
) {
    JSON data;
    try {
	data = JSON::parse (content);
    } catch (const std::exception& e) {
	sLog.error ("Cannot parse parameter metadata for ", name, " in shader ", this->m_file, ": ", e.what ());
	return;
    }
    const auto material = data.optional ("material");
    const auto defvalue = data.optional ("default");
    // auto range = data.find ("range");
    const auto combo = data.find ("combo");

    // this is not a real parameter
    auto constant = this->m_constants.end ();

    if (material.has_value ()) {
	constant = this->m_constants.find (*material);
    }

    if (constant == this->m_constants.end () && !defvalue.has_value ()) {
	if (type != "sampler2D") {
	    sLog.exception ("Cannot parse parameter data for ", name, " in shader ", this->m_file);
	}
    }

    Variables::ShaderVariable* parameter = nullptr;

    if (type == "vec4") {
	parameter
	    = new Variables::ShaderVariableVector4 (VectorBuilder::parse<glm::vec4> (defvalue->get<std::string> ()));
    } else if (type == "vec3") {
	parameter = new Variables::ShaderVariableVector3 (VectorBuilder::parse<glm::vec3> (*defvalue));
    } else if (type == "vec2") {
	parameter = new Variables::ShaderVariableVector2 (VectorBuilder::parse<glm::vec2> (*defvalue));
    } else if (type == "float") {
	if (defvalue->is_string ()) {
	    parameter = new Variables::ShaderVariableFloat (std::stoi (defvalue->get<std::string> ()));
	} else {
	    parameter = new Variables::ShaderVariableFloat (defvalue->get<float> ());
	}
    } else if (type == "int") {
	if (defvalue->is_string ()) {
	    parameter = new Variables::ShaderVariableInteger (std::stoi (defvalue->get<std::string> ()));
	} else {
	    parameter = new Variables::ShaderVariableInteger (defvalue->get<int> ());
	}
    } else if (type == "sampler2D" || type == "sampler2DComparison") {
	// samplers can have special requirements, check what sampler we're working with and create definitions
	// if needed
	const auto textureName = data.find ("default");
	// TODO: CREATE TEXTURE WITH THE GIVEN COLOR
	// extract the texture number from the name
	const char value = name.at (std::string ("g_Texture").length ());
	const auto requireany = data.find ("requireany");
	const auto require = data.find ("require");
	// now convert it to integer
	// TODO: BETTER CONVERSION HERE
	size_t index = value - '0';
	// TODO: SUPPORT USER TEXTURES!!

	if (combo != data.end ()) {
	    // TODO: CLEANUP HOW THIS IS DETERMINED FIRST
	    // if the texture exists (and is not null), add to the combo
	    const auto textureSlotUsed
		= this->m_passTextures.contains (index) || this->m_overrideTextures.contains (index);
	    bool isRequired = false;
	    int comboValue = 1;

	    if (textureSlotUsed) {
		// nothing extra to do, the texture exists, the combo must be set
		// these tend to not have default value
		isRequired = true;
	    } else if (require != data.end ()) {
		// this is required based on certain conditions
		if (requireany != data.end () && requireany->get<bool> ()) {
		    // any of the values set are valid, check for them
		    for (const auto& item : require->items ()) {
			const std::string& macro = item.key ();
			const auto it = this->m_combos.find (macro);

			// if any of the values matched, this option is required
			if (it == this->m_combos.end () || this->m_overrideCombos.contains (macro)
			    || it->second != item.value ()) {
			    isRequired = true;
			    break;
			}
		    }
		} else {
		    isRequired = true;

		    // all values must match for it to be required
		    for (const auto& item : require->items ()) {
			const std::string& macro = item.key ();
			const auto it = this->m_combos.find (macro);

			// these can not exist and that'd be fine, we just care about the values
			if ((it != this->m_combos.end () || this->m_overrideCombos.contains (macro))
			    && it->second == item.value ()) {
			    isRequired = false;
			    break;
			}
		    }
		}
	    }

	    if (isRequired && !textureSlotUsed) {
		if (!defvalue.has_value ()) {
		    isRequired = false;
		} else {
		    // is the combo registered already?
		    // if not, add it with the default value
		    // there's already a combo providing this value, so it doesn't need to be added
		    if (this->m_combos.contains (*combo) || this->m_overrideCombos.contains (*combo)) {
			isRequired = false;
			// otherwise a default value must be used
		    } else if (defvalue->is_string ()) {
			comboValue = std::stoi (defvalue->get<std::string> ().c_str ());
		    } else if (defvalue->is_number ()) {
			comboValue = *defvalue;
		    } else {
			sLog.exception (
			    "Cannot determine default value for combo ", combo->get<std::string> (),
			    " because it's not specified by the shader and is not given a default value: ", this->m_file
			);
		    }
		}
	    }

	    if (isRequired) {
		// add the new combo to the list
		this->m_discoveredCombos.emplace (*combo, comboValue);
		// textures linked to combos need to be tracked too
		this->m_usedCombos.emplace (*combo, true);
	    }
	}

	if (textureName != data.end ()) {
	    this->m_defaultTextures.emplace (index, *textureName);
	}

	// samplers are not saved, we can ignore them for now
	return;
    } else {
	sLog.error ("Unknown parameter type: ", type, " for ", name, " in shader ", this->m_file);
	return;
    }

    if (material.has_value () && parameter != nullptr) {
	parameter->setIdentifierName (*material);
	parameter->setName (name);

	this->m_parameters.push_back (parameter);
    }
}

void ShaderUnit::stripOrphanedEndifs () {
    // Workshop shaders (authored for HLSL's lenient preprocessor) sometimes emit one extra
    // #endif that has no matching #if.  GLSL's preprocessor rejects these.  Walk the shader
    // line-by-line, track depth, and blank-out any #endif whose depth would underflow.
    int depth = 0;
    size_t pos = 0;

    while (pos < this->m_preprocessed.size ()) {
        const size_t lineEnd = this->m_preprocessed.find ('\n', pos);
        const size_t end = (lineEnd == std::string::npos) ? this->m_preprocessed.size () : lineEnd;

        // Trim leading whitespace for the directive check only
        size_t tok = pos;
        while (tok < end && (this->m_preprocessed [tok] == ' ' || this->m_preprocessed [tok] == '\t')) {
            tok++;
        }

        const size_t remaining = end - tok;
        if (remaining >= 3 && this->m_preprocessed.substr (tok, 3) == "#if") {
            depth++;
        } else if (remaining >= 6 && this->m_preprocessed.substr (tok, 6) == "#endif") {
            if (depth == 0) {
                // orphaned #endif — comment it out
                this->m_preprocessed [tok] = '/';
                this->m_preprocessed [tok + 1] = '/';
            } else {
                depth--;
            }
        }

        pos = (lineEnd == std::string::npos) ? this->m_preprocessed.size () : lineEnd + 1;
    }
}

const ComboMap& ShaderUnit::getCombos () const { return this->m_combos; }

const ComboMap& ShaderUnit::getDiscoveredCombos () const { return this->m_discoveredCombos; }

const ComboMap& ShaderUnit::getPromotedCombos () const { return this->m_promotedCombos; }

void ShaderUnit::linkToUnit (const ShaderUnit* unit) { this->m_link = unit; }

const ShaderUnit* ShaderUnit::getLinkedUnit () const { return this->m_link; }

const std::string& ShaderUnit::compile () {
    if (!this->m_final.empty ()) {
	return this->m_final;
    }

    this->m_final = SHADER_HEADER (this->m_file);

    if (this->m_type == GLSLContext::UnitType_Fragment) {
	this->m_final += FRAGMENT_SHADER_DEFINES;
    } else {
	this->m_final += VERTEX_SHADER_DEFINES;
    }

    std::map<std::string, bool> addedCombos;

    // Combos forced on by the [COMBO] "require" chain come first so they win over the material/override values.
    // The linked unit's promotions are emitted too: the [COMBO] annotations live in one unit (usually the
    // fragment) but the #if guards span both, so vertex and fragment must agree on the promoted combo values or
    // their varyings desync and the program fails to link.
    const ComboMap* promotedSets[] = {
	&this->m_promotedCombos,
	this->m_link != nullptr ? &this->m_link->getPromotedCombos () : nullptr,
    };
    for (const ComboMap* promoted : promotedSets) {
	if (promoted == nullptr) {
	    continue;
	}
	for (const auto& [name, value] : *promoted) {
	    std::string uppercase;
	    std::ranges::transform (name, std::back_inserter (uppercase), ::toupper);

	    if (!addedCombos.contains (uppercase)) {
		this->m_final += DEFINE_COMBO (uppercase, value);
		addedCombos.emplace (uppercase, true);
	    }
	}
    }

    for (const auto& [name, value] : this->m_overrideCombos) {
	std::string uppercase;
	std::ranges::transform (name, std::back_inserter (uppercase), ::toupper);

	if (!addedCombos.contains (uppercase)) {
	    this->m_final += DEFINE_COMBO (uppercase, value);
	    addedCombos.emplace (uppercase, true);
	}
    }

    // now add all the combos to the source
    for (const auto& [name, value] : this->m_combos) {
	std::string uppercase;
	std::ranges::transform (name, std::back_inserter (uppercase), ::toupper);

	if (!addedCombos.contains (uppercase)) {
	    this->m_final += DEFINE_COMBO (uppercase, value);
	    addedCombos.emplace (uppercase, true);
	}
    }

    for (const auto& [name, value] : this->m_discoveredCombos) {
	std::string uppercase;
	std::ranges::transform (name, std::back_inserter (uppercase), ::toupper);

	if (!addedCombos.contains (uppercase)) {
	    this->m_final += DEFINE_COMBO (uppercase, value);
	    addedCombos.emplace (uppercase, true);
	}
    }

    if (this->m_link != nullptr) {
	for (const auto& [name, value] : this->m_link->getCombos ()) {
	    std::string uppercase;
	    std::ranges::transform (name, std::back_inserter (uppercase), ::toupper);

	    if (!addedCombos.contains (uppercase)) {
		this->m_final += DEFINE_COMBO (uppercase, value);
		addedCombos.emplace (uppercase, true);
	    }
	}

	for (const auto& [name, value] : this->m_link->getDiscoveredCombos ()) {
	    std::string uppercase;
	    std::ranges::transform (name, std::back_inserter (uppercase), ::toupper);

	    if (!addedCombos.contains (uppercase)) {
		this->m_final += DEFINE_COMBO (uppercase, value);
		addedCombos.emplace (uppercase, true);
	    }
	}
    }

    // this should be the rest of the shader
    // all compat passes are pure functions of (unit type, preprocessed source,
    // linked preprocessed source); memoized like GLSLContext::toGlsl so stock
    // shaders shared between wallpapers and switch-backs skip the regex passes.
    // Guarded by a mutex so a future worker-thread caller stays safe.
    static std::mutex sCompatCacheMutex;
    static std::unordered_map<std::string, std::string> sCompatCache;
    static const std::string sNoLink;

    const std::string& linkedSource = this->m_link != nullptr ? this->m_link->m_preprocessed : sNoLink;
    std::string compatKey;
    compatKey.reserve (this->m_preprocessed.size () + linkedSource.size () + 2);
    compatKey.push_back (this->m_type == GLSLContext::UnitType_Vertex ? 'v' : 'f');
    compatKey.append (this->m_preprocessed);
    compatKey.push_back ('\x1F');
    compatKey.append (linkedSource);

    {
	const std::lock_guard<std::mutex> lock (sCompatCacheMutex);
	const auto it = sCompatCache.find (compatKey);
	if (it != sCompatCache.end ()) {
	    this->m_final += it->second;
	    return this->m_final;
	}
    }

    std::string compatResult =
	this->applyFragmentTexCoordCompatibility (
	    this->applyFragmentWritableVaryings (
		this->applyFloatTernaryCompatibility (
		    this->applyLinkedVaryingCompatibility (this->m_preprocessed))));
    this->m_final += compatResult;

    {
	const std::lock_guard<std::mutex> lock (sCompatCacheMutex);
	// content-keyed, so it stays valid across switches, but never evicts;
	// cap it so cycling through many wallpapers can't grow it unboundedly
	if (sCompatCache.size () >= 512)
	    sCompatCache.clear ();
	sCompatCache.emplace (compatKey, std::move (compatResult));
    }

    // the pass itself handles shader compilation, the unit doesn't have enough information for this step
    return this->m_final;
}

const std::vector<Variables::ShaderVariable*>& ShaderUnit::getParameters () const { return this->m_parameters; }
const TextureMap& ShaderUnit::getTextures () const { return this->m_defaultTextures; }
