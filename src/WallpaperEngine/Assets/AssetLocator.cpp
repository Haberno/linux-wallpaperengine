#include "AssetLocator.h"

#include "AssetLoadException.h"

using namespace WallpaperEngine::Assets;

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

std::string AssetLocator::fragmentShader (const std::filesystem::path& filename) const {
    auto final = filename;

    final.replace_extension ("frag");

    return this->shader (final);
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
