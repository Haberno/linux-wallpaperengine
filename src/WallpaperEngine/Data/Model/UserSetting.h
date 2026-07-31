#pragma once

#include <memory>
#include <optional>

#include "DynamicValue.h"
#include "PropertyAnimation.h"
#include "Types.h"

namespace WallpaperEngine::Data::Model {
struct UserSetting {
    /**
     * The value of this setting, can be a few different things:
     * - a value connected to the property
     * - a default value
     * - a static value
     */
    DynamicValueUniquePtr value;
    /** The property this setting takes the value from (if specified) */
    PropertySharedPtr property;
    /** Condition required for this setting, this should be possible to run in JS' V8 */
    std::optional<ConditionInfo> condition;
    /** Keyframed animation for this setting (e.g. an origin moving across the scene), if any */
    PropertyAnimationUniquePtr animation;
    /** TODO: Value might come from a script and not have conditions, implement this later */

    /**
     * Resolve the current scalar value, including an authored keyframe animation.
     *
     * Wallpaper Engine attaches animations to the property itself, so consumers
     * must sample the setting instead of reading DynamicValue directly.
     */
    [[nodiscard]] float evaluateFloat (float time) const {
	const float base = this->value->getFloat ();
	return this->animation != nullptr ? this->animation->evaluateFloat (base, time) : base;
    }

    /**
     * Resolve the current vector value, including absolute or relative keyframes.
     */
    [[nodiscard]] glm::vec3 evaluateVec3 (float time) const {
	const glm::vec3 base = this->value->getVec3 ();
	return this->animation != nullptr ? this->animation->evaluateVec3 (base, time) : base;
    }
};
} // namespace WallpaperEngine::Data::Model
