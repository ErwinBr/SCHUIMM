#include "Room/ObjectAnimation.h"

#include "Core/Common/StringBuilder.h"

namespace schuimm::room {

Result<std::shared_ptr<const ObjectAnimationSet>> resolveObjectAnimations(const ObjectAnimationConfig& config,
                                                                         const AnimationLoader& load)
{
    auto set = std::make_shared<ObjectAnimationSet>();
    set->defaultAnimation = config.defaultAnimation;
    std::map<std::string, std::shared_ptr<const animation::AnimationResource>, std::less<>> loaded;
    for (const auto& [name, path] : config.resources) {
        auto resource = load(path);
        if (!resource) {
            return makeError(resource.error().code, concat("animation_resources.", name, ": ", resource.error().message));
        }
        loaded.emplace(name, *resource);
        set->resources.emplace_back(path, std::move(*resource));
    }
    for (const auto& [name, mapping] : config.animations) {
        const auto resource = loaded.find(mapping.resource);
        if (resource == loaded.end()) {
            return makeError(ErrorCode::InvalidData, concat("animations.", name, ".resource: no animation resource '",
                                                            mapping.resource, "'"));
        }
        auto clip = animation::AnimationClipRef::find(resource->second, mapping.clip);
        if (!clip) {
            return makeError(ErrorCode::InvalidData, concat("animations.", name, ".clip: no clip '", mapping.clip, "' in '",
                                                            config.resources.at(mapping.resource).view(), "'"));
        }
        set->clips.emplace(name, std::move(*clip));
    }
    if (set->find(set->defaultAnimation) == nullptr) {
        return makeError(ErrorCode::InvalidData, concat("default_animation: no animation '", set->defaultAnimation, "'"));
    }
    return std::shared_ptr<const ObjectAnimationSet>(std::move(set));
}

} // namespace schuimm::room
