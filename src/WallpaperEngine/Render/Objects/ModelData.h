#pragma once

#include "WallpaperEngine/Data/Model/MdlMesh.h"
#include "WallpaperEngine/Scripting/ModelDataShape.h"

#include <GL/glew.h>
#include <memory>
#include <unordered_map>

namespace WallpaperEngine::Render::Objects {
/** Scene-owned geometry created by SceneScript; layers share the uploaded buffers. */
class ModelData {
public:
    struct Shape {
	Scripting::ModelDataShape input;
	std::string geometryMaterial;
	Data::Model::MdlMesh layout;
	uint32_t colorOffset = Data::Model::MdlMesh::AttributeAbsent;
	GLuint vertexBuffer = GL_NONE;
	GLuint indexBuffer = GL_NONE;
	GLsizei drawCount = 0;
    };
    explicit ModelData (std::vector<Scripting::ModelDataShape> input);
    ~ModelData ();
    ModelData (const ModelData&) = delete;
    ModelData& operator= (const ModelData&) = delete;
    void upload ();
    const char* applyData (const std::vector<Scripting::ModelDataShape>& updates);
    const char* replaceData (std::vector<Scripting::ModelDataShape> replacements);
    std::vector<Shape> shapes;
    Data::Model::MdlMesh mesh;
    uint64_t materialRevision = 0;

private:
    static void updateLayout (Shape& shape);
};

/** A script lease plus weak lookup; live layers retain the resource after release. */
class ModelDataStore {
public:
    uint32_t insert (std::unique_ptr<ModelData> input) {
	if (m_nextToken == UINT32_MAX) {
	    return 0;
	}
	const auto token = ++m_nextToken;
	const std::weak_ptr<State> owner = m_state;
	std::shared_ptr<ModelData> resource (input.release (), [owner, token] (ModelData* data) {
	    if (auto state = owner.lock ()) {
		state->entries.erase (token);
	    }
	    delete data;
	});
	m_state->entries.emplace (token, Entry { resource, std::move (resource) });
	return token;
    }
    std::shared_ptr<ModelData> find (uint32_t token) const {
	const auto found = m_state->entries.find (token);
	return found == m_state->entries.end () ? nullptr : found->second.resource.lock ();
    }
    void release (uint32_t token) {
	const auto found = m_state->entries.find (token);
	if (found == m_state->entries.end ()) {
	    return;
	}
	// Destruction may erase the entry, so move its last strong reference out first.
	auto lease = std::move (found->second.scriptLease);
    }

private:
    struct Entry {
	std::weak_ptr<ModelData> resource;
	std::shared_ptr<ModelData> scriptLease;
    };
    struct State {
	std::unordered_map<uint32_t, Entry> entries;
    };
    uint32_t m_nextToken = 0;
    std::shared_ptr<State> m_state = std::make_shared<State> ();
};
} // namespace WallpaperEngine::Render::Objects
