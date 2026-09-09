#include "ShaderProgramCache.h"

#include <algorithm>

#include "WallpaperEngine/Debug/RenderHealth.h"
#include "WallpaperEngine/Logging/Log.h"

using namespace WallpaperEngine::Render::Shaders;

namespace {
// Geometry callbacks cache attribute locations, and CPass caches uniform
// locations. A shared executable must preserve both, including array extents.
std::string programInterface (GLuint program) {
    std::string result;
    for (const bool uniforms : { false, true }) {
	GLint count = 0;
	GLint maxLength = 0;
	glGetProgramiv (program, uniforms ? GL_ACTIVE_UNIFORMS : GL_ACTIVE_ATTRIBUTES, &count);
	glGetProgramiv (program, uniforms ? GL_ACTIVE_UNIFORM_MAX_LENGTH : GL_ACTIVE_ATTRIBUTE_MAX_LENGTH, &maxLength);
	std::vector<char> name (std::max (maxLength, 1));
	std::vector<std::string> entries;
	for (GLint index = 0; index < count; ++index) {
	    GLsizei length = 0;
	    GLint size = 0;
	    GLenum type = 0;
	    if (uniforms) {
		glGetActiveUniform (program, index, name.size (), &length, &size, &type, name.data ());
	    } else {
		glGetActiveAttrib (program, index, name.size (), &length, &size, &type, name.data ());
	    }
	    const GLint location
		= uniforms ? glGetUniformLocation (program, name.data ()) : glGetAttribLocation (program, name.data ());
	    entries.push_back (
		std::string (name.data (), length) + ':' + std::to_string (location) + ':' + std::to_string (type) + ':'
		+ std::to_string (size) + ';'
	    );
	}
	std::sort (entries.begin (), entries.end ());
	result += '|';
	for (const auto& entry : entries) {
	    result += entry;
	}
    }
    return result;
}

GLuint compileStage (const std::string& source, GLenum type) {
    const GLuint shader = glCreateShader (type);
    const char* text = source.c_str ();
    glShaderSource (shader, 1, &text, nullptr);
    glCompileShader (shader);

    GLint compiled = GL_FALSE;
    GLint length = 0;
    glGetShaderiv (shader, GL_COMPILE_STATUS, &compiled);
    glGetShaderiv (shader, GL_INFO_LOG_LENGTH, &length);
    std::string log (std::max (length, 1), '\0');
    if (length > 0) {
	GLsizei written = 0;
	glGetShaderInfoLog (shader, length, &written, log.data ());
	log.resize (written);
    }
    if (compiled != GL_TRUE) {
	glDeleteShader (shader);
	sLog.exception ("Cannot compile shader: ", log, "\nCompiled source code:\n", source);
    }
    if (length > 1) {
	sLog.error (log, "\nCompiled source code:\n", source);
    }
    return shader;
}
} // namespace

GLuint ShaderProgramCache::createProgram (
    const std::string& vertex, const std::string& fragment, std::shared_ptr<SharingGroup>* group
) {
    if (group) {
	group->reset ();
    }
    // All authored defines, generated lighting, bone counts and source edits must
    // participate in the key. Length-prefixing keeps the two stages unambiguous.
    const std::string key = std::to_string (vertex.size ()) + ':' + vertex + fragment;
    const bool supportsBinaries = GLEW_VERSION_4_1 || GLEW_ARB_get_program_binary;
    GLuint program = glCreateProgram ();
    if (supportsBinaries) {
	if (const auto found = m_entries.find (key); found != m_entries.end ()) {
	    const auto& entry = found->second;
	    glProgramBinary (program, entry.format, entry.binary.data (), static_cast<GLsizei> (entry.binary.size ()));
	    GLint linked = GL_FALSE;
	    glGetProgramiv (program, GL_LINK_STATUS, &linked);
	    if (linked == GL_TRUE) {
		found->second.lastUsed = ++m_useCounter;
		if (group) {
		    *group = entry.group;
		}
		Debug::RenderHealth::count ("shader.program_cache_hit");
		return program;
	    }
	    // Drivers may reject their own binaries. Discard this entry and compile
	    // normally into a fresh program, without making the material disappear.
	    m_bytes -= found->first.size () + entry.binary.size ();
	    m_entries.erase (found);
	    glDeleteProgram (program);
	    program = glCreateProgram ();
	    Debug::RenderHealth::count ("shader.program_cache_rejected");
	}
    }

    Debug::RenderHealth::count ("shader.program_cache_miss");
    GLuint vertexShader = GL_NONE;
    GLuint fragmentShader = GL_NONE;
    try {
	vertexShader = compileStage (vertex, GL_VERTEX_SHADER);
	fragmentShader = compileStage (fragment, GL_FRAGMENT_SHADER);
	if (supportsBinaries && m_budgetBytes > 0) {
	    glProgramParameteri (program, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);
	}
	glAttachShader (program, vertexShader);
	glAttachShader (program, fragmentShader);
	glLinkProgram (program);
	GLint linked = GL_FALSE;
	GLint length = 0;
	glGetProgramiv (program, GL_LINK_STATUS, &linked);
	glGetProgramiv (program, GL_INFO_LOG_LENGTH, &length);
	std::string log (std::max (length, 1), '\0');
	if (length > 0) {
	    GLsizei written = 0;
	    glGetProgramInfoLog (program, length, &written, log.data ());
	    log.resize (written);
	}
	if (linked != GL_TRUE) {
	    sLog.exception ("Cannot link shader program: ", log);
	}
	if (length > 1) {
	    sLog.error (log);
	}
    } catch (...) {
	if (vertexShader != GL_NONE) {
	    glDeleteShader (vertexShader);
	}
	if (fragmentShader != GL_NONE) {
	    glDeleteShader (fragmentShader);
	}
	glDeleteProgram (program);
	throw;
    }
    glDetachShader (program, vertexShader);
    glDetachShader (program, fragmentShader);
    glDeleteShader (vertexShader);
    glDeleteShader (fragmentShader);

    if (supportsBinaries && m_budgetBytes > key.size ()) {
	GLint length = 0;
	glGetProgramiv (program, GL_PROGRAM_BINARY_LENGTH, &length);
	if (length > 0 && static_cast<size_t> (length) <= m_budgetBytes - key.size ()) {
	    Entry entry { .format = 0, .binary = std::vector<char> (length), .lastUsed = ++m_useCounter };
	    GLsizei written = 0;
	    glGetProgramBinary (program, length, &written, &entry.format, entry.binary.data ());
	    if (written > 0) {
		entry.binary.resize (written);
		const size_t bytes = key.size () + entry.binary.size ();
		while (m_bytes + bytes > m_budgetBytes && !m_entries.empty ()) {
		    const auto oldest
			= std::min_element (m_entries.begin (), m_entries.end (), [] (const auto& a, const auto& b) {
			      return a.second.lastUsed < b.second.lastUsed;
			  });
		    m_bytes -= oldest->first.size () + oldest->second.binary.size ();
		    m_entries.erase (oldest);
		}
		if (group) {
		    *group = entry.group;
		}
		m_entries.emplace (key, std::move (entry));
		m_bytes += bytes;
	    }
	}
    }
    return program;
}

ShaderProgramCache::SharedProgram::~SharedProgram () { glDeleteProgram (id); }

std::shared_ptr<const ShaderProgramCache::SharedProgram> ShaderProgramCache::shareProgram (
    GLuint privateProgram, const std::shared_ptr<SharingGroup>& group, const std::string& layout
) {
    if (!group) {
	return nullptr;
    }
    std::erase_if (group->programs, [] (const auto& item) { return item.second.expired (); });
    const std::string interface = programInterface (privateProgram);
    const std::string key = std::to_string (layout.size ()) + ':' + layout + interface;
    if (const auto found = group->programs.find (key); found != group->programs.end ()) {
	if (auto program = found->second.lock ()) {
	    Debug::RenderHealth::count ("shader.live_program_hit");
	    return program;
	}
    }

    // Clone into a separate object: callers retain their private executable for
    // uniform registration and can leave sharing if their write layout changes.
    GLint length = 0;
    glGetProgramiv (privateProgram, GL_PROGRAM_BINARY_LENGTH, &length);
    if (length <= 0) {
	return nullptr;
    }
    std::vector<char> binary (length);
    GLenum format = 0;
    GLsizei written = 0;
    glGetProgramBinary (privateProgram, length, &written, &format, binary.data ());
    if (written <= 0) {
	return nullptr;
    }
    auto program = std::make_shared<SharedProgram> (glCreateProgram ());
    glProgramBinary (program->id, format, binary.data (), written);
    GLint linked = GL_FALSE;
    glGetProgramiv (program->id, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE || programInterface (program->id) != interface) {
	Debug::RenderHealth::count ("shader.live_program_rejected");
	return nullptr;
    }
    group->programs.emplace (key, program);
    Debug::RenderHealth::count ("shader.live_program_created");
    return program;
}
