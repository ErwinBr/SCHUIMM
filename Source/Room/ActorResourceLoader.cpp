#include "Room/ActorResourceLoader.h"

#include "Core/Common/StringBuilder.h"
#include "Resource/Json/JsonDecoder.h"

#include <string>
#include <utility>

namespace schuimm::room {

namespace {

using json::element;
using json::Json;
using json::member;

constexpr std::uint64_t maxActorFileBytes = 16u << 20;

class Decoder : public json::Decoder {
public:
    Decoder(std::string_view source, const AnimationResolver& animations)
        : json::Decoder(source)
        , animations_(animations)
    {
    }

    Result<ActorResource> decodeRoot(const Json& root) const
    {
        int version = 0;
        SCHUIMM_JSON_TRY(readVersion(root, "actor resource", 1, actorResourceVersion, version));
        SCHUIMM_JSON_TRY(onlyKnown(root, "", {"version", "id", "script", "animation_resources", "animations",
                                              "default_animation", "default_facing", "default_scale",
                                              "perspective_scale", "movement"}));
        ActorResource actor;
        const Json* value = nullptr;
        SCHUIMM_JSON_TRY(required(root, "", "id", value));
        SCHUIMM_JSON_TRY(readIdentifier(*value, "id", actor.id));

        if (const Json* script = optional(root, "script")) {
            SCHUIMM_JSON_TRY(readAssetPath(*script, "script", actor.script));
            if (actor.script->extension() != ".sch") {
                return fail("script", concat("must name a script source (.sch), found '", actor.script->view(), "'"));
            }
        }

        SCHUIMM_JSON_TRY(required(root, "", "animation_resources", value));
        SCHUIMM_JSON_TRY(readAnimationResources(*value, "animation_resources", actor));
        SCHUIMM_JSON_TRY(required(root, "", "animations", value));
        SCHUIMM_JSON_TRY(readAnimations(*value, "animations", actor));

        SCHUIMM_JSON_TRY(required(root, "", "default_animation", value));
        SCHUIMM_JSON_TRY(readIdentifier(*value, "default_animation", actor.defaultAnimation));
        if (!actor.hasAnimation(actor.defaultAnimation)) {
            return fail("default_animation", concat("no animation '", actor.defaultAnimation, "'"));
        }
        SCHUIMM_JSON_TRY(required(root, "", "default_facing", value));
        SCHUIMM_JSON_TRY(readFacing(*value, "default_facing", actor.defaultFacing));
        if (const Json* scale = optional(root, "default_scale")) {
            SCHUIMM_JSON_TRY(readFloatInRange(*scale, "default_scale", minActorScale, maxActorScale, actor.defaultScale));
        }
        if (const Json* perspective = optional(root, "perspective_scale")) {
            SCHUIMM_JSON_TRY(readBool(*perspective, "perspective_scale", actor.perspectiveScale));
        }
        if (const Json* movement = optional(root, "movement")) {
            ActorMovementConfig config;
            SCHUIMM_JSON_TRY(readMovement(*movement, "movement", actor, config));
            actor.movement = std::move(config);
        }
        if (auto resolved = actor.resolve(actor.defaultAnimation, actor.defaultFacing); !resolved) {
            return fail("default_facing", concat("the default animation does not resolve: ", resolved.error().message));
        }
        return actor;
    }

    Status readFacing(const Json& value, std::string_view path, Facing& out) const
    {
        std::string name;
        SCHUIMM_JSON_TRY(readString(value, path, name));
        const auto facing = parseFacing(name);
        if (!facing) {
            return fail(path, concat("unknown facing '", name,
                                     "' (north, north_east, east, south_east, south, south_west, west, north_west)"));
        }
        out = *facing;
        return {};
    }

private:
    Status readMovement(const Json& value, const std::string& path, const ActorResource& actor,
                        ActorMovementConfig& out) const
    {
        SCHUIMM_JSON_TRY(readJsonObject(value, path));
        SCHUIMM_JSON_TRY(onlyKnown(value, path, {"speed", "walk_animation", "idle_animation", "perspective_speed"}));
        const Json* field = nullptr;
        SCHUIMM_JSON_TRY(required(value, path, "speed", field));
        SCHUIMM_JSON_TRY(readFloatInRange(*field, member(path, "speed"), minWalkSpeed, maxWalkSpeed, out.speed));
        const auto readAnimationName = [&](const char* key, std::string& name) -> Status {
            const Json* animation = nullptr;
            SCHUIMM_JSON_TRY(required(value, path, key, animation));
            SCHUIMM_JSON_TRY(readIdentifier(*animation, member(path, key), name));
            if (!actor.hasAnimation(name)) return fail(member(path, key), concat("no animation '", name, "'"));
            return {};
        };
        SCHUIMM_JSON_TRY(readAnimationName("walk_animation", out.walkAnimation));
        SCHUIMM_JSON_TRY(readAnimationName("idle_animation", out.idleAnimation));
        if (const Json* perspective = optional(value, "perspective_speed")) {
            SCHUIMM_JSON_TRY(readBool(*perspective, member(path, "perspective_speed"), out.perspectiveSpeed));
        }
        return {};
    }

    Status readAnimationResources(const Json& value, const std::string& path, ActorResource& actor) const
    {
        SCHUIMM_JSON_TRY(readJsonObject(value, path));
        if (value.empty()) return fail(path, "an actor needs at least one animation resource");
        for (const auto& item : value.items()) {
            const std::string itemPath = member(path, item.key());
            SCHUIMM_JSON_TRY(checkName(item.key(), itemPath, "animation resource"));
            std::optional<AssetPath> asset;
            SCHUIMM_JSON_TRY(readAssetPath(item.value(), itemPath, asset));
            auto resource = animations_(*asset);
            if (!resource) {
                return makeError(resource.error().code, concat(source(), ": ", itemPath, ": ", resource.error().message));
            }
            actor.animationResources.emplace(item.key(), ActorAnimationResource{std::move(*asset), std::move(*resource)});
        }
        return {};
    }

    Status readAnimations(const Json& value, const std::string& path, ActorResource& actor) const
    {
        SCHUIMM_JSON_TRY(readJsonObject(value, path));
        if (value.empty()) return fail(path, "an actor needs at least one animation");
        for (const auto& item : value.items()) {
            const std::string animationPath = member(path, item.key());
            SCHUIMM_JSON_TRY(checkName(item.key(), animationPath, "animation"));
            ActorAnimation animation;
            SCHUIMM_JSON_TRY(readAnimation(item.value(), animationPath, actor, animation));
            actor.animations.emplace(item.key(), std::move(animation));
        }
        return {};
    }

    Status readAnimation(const Json& value, const std::string& path, const ActorResource& actor,
                         ActorAnimation& out) const
    {
        SCHUIMM_JSON_TRY(readJsonObject(value, path));
        if (const Json* any = optional(value, "any")) {
            for (const auto& item : value.items()) {
                if (item.key() != "any") {
                    return fail(member(path, item.key()), "not allowed in an animation that uses \"any\"");
                }
            }
            ActorClipMapping mapping;
            SCHUIMM_JSON_TRY(readMapping(*any, member(path, "any"), actor, mapping));
            out.any = std::move(mapping);
            return {};
        }
        for (const auto& item : value.items()) {
            if (item.key() == "fallback" || item.key() == "any") continue; // "any" here is null: omitted.
            const std::string itemPath = member(path, item.key());
            const auto facing = parseFacing(item.key());
            if (!facing) {
                return fail(itemPath, "unknown property (expected a facing name, \"any\" or \"fallback\")");
            }
            if (item.value().is_null()) continue; // Optional properties may be null.
            ActorClipMapping mapping;
            SCHUIMM_JSON_TRY(readMapping(item.value(), itemPath, actor, mapping));
            out.directions[facingIndex(*facing)] = std::move(mapping);
        }
        bool anyDirection = false;
        for (const auto& direction : out.directions) anyDirection = anyDirection || direction.has_value();
        if (!anyDirection) {
            return fail(path, "an animation needs a clip for at least one facing, or \"any\"");
        }
        if (const Json* fallback = optional(value, "fallback")) {
            const std::string fallbackPath = member(path, "fallback");
            Facing facing = Facing::South;
            SCHUIMM_JSON_TRY(readFacing(*fallback, fallbackPath, facing));
            if (!out.directions[facingIndex(facing)]) {
                return fail(fallbackPath, concat("facing '", facingName(facing), "' has no clip in this animation"));
            }
            out.fallback = facing;
        }
        return {};
    }

    Status readMapping(const Json& value, const std::string& path, const ActorResource& actor,
                       ActorClipMapping& out) const
    {
        SCHUIMM_JSON_TRY(readJsonObject(value, path));
        SCHUIMM_JSON_TRY(onlyKnown(value, path, {"resource", "clip", "flip_x"}));
        const Json* field = nullptr;
        SCHUIMM_JSON_TRY(required(value, path, "resource", field));
        const std::string resourcePath = member(path, "resource");
        SCHUIMM_JSON_TRY(readString(*field, resourcePath, out.resource));
        const auto resource = actor.animationResources.find(out.resource);
        if (resource == actor.animationResources.end()) {
            return fail(resourcePath, concat("no animation resource '", out.resource, "' in animation_resources"));
        }
        SCHUIMM_JSON_TRY(required(value, path, "clip", field));
        const std::string clipPath = member(path, "clip");
        SCHUIMM_JSON_TRY(readString(*field, clipPath, out.clipName));
        auto clip = animation::AnimationClipRef::find(resource->second.resource, out.clipName);
        if (!clip) {
            return fail(clipPath, concat("animation resource '", resource->second.path.view(), "' has no clip '",
                                         out.clipName, "'"));
        }
        out.clip = std::move(*clip);
        if (const Json* flip = optional(value, "flip_x")) {
            SCHUIMM_JSON_TRY(readBool(*flip, member(path, "flip_x"), out.flipX));
        }
        return {};
    }

    Status checkName(const std::string& name, const std::string& path, std::string_view what) const
    {
        if (!json::isValidIdentifier(name)) {
            return fail(path, concat("invalid ", what, " name '", name,
                                     "' (letters, digits and '_' only, not starting with a digit)"));
        }
        return {};
    }

    const AnimationResolver& animations_;
};

} // namespace

Result<ActorResource> decodeActorResource(std::span<const std::byte> data, std::string_view sourceName,
                                          const AnimationResolver& animations)
{
    const Decoder decoder(sourceName, animations);
    return json::decodeDocument<ActorResource>(decoder, data,
                                               [&decoder](const Json& root) { return decoder.decodeRoot(root); });
}

Status checkAssetExists(const AssetLocator& locator, const AssetPath& asset, std::string_view sourceName,
                        std::string_view path)
{
    const std::string prefix = sourceName.empty() ? std::string() : concat(sourceName, ": ", path, ": ");
    auto info = locator.info(asset);
    if (!info) return makeError(info.error().code, concat(prefix, info.error().message));
    if (info->type != fs::FileType::Regular) {
        return makeError(ErrorCode::NotFound, concat(prefix, "asset '", asset.view(), "' is not a regular file"));
    }
    return {};
}

Result<ActorResource> loadActorResource(const AssetLocator& locator, const AssetPath& asset,
                                        const AnimationResolver& animations)
{
    auto bytes = locator.readBytes(asset, maxActorFileBytes);
    if (!bytes) return std::unexpected(bytes.error());
    return decodeActorResource(*bytes, asset.view(), animations);
}

} // namespace schuimm::room
