#include "LocalStorage.h"

#include "WallpaperEngine/Data/Utils/ScopeGuard.h"

#include <nlohmann/json.hpp>

#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <sys/file.h>
#include <unistd.h>

namespace WallpaperEngine::Scripting {
namespace {
using JSON = nlohmann::json;
using Data::Utils::ScopeGuard;

std::filesystem::path defaultStateDirectory () {
    if (const char* state = std::getenv ("XDG_STATE_HOME"); state && state[0] == '/') {
	return state;
    }
    if (const char* home = std::getenv ("HOME"); home && home[0] == '/') {
	return std::filesystem::path (home) / ".local/state";
    }
    return {};
}

std::string projectFilename (const std::string& project) {
    // Unlike std::hash, this identity is stable across library/engine upgrades.
    uint64_t hash = 14695981039346656037ull;
    for (const unsigned char c : project) {
	hash = (hash ^ c) * 1099511628211ull;
    }
    std::ostringstream stream;
    stream << std::hex << std::setw (16) << std::setfill ('0') << hash;
    return stream.str () + ".json";
}

std::string jsString (JSContext* context, JSValueConst value) {
    size_t length = 0;
    const char* text = JS_ToCStringLen (context, &length, value);
    if (!text) {
	throw std::runtime_error ("Cannot read storage argument");
    }
    ScopeGuard release ([&] { JS_FreeCString (context, text); });
    return { text, length };
}

class StorageFile {
public:
    StorageFile (const std::filesystem::path& filename, const std::string& project, bool writing) :
	m_filename (filename) {
	if (filename.empty ()) {
	    throw std::runtime_error ("Neither XDG_STATE_HOME nor HOME provides a storage directory");
	}
	// Merely constructing a wallpaper or reading missing values must not create
	// a persistent directory. Writers take the same lock before loading the file.
	if (!writing && !std::filesystem::exists (filename)) {
	    m_data = empty (project);
	    return;
	}
	std::filesystem::create_directories (filename.parent_path ());
	m_lock = open ((filename.string () + ".lock").c_str (), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
	if (m_lock < 0) {
	    throw std::system_error (errno, std::generic_category (), "Open localStorage lock");
	}
	try {
	    int result;
	    do {
		result = flock (m_lock, writing ? LOCK_EX : LOCK_SH);
	    } while (result < 0 && errno == EINTR);
	    if (result < 0) {
		throw std::system_error (errno, std::generic_category (), "Lock localStorage");
	    }
	    if (!std::filesystem::exists (filename)) {
		m_data = empty (project);
	    } else {
		std::ifstream input (filename);
		input >> m_data;
		if (m_data.value ("version", 0) != 1 || m_data.value ("project", std::string {}) != project
		    || !m_data.at ("global").is_object () || !m_data.at ("screens").is_object ()) {
		    throw std::runtime_error ("Invalid localStorage file or project identity collision");
		}
	    }
	} catch (...) {
	    close (m_lock);
	    m_lock = -1;
	    throw;
	}
    }

    ~StorageFile () {
	if (m_lock >= 0) {
	    close (m_lock);
	}
    }

    JSON& bucket (bool global, const std::string& screen) {
	if (global) {
	    return m_data["global"];
	}
	auto& result = m_data["screens"][screen];
	if (result.is_null ()) {
	    result = JSON::object ();
	}
	if (!result.is_object ()) {
	    throw std::runtime_error ("Invalid localStorage screen data");
	}
	return result;
    }

    void save () const {
	// Keep the previous complete file until the replacement is closed. The
	// separate lock survives rename and prevents lost writes between instances.
	std::string temporary = m_filename.string () + ".XXXXXX";
	const int fd = mkstemp (temporary.data ());
	if (fd < 0) {
	    throw std::system_error (errno, std::generic_category (), "Create localStorage file");
	}
	ScopeGuard cleanup ([&] { unlink (temporary.c_str ()); });
	{
	    ScopeGuard closeFile ([&] { close (fd); });
	    const std::string serialized = m_data.dump ();
	    size_t written = 0;
	    while (written < serialized.size ()) {
		const ssize_t count = write (fd, serialized.data () + written, serialized.size () - written);
		if (count < 0 && errno == EINTR) {
		    continue;
		}
		if (count <= 0) {
		    throw std::system_error (errno, std::generic_category (), "Write localStorage file");
		}
		written += static_cast<size_t> (count);
	    }
	}
	std::filesystem::rename (temporary, m_filename);
    }

private:
    static JSON empty (const std::string& project) {
	return { { "version", 1 }, { "project", project }, { "global", JSON::object () },
		 { "screens", JSON::object () } };
    }

    std::filesystem::path m_filename;
    int m_lock = -1;
    JSON m_data;
};

enum Operation { Get, Set, Delete, Clear };

JSValue storageCall (JSContext* context, JSValueConst, int argc, JSValueConst* argv, int magic, JSValue* data) {
    try {
	const auto filename = jsString (context, data[0]);
	const auto project = jsString (context, data[1]);
	const auto screen = jsString (context, data[2]);
	const bool global = argc > 0 && JS_ToBool (context, argv[0]);
	if (magic != Clear && (argc < 2 || !JS_IsString (argv[1]))) {
	    return JS_ThrowTypeError (context, "localStorage key must be a string");
	}
	const std::string key = magic == Clear ? std::string {} : jsString (context, argv[1]);
	StorageFile file (filename, project, magic != Get);
	auto& bucket = file.bucket (global, screen);
	if (magic == Get) {
	    const auto found = bucket.find (key);
	    if (found == bucket.end ()) {
		return JS_UNDEFINED;
	    }
	    const auto& value = found->get_ref<const std::string&> ();
	    return JS_NewStringLen (context, value.data (), value.size ());
	}
	if (magic == Set) {
	    if (argc < 3 || !JS_IsString (argv[2])) {
		return JS_ThrowTypeError (context, "localStorage value must be serialized JSON");
	    }
	    const auto value = jsString (context, argv[2]);
	    if (bucket.contains (key) && bucket[key] == value) {
		return JS_UNDEFINED;
	    }
	    bucket[key] = value;
	} else if (magic == Delete) {
	    if (bucket.erase (key) == 0) {
		return JS_FALSE;
	    }
	} else {
	    if (bucket.empty ()) {
		return JS_UNDEFINED;
	    }
	    bucket.clear ();
	}
	file.save ();
	return magic == Delete ? JS_TRUE : JS_UNDEFINED;
    } catch (const std::exception& error) {
	return JS_ThrowInternalError (context, "localStorage: %s", error.what ());
    }
}
}

void installLocalStorage (
    JSContext* context, JSValueConst global, const std::string& project, const std::string& screen,
    const std::filesystem::path& stateDirectory
) {
    const auto directory = stateDirectory.empty () ? defaultStateDirectory () : stateDirectory;
    const auto filename = directory.empty () ? std::filesystem::path {}
	: directory / "linux-wallpaperengine/scenescript" / projectFilename (project);
    JSValue data[] = { JS_NewString (context, filename.c_str ()),
		       JS_NewStringLen (context, project.data (), project.size ()),
		       JS_NewStringLen (context, screen.data (), screen.size ()) };
    JSValue bridge = JS_NewObject (context);
    const char* names[] = { "get", "set", "delete", "clear" };
    for (int operation = Get; operation <= Clear; ++operation) {
	JS_SetPropertyStr (
	    context, bridge, names[operation],
	    JS_NewCFunctionData (context, storageCall, 3, operation, 3, data)
	);
    }
    for (auto value : data) {
	JS_FreeValue (context, value);
    }
    JS_SetPropertyStr (context, global, "__sceneLocalStorage", bridge);
}
}
