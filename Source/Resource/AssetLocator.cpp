#include "Resource/AssetLocator.h"

#include "Core/Common/StringBuilder.h"
#include "Platform/FileSystem/Path.h"

namespace schuimm {

Result<AssetLocator> AssetLocator::create(std::string_view root, std::string_view baseDirectory)
{
    if (root.empty()) {
        return makeError(ErrorCode::InvalidArgument, "asset root is empty");
    }
    std::string absolute = path::makeAbsolute(root, baseDirectory);
    if (absolute.empty()) {
        return makeError(ErrorCode::InvalidArgument,
                         concat("asset root '", root, "' is relative and the base directory '", baseDirectory,
                                "' is not absolute; the current working directory is never used"));
    }
    auto info = fs::fileInfo(absolute);
    if (!info) {
        return makeError(info.error().code, concat("asset root '", absolute, "' is not accessible: ", info.error().message));
    }
    if (info->type != fs::FileType::Directory) {
        return makeError(ErrorCode::InvalidArgument, concat("asset root '", absolute, "' is not a directory"));
    }
    return AssetLocator(std::move(absolute));
}

AssetLocator::AssetLocator(std::string root)
    : root_(std::move(root))
{
}

std::string AssetLocator::resolve(const AssetPath& asset) const
{
    // AssetPath is relative and cannot contain "..": the result always stays below the root.
    return path::join(root_, asset.view());
}

Error AssetLocator::describeFailure(const AssetPath& asset, const Error& error) const
{
    const char* what = error.code == ErrorCode::NotFound ? "not found" : "cannot be read";
    return Error{error.code, concat("asset '", asset.string(), "' ", what, " (asset root '", root_, "'): ",
                                    error.message)};
}

Result<fs::FileInfo> AssetLocator::info(const AssetPath& asset) const
{
    auto info = fs::fileInfo(resolve(asset));
    if (!info) {
        return std::unexpected(describeFailure(asset, info.error()));
    }
    return info;
}

bool AssetLocator::exists(const AssetPath& asset) const
{
    const auto info = fs::fileInfo(resolve(asset));
    return info.has_value() && info->type == fs::FileType::Regular;
}

Result<std::vector<std::byte>> AssetLocator::readBytes(const AssetPath& asset, std::uint64_t maxBytes) const
{
    auto data = fs::readFile(resolve(asset), maxBytes);
    if (!data) {
        return std::unexpected(describeFailure(asset, data.error()));
    }
    return data;
}

std::vector<std::string> defaultAssetRootCandidates(std::string_view executablePath)
{
    std::vector<std::string> candidates;
    const std::string directory = path::parent(executablePath);
    // X.app/Contents/MacOS/<binary>: bundle resources live in X.app/Contents/Resources.
    const std::string contents = path::parent(directory);
    if (path::fileName(directory) == "MacOS" && path::fileName(contents) == "Contents" &&
        path::extension(path::parent(contents)) == ".app") {
        candidates.push_back(path::join(contents, "Resources/Assets"));
    }
    candidates.push_back(path::join(directory, "Assets"));
    return candidates;
}

Result<AssetLocator> locateAssetRoot(const std::vector<std::string>& candidates)
{
    std::string tried;
    for (const std::string& candidate : candidates) {
        auto locator = AssetLocator::create(candidate);
        if (locator) {
            return locator;
        }
        tried += concat("\n  ", candidate, ": ", locator.error().message);
    }
    return makeError(ErrorCode::NotFound, concat("no asset root found; tried:", tried.empty() ? " (no candidates)" : tried));
}

} // namespace schuimm
