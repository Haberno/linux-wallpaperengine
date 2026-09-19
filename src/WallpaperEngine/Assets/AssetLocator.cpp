#include "AssetLocator.h"

#include "AssetLoadException.h"

using namespace WallpaperEngine::Assets;

namespace {
bool normalizeCursorRipple (const std::filesystem::path& filename, std::string& source) {
    std::string from, to, outputFrom, outputTo;
    if (filename == "effects/cursorripple_apply_force.frag") {
	if (source.find ("float timeAmt = min(1.0 / 30.0, g_Frametime) / 0.02;") == std::string::npos) {
	    return false;
	}
	// Pointer displacement already contains dt. Normalize its extra time
	// factor to the stock shader's 30 Hz reference without changing the path.
	from = "float pointerMoveAmt = v_PointDelta.x;";
	to = "float pointerMoveAmt = v_PointDelta.x * max(1.0, 1.0 / (30.0 * max(g_Frametime, 0.0001)));";
	outputFrom = "gl_FragColor = albedo + colorAdd;";
	outputTo = "gl_FragColor = saturate(albedo + colorAdd);";
    } else if (filename == "effects/cursorripple_simulate_force.frag") {
	// The stock one-byte minimum decay otherwise drains the wave once per
	// rendered frame. Float feedback retains these smaller time-based steps.
	from = "float drop = max(1.001 / 255.0, decay / 255.0 * (g_Frametime / 0.02) * g_RippleDecay);";
	to = "float drop = max(1.001 / 255.0 * min(1.0, max(0.0, g_Frametime * 30.0)), "
	     "decay / 255.0 * (g_Frametime / 0.02) * g_RippleDecay);";
	outputFrom = "albedo = force;";
	outputTo = "albedo = saturate(force);";
    } else {
	return false;
    }
    // Leave customized equations alone. Clamp both writes to retain the original
    // normalized buffer range now that storage no longer provides that clamp.
    const auto position = source.find (from);
    if (position == std::string::npos || source.find (outputFrom) == std::string::npos) {
	return false;
    }
    source.replace (position, from.size (), to);
    source.replace (source.find (outputFrom), outputFrom.size (), outputTo);
    return true;
}
}

AssetLocator::AssetLocator (ContainerUniquePtr filesystem, std::vector<std::filesystem::path> textureFallbackRoots) :
    m_filesystem (std::move (filesystem)), m_textureFallbackRoots (std::move (textureFallbackRoots)) { }

const std::string& AssetLocator::identity () const { return this->m_filesystem->fingerprint (); }

std::string AssetLocator::shader (const std::filesystem::path& filename) const {
    try {
	std::filesystem::path shader = filename;

	// Workshop compatibility paths include an id, an asset directory, and a file.
	// Short or empty paths still go through the normal missing-asset error path.
	if (auto it = shader.begin (); it != shader.end () && *it++ == "workshop" && it != shader.end ()) {
	    const std::filesystem::path workshopId = *it++;

	    if (it != shader.end () && ++it != shader.end ()) {
		const std::filesystem::path& shaderfile = *it;

		try {
		    shader = std::filesystem::path ("zcompat") / "scene" / "shaders" / workshopId / shaderfile;
		    // replace the old path with the new one
		    std::string contents = this->m_filesystem->readString (shader);

		    sLog.out ("Replaced ", filename, " with compat ", shader);

		    return contents;
		} catch (std::filesystem::filesystem_error&) {
		    // these exceptions can be ignored because the replacement file might not exist
		}
	    }
	}

	return this->m_filesystem->readString ("shaders" / filename);
    } catch (std::filesystem::filesystem_error& base) {
	throw AssetLoadException (base);
    }
}

std::string AssetLocator::fragmentShader (const std::filesystem::path& filename, bool* floatFeedback) const {
    auto final = filename;

    final.replace_extension ("frag");

    auto source = this->shader (final);
    const bool normalized = normalizeCursorRipple (final, source);
    if (floatFeedback != nullptr) {
	*floatFeedback = normalized;
    }
    return source;
}

std::string AssetLocator::vertexShader (const std::filesystem::path& filename) const {
    auto final = filename;

    final.replace_extension ("vert");

    return this->shader (final);
}

std::string AssetLocator::includeShader (const std::filesystem::path& filename) const {
    auto final = filename;

    final.replace_extension ("h");

    return this->shader (final);
}

std::string AssetLocator::readString (const std::filesystem::path& filename) const {
    try {
	return this->m_filesystem->readString (filename);
    } catch (std::filesystem::filesystem_error& base) {
	throw AssetLoadException (base);
    }
}

ReadStreamSharedPtr AssetLocator::texture (const std::filesystem::path& filename) const {
    const auto final = std::filesystem::path ("materials") / filename.string ().append (".tex");

    try {
	return this->m_filesystem->read (final);
    } catch (std::filesystem::filesystem_error& base) {
	// These are explicit stock texture roots. Only .tex requests may reach them;
	// a preview's scene.json, project.json and materials remain invisible.
	const auto relative = std::filesystem::path (filename.string () + ".tex").lexically_normal ();
	if (!relative.is_absolute () && !relative.empty () && *relative.begin () != "..") {
	    for (const auto& root : this->m_textureFallbackRoots) {
		try {
		    return this->m_filesystem->read (root / relative);
		} catch (const std::filesystem::filesystem_error&) { }
	    }
	}
	throw AssetLoadException (base);
    }
}

ReadStreamSharedPtr AssetLocator::read (const std::filesystem::path& path) const {
    try {
	return this->m_filesystem->read (path);
    } catch (std::filesystem::filesystem_error& base) {
	throw AssetLoadException (base);
    }
}

std::filesystem::path AssetLocator::physicalPath (const std::filesystem::path& path) const {
    try {
	return this->m_filesystem->physicalPath (path);
    } catch (std::filesystem::filesystem_error& base) {
	throw AssetLoadException (base);
    }
}
