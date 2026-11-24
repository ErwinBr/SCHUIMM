#pragma once

#include "Core/Common/Error.h"

#include <compare>
#include <cstdint>
#include <string>
#include <string_view>

namespace schuimm {

// The engine's name for an asset: a path relative to the asset root, the same on every platform.
//
// Rules (enforced by parse()):
//   * UTF-8, '/' is the only separator; '\' and ':' are rejected (no Windows separators, drive
//     letters or alternate data streams), as are control characters.
//   * Always relative to the asset root: a leading '/' is rejected.
//   * Normalised: "." components, repeated and trailing '/' are removed and "a/../b" becomes "b".
//     A ".." that would leave the asset root is rejected.
//   * Not empty after normalisation.
//   * Case-sensitive: "Hero.png" and "hero.png" are different assets, even on file systems that
//     treat them as the same file (macOS, Windows).
//
// Because the rules do not depend on the host, an asset path that is valid on one platform is
// valid and means the same file on every other.
class AssetPath {
public:
    [[nodiscard]] static Result<AssetPath> parse(std::string_view text);

    [[nodiscard]] const std::string& string() const noexcept { return path_; }
    [[nodiscard]] std::string_view view() const noexcept { return path_; }

    // "sprites/hero.png" -> "hero.png" / ".png" (lower-case is not enforced).
    [[nodiscard]] std::string_view fileName() const noexcept;
    [[nodiscard]] std::string_view extension() const noexcept;

    // 64-bit FNV-1a of the normalised path. Deterministic across platforms and runs.
    [[nodiscard]] std::uint64_t hash() const noexcept { return hash_; }

    friend bool operator==(const AssetPath& a, const AssetPath& b) noexcept { return a.path_ == b.path_; }
    friend std::strong_ordering operator<=>(const AssetPath& a, const AssetPath& b) noexcept
    {
        return a.path_ <=> b.path_;
    }

private:
    explicit AssetPath(std::string normalised);

    std::string path_;
    std::uint64_t hash_ = 0;
};

[[nodiscard]] std::uint64_t fnv1a64(std::string_view text) noexcept;

} // namespace schuimm
