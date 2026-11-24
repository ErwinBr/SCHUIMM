#pragma once

#include "Core/Common/Error.h"
#include "Platform/FileSystem/FileSystem.h"
#include "Resource/AssetPath.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace schuimm {

// Maps AssetPaths to files below one asset root directory and reads them.
//
// The root is fixed at creation and always absolute: a relative root is resolved against an
// explicit base directory (normally fs::executableDirectory()), never against the process's
// current working directory, so asset lookup does not depend on how the game was started.
//
// Typical set-ups:
//   * Development: the root comes from the command line or a project file, e.g. the project's
//     Assets folder in the source tree.
//   * Packaged game: the root is next to the executable, or inside the macOS bundle; see
//     defaultAssetRootCandidates().
//
// An AssetLocator only locates and reads bytes. Decoding (Resource/Image) and caching
// (ResourceCache) are separate. It holds no mutable state, so a const AssetLocator may be shared
// freely; it is cheap to copy.
class AssetLocator {
public:
    // Fails with InvalidArgument if `root` is relative and `baseDirectory` is not absolute,
    // NotFound if the root does not exist, InvalidArgument if it is not a directory.
    [[nodiscard]] static Result<AssetLocator> create(std::string_view root, std::string_view baseDirectory = {});

    // The absolute, normalised root directory (generic '/' form).
    [[nodiscard]] const std::string& root() const noexcept { return root_; }

    // Absolute path of the asset's file. Does not check that it exists.
    [[nodiscard]] std::string resolve(const AssetPath& asset) const;

    // Errors are NotFound (no such asset), PermissionDenied or IoFailure; messages name the asset
    // path, the resolved file and the root.
    [[nodiscard]] Result<fs::FileInfo> info(const AssetPath& asset) const;
    [[nodiscard]] bool exists(const AssetPath& asset) const;
    [[nodiscard]] Result<std::vector<std::byte>> readBytes(const AssetPath& asset,
                                                           std::uint64_t maxBytes = fs::defaultMaxReadBytes) const;

private:
    explicit AssetLocator(std::string root);
    [[nodiscard]] Error describeFailure(const AssetPath& asset, const Error& error) const;

    std::string root_;
};

// Where a packaged game looks for its assets, most specific first, for the given absolute
// executable path:
//   1. macOS bundle (executable in X.app/Contents/MacOS/): X.app/Contents/Resources/Assets
//   2. <executable directory>/Assets
// Pure function: it does not touch the file system.
[[nodiscard]] std::vector<std::string> defaultAssetRootCandidates(std::string_view executablePath);

// Creates a locator for the first candidate that exists. The error lists every path tried.
[[nodiscard]] Result<AssetLocator> locateAssetRoot(const std::vector<std::string>& candidates);

} // namespace schuimm
