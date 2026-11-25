#include "Animation/AnimationResourceLoader.h"

#include "Core/Common/StringBuilder.h"
#include "Core/Image/Image.h"
#include "Resource/Json/JsonDecoder.h"

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace schuimm::animation {

namespace {

using json::element;
using json::Json;
using json::member;

constexpr std::uint64_t maxAnimationFileBytes = 16u << 20;

class Decoder : public json::Decoder {
public:
    Decoder(std::string_view source, const ImageSizeFunction& imageSize)
        : json::Decoder(source)
        , imageSize_(imageSize)
    {
    }

    Result<AnimationResource> decodeRoot(const Json& root)
    {
        int version = 0;
        SCHUIMM_JSON_TRY(readVersion(root, "animation resource", 1, animationResourceVersion, version));
        SCHUIMM_JSON_TRY(onlyKnown(root, "", {"version", "clips"}));
        const Json* clips = nullptr;
        SCHUIMM_JSON_TRY(required(root, "", "clips", clips));
        SCHUIMM_JSON_TRY(readJsonObject(*clips, "clips"));
        if (clips->empty()) return fail("clips", "an animation resource needs at least one clip");

        AnimationResource resource;
        // nlohmann objects iterate in key order: the clips come out sorted by name.
        for (const auto& item : clips->items()) {
            const std::string path = member("clips", item.key());
            if (!json::isValidIdentifier(item.key())) {
                return fail(path, concat("invalid clip name '", item.key(),
                                         "' (letters, digits and '_' only, not starting with a digit)"));
            }
            AnimationClip clip;
            clip.name = item.key();
            SCHUIMM_JSON_TRY(readClip(item.value(), path, clip));
            resource.clips.push_back(std::move(clip));
        }
        return resource;
    }

private:
    Status readClip(const Json& value, const std::string& path, AnimationClip& clip)
    {
        SCHUIMM_JSON_TRY(readJsonObject(value, path));
        SCHUIMM_JSON_TRY(onlyKnown(value, path, {"loop", "frames"}));
        const Json* field = nullptr;
        SCHUIMM_JSON_TRY(required(value, path, "loop", field));
        SCHUIMM_JSON_TRY(readBool(*field, member(path, "loop"), clip.loop));
        SCHUIMM_JSON_TRY(required(value, path, "frames", field));
        const std::string framesPath = member(path, "frames");
        SCHUIMM_JSON_TRY(readArray(*field, framesPath));
        if (field->empty()) return fail(framesPath, "a clip needs at least one frame");
        if (field->size() > maxFramesPerClip) {
            return fail(framesPath, concat("a clip has at most ", maxFramesPerClip, " frames, found ", field->size()));
        }
        for (std::size_t i = 0; i < field->size(); ++i) {
            const std::string framePath = element(framesPath, i);
            auto frame = readFrame((*field)[i], framePath);
            if (!frame) return std::unexpected(frame.error());
            clip.frameStarts.push_back(clip.totalDuration);
            clip.totalDuration += frame->duration; // Both below 2^32: no overflow.
            if (clip.totalDuration > maxClipDuration) {
                return fail(member(framePath, "duration"),
                            concat("the clip's total duration exceeds ", maxClipDuration, " ticks"));
            }
            clip.frames.push_back(std::move(*frame));
        }
        return {};
    }

    Result<AnimationFrame> readFrame(const Json& value, const std::string& path)
    {
        SCHUIMM_JSON_TRY(readJsonObject(value, path));
        SCHUIMM_JSON_TRY(onlyKnown(value, path, {"image", "source", "pivot", "duration"}));
        const Json* field = nullptr;
        SCHUIMM_JSON_TRY(required(value, path, "image", field));
        const std::string imagePath = member(path, "image");
        std::optional<AssetPath> image;
        SCHUIMM_JSON_TRY(readAssetPath(*field, imagePath, image));
        AnimationFrame frame{std::move(*image), {}, {}, 1};
        auto size = sizeOf(frame.image, imagePath);
        if (!size) return std::unexpected(size.error());

        if (const Json* source = optional(value, "source")) {
            SCHUIMM_JSON_TRY(readSource(*source, member(path, "source"), *size, frame.source));
        } else {
            frame.source = RectI{0, 0, size->width, size->height};
        }
        if (const Json* pivot = optional(value, "pivot")) {
            SCHUIMM_JSON_TRY(readPoint(*pivot, member(path, "pivot"), frame.pivot));
        }
        SCHUIMM_JSON_TRY(required(value, path, "duration", field));
        int duration = 0;
        SCHUIMM_JSON_TRY(readInteger(*field, member(path, "duration"), 1, static_cast<int>(maxFrameDuration), duration));
        frame.duration = static_cast<std::uint32_t>(duration);
        return frame;
    }

    // [x, y, width, height], integers, inside an image of `image` size.
    Status readSource(const Json& value, const std::string& path, Extent2D image, RectI& out) const
    {
        if (!value.is_array() || value.size() != 4) {
            return fail(path, concat("expected a rectangle [x, y, width, height], found ", json::describeValue(value)));
        }
        SCHUIMM_JSON_TRY(readInteger(value[0], element(path, 0), 0, Image::maxDimension, out.x));
        SCHUIMM_JSON_TRY(readInteger(value[1], element(path, 1), 0, Image::maxDimension, out.y));
        SCHUIMM_JSON_TRY(readInteger(value[2], element(path, 2), 1, Image::maxDimension, out.width));
        SCHUIMM_JSON_TRY(readInteger(value[3], element(path, 3), 1, Image::maxDimension, out.height));
        // All four are at most 16384: the sums cannot overflow.
        if (out.x + out.width > image.width || out.y + out.height > image.height) {
            return fail(path, concat("rectangle [", out.x, ", ", out.y, ", ", out.width, ", ", out.height,
                                     "] is not inside the ", image.width, "x", image.height, " image"));
        }
        return {};
    }

    Result<Extent2D> sizeOf(const AssetPath& image, const std::string& path)
    {
        if (const auto known = sizes_.find(image); known != sizes_.end()) return known->second;
        auto size = imageSize_(image);
        if (!size) {
            return makeError(size.error().code, concat(source(), ": ", path, ": ", size.error().message));
        }
        if (size->isEmpty()) return fail(path, concat("image '", image.view(), "' is empty"));
        sizes_.emplace(image, *size);
        return *size;
    }

    const ImageSizeFunction& imageSize_;
    std::map<AssetPath, Extent2D> sizes_;
};

} // namespace

Result<AnimationResource> decodeAnimationResource(std::span<const std::byte> data, std::string_view sourceName,
                                                  const ImageSizeFunction& imageSize)
{
    Decoder decoder(sourceName, imageSize);
    return json::decodeDocument<AnimationResource>(decoder, data,
                                                   [&decoder](const Json& root) { return decoder.decodeRoot(root); });
}

Result<AnimationResource> loadAnimationResource(const AssetLocator& locator, const AssetPath& asset,
                                                ResourceCache<ImageInfo>& images)
{
    auto bytes = locator.readBytes(asset, maxAnimationFileBytes);
    if (!bytes) return std::unexpected(bytes.error());
    const ImageSizeFunction imageSize = [&images](const AssetPath& image) -> Result<Extent2D> {
        auto info = images.load(ResourceId<ImageInfo>(image));
        if (!info) return std::unexpected(info.error());
        return (*info)->size;
    };
    return decodeAnimationResource(*bytes, asset.view(), imageSize);
}

} // namespace schuimm::animation
