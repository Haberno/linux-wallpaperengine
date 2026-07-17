#pragma once

#include <optional>
#include <string>

#include "WallpaperEngine/Render/Helpers/ContextAware.h"

#include "WallpaperEngine/Render/Wallpapers/CScene.h"

namespace WallpaperEngine::Render::Wallpapers {
class CScene;
}

namespace WallpaperEngine::Render {
class CObject : public Helpers::ContextAware, public TypeCaster {
public:
    CObject (Wallpapers::CScene& scene, const Object& object);
    virtual ~CObject () override = default;

    virtual void setup ();
    virtual void render ();

    [[nodiscard]] Wallpapers::CScene& getScene () const;
    [[nodiscard]] const AssetLocator& getAssetLocator () const;
    [[nodiscard]] int getId () const;
    [[nodiscard]] const Object& getObject () const;
    /** Model-local animated attachment transform for a named MDAT attachment. */
    [[nodiscard]] virtual std::optional<glm::mat4> getAttachmentTransform (const std::string& name) const;
    /** Effective parallax depth: the root-most layer controls its entire subtree; an
     *  unauthored root defaults to 1 (Wallpaper Engine's Layer constructor default). */
    [[nodiscard]] glm::vec2 resolveParallaxDepth () const;

    /** true when no ancestor in the parent chain is hidden — a hidden container hides its
     *  whole subtree in Wallpaper Engine, and children often carry no visible of their own */
    [[nodiscard]] bool isVisibleThroughParents () const;

    /** World transform for 3D scenes, including animated MDAT attachment edges,
     *  composed through the parent chain. 2D images keep their own decomposed path. */
    [[nodiscard]] glm::mat4 resolveWorldMatrix () const;

    /** Classify a 3D model's authored material passes for render ordering. */
    [[nodiscard]] RenderSortClass getRenderSortClass () const;

private:
    Wallpapers::CScene& m_scene;
    const Object& m_object;
};
} // namespace WallpaperEngine::Render
