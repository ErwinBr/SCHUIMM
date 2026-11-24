#pragma once

#include "Core/Common/Error.h"
#include "Resource/AssetPath.h"

#include <compare>
#include <cstddef>
#include <functional>
#include <string_view>

namespace schuimm {

// Identifies one resource of type T by the asset it is loaded from.
//
// The type parameter keeps identifiers of different resource types apart at compile time:
// a ResourceId<Image> cannot be passed where a ResourceId<Font> is expected, even if both name
// the same file. Equality and hashing use the normalised AssetPath, so "ui/../hero.png" and
// "hero.png" identify the same resource. The hash is precomputed (AssetPath::hash()).
//
// There are deliberately no GUIDs or generated identifiers: the asset path is the stable,
// human-readable identity until a concrete requirement (renaming assets without breaking
// references, packed archives) calls for more.
template <typename T>
class ResourceId {
public:
    using ResourceType = T;

    explicit ResourceId(AssetPath path) noexcept
        : path_(std::move(path))
    {
    }

    [[nodiscard]] static Result<ResourceId> parse(std::string_view assetPath)
    {
        auto path = AssetPath::parse(assetPath);
        if (!path) {
            return std::unexpected(path.error());
        }
        return ResourceId(std::move(*path));
    }

    [[nodiscard]] const AssetPath& path() const noexcept { return path_; }
    [[nodiscard]] std::uint64_t hash() const noexcept { return path_.hash(); }

    friend bool operator==(const ResourceId& a, const ResourceId& b) noexcept = default;
    friend std::strong_ordering operator<=>(const ResourceId& a, const ResourceId& b) noexcept
    {
        return a.path_ <=> b.path_;
    }

private:
    AssetPath path_;
};

} // namespace schuimm

template <typename T>
struct std::hash<schuimm::ResourceId<T>> {
    std::size_t operator()(const schuimm::ResourceId<T>& id) const noexcept
    {
        return static_cast<std::size_t>(id.hash());
    }
};
