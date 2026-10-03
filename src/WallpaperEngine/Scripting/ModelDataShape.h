#pragma once

#include "quickjs.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace WallpaperEngine::Scripting {
/** Owned shape input; no pointer into JavaScript storage survives parsing. */
struct ModelDataShape {
    std::vector<float> vertices;
    std::vector<uint8_t> indices;
    uint32_t vertexFormat = 0;
    uint32_t strideBytes = 0;
    uint8_t indexWidth = 0;
    std::string material;
    size_t vertexFloatCount = 0;
    size_t indexCount = 0;
    bool hasVertices = false;
    bool deleteShape = false;
    bool deleteIndices = false;
    bool vertexDynamic = false;
    bool indexDynamic = false;
};

namespace ModelDataInput {
    class Value {
    public:
	Value (JSContext* context, JSValue input) : ctx (context), value (input) { }
	~Value () { JS_FreeValue (ctx, value); }
	Value (const Value&) = delete;
	Value& operator= (const Value&) = delete;
	JSContext* ctx;
	JSValue value;
    };

    template <typename Element> bool copyView (JSContext* ctx, JSValueConst view, std::vector<Element>& output, size_t requiredBytes = 0, size_t* viewElements = nullptr) {
	size_t offset = 0, length = 0, width = 0;
	Value buffer (ctx, JS_GetTypedArrayBuffer (ctx, view, &offset, &length, &width));
	if (JS_IsException (buffer.value)) {
	    return false;
	}
	// Allocate before acquiring the pointer: JSC permits no intervening API calls
	// between pointer acquisition and copying the borrowed backing storage.
	const size_t copiedBytes = std::max (length, requiredBytes);
	output.resize (copiedBytes / sizeof (Element));
	size_t backingLength = 0;
	const auto* bytes = JS_GetArrayBuffer (ctx, &backingLength, buffer.value);
	if ((length != 0 && bytes == nullptr) || offset > backingLength || length > backingLength - offset) {
	    if (bytes != nullptr) {
		JS_ThrowTypeError (ctx, "Invalid typed array view");
	    }
	    return false;
	}
	if (copiedBytes > backingLength - offset) {
	    JS_ThrowSyntaxError (ctx, "Model update backing buffer is smaller than the original allocation");
	    return false;
	}
	if (copiedBytes != 0) {
	    std::memcpy (output.data (), bytes + offset, copiedBytes);
	}
	if (viewElements != nullptr) {
	    *viewElements = length / width;
	}
	return true;
    }

    /** Parse one native shape. Returns false with a pending JS exception. */
    inline bool readShape (JSContext* ctx, JSValueConst input, ModelDataShape& result, bool creation = true, size_t vertexCapacity = 0, size_t indexCapacity = 0) {
	ModelDataShape parsed;
	if (!creation && !JS_IsObject (input)) {
	    parsed.deleteShape = JS_IsNull (input);
	    result = std::move (parsed);
	    return true;
	}
	Value vertices (ctx, JS_GetPropertyStr (ctx, input, "vertexBuffer"));
	if (JS_IsException (vertices.value)) {
	    return false;
	}
	if (JS_GetTypedArrayType (vertices.value) == JS_TYPED_ARRAY_FLOAT32) {
	    parsed.hasVertices = true;
	    if (!copyView (ctx, vertices.value, parsed.vertices, vertexCapacity, &parsed.vertexFloatCount)) {
		return false;
	    }
	} else if (creation) {
	    JS_ThrowSyntaxError (ctx, "Vertex buffer missing.");
	    return false;
	}

	Value indices (ctx, JS_GetPropertyStr (ctx, input, "indexBuffer"));
	if (JS_IsException (indices.value)) {
	    return false;
	}
	parsed.deleteIndices = !creation && JS_IsNull (indices.value);
	const int indexType = JS_GetTypedArrayType (indices.value);
	if (indexType == JS_TYPED_ARRAY_UINT16 || indexType == JS_TYPED_ARRAY_UINT32) {
	    parsed.indexWidth = indexType == JS_TYPED_ARRAY_UINT16 ? 2 : 4;
	    if (!copyView (ctx, indices.value, parsed.indices, indexCapacity, &parsed.indexCount)) {
		return false;
	    }
	}

	Value format (ctx, JS_GetPropertyStr (ctx, input, "vertexFormat"));
	if (JS_IsException (format.value)) {
	    return false;
	}
	if (JS_IsArray (format.value)) {
	    Value countValue (ctx, JS_GetPropertyStr (ctx, format.value, "length"));
	    uint32_t count = 0;
	    if (JS_IsException (countValue.value) || JS_ToUint32 (ctx, &count, countValue.value) < 0) {
		return false;
	    }
	    for (uint32_t index = 0; index < count; ++index) {
		Value attribute (ctx, JS_GetPropertyUint32 (ctx, format.value, index));
		if (JS_IsException (attribute.value)) {
		    return false;
		}
		if (!JS_IsString (attribute.value)) {
		    break;
		}
		const char* name = JS_ToCString (ctx, attribute.value);
		if (name == nullptr) {
		    return false;
		}
		if (std::strcmp (name, "position") == 0) {
		    parsed.vertexFormat |= 1;
		} else if (std::strcmp (name, "normal") == 0) {
		    parsed.vertexFormat |= 2;
		} else if (std::strcmp (name, "tangentSigned") == 0) {
		    parsed.vertexFormat |= 4;
		} else if (std::strcmp (name, "uv") == 0) {
		    parsed.vertexFormat |= 8;
		} else if (std::strcmp (name, "color") == 0) {
		    parsed.vertexFormat |= 0x8000;
		}
		JS_FreeCString (ctx, name);
	    }
	}
	if (creation && parsed.vertexFormat == 0) {
	    JS_ThrowSyntaxError (ctx, "Vertex format missing.");
	    return false;
	}
	for (const auto [flag, components] : std::array<std::pair<uint32_t, uint32_t>, 5> {
		 { { 1, 3 }, { 2, 3 }, { 4, 4 }, { 8, 2 }, { 0x8000, 4 } } }) {
	    if ((parsed.vertexFormat & flag) != 0) {
		parsed.strideBytes += components * sizeof (float);
	    }
	}
	Value material (ctx, JS_GetPropertyStr (ctx, input, "material"));
	if (JS_IsException (material.value)) {
	    return false;
	}
	if (JS_IsString (material.value)) {
	    const char* name = JS_ToCString (ctx, material.value);
	    if (name == nullptr) {
		return false;
	    }
	    parsed.material = name;
	    JS_FreeCString (ctx, name);
	} else if (creation) {
	    JS_ThrowSyntaxError (ctx, "Material missing.");
	    return false;
	}

	Value vertexDynamic (ctx, JS_GetPropertyStr (ctx, input, "isVertexBufferDynamic"));
	if (JS_IsException (vertexDynamic.value)) {
	    return false;
	}
	Value indexDynamic (ctx, JS_GetPropertyStr (ctx, input, "isIndexBufferDynamic"));
	if (JS_IsException (indexDynamic.value)) {
	    return false;
	}
	parsed.vertexDynamic = JS_IsBool (vertexDynamic.value) && JS_ToBool (ctx, vertexDynamic.value) != 0;
	parsed.indexDynamic = JS_IsBool (indexDynamic.value) && JS_ToBool (ctx, indexDynamic.value) != 0;
	if (creation && (parsed.vertices.size () * sizeof (float)) % parsed.strideBytes != 0) {
	    JS_ThrowSyntaxError (ctx, "Inconsistent vertex buffer size");
	    return false;
	}
	result = std::move (parsed);
	return true;
    }
} // namespace ModelDataInput
} // namespace WallpaperEngine::Scripting
