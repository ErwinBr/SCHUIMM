#pragma once

// Locations of the checked-in test assets (Tests/Assets) and a scratch directory, both absolute
// and passed in by CMake, so the tests do not depend on the working directory ctest uses.

#include "Core/Common/StringBuilder.h"
#include "Platform/FileSystem/FileSystem.h"
#include "Platform/FileSystem/Path.h"
#include "Resource/AssetLocator.h"

#include <chrono>
#include <string>

namespace schuimm::test {

inline std::string testAssetRoot()
{
    return SCHUIMM_TEST_ASSET_DIR;
}

inline AssetLocator testAssetLocator()
{
    return AssetLocator::create(testAssetRoot()).value();
}

inline AssetPath assetPath(std::string_view text)
{
    return AssetPath::parse(text).value();
}

// A new, empty directory below the build tree (not removed: it lives in the build tree only).
inline std::string makeScratchDirectory(std::string_view name)
{
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::string directory = path::join(SCHUIMM_TEST_OUTPUT_DIR, concat("scratch-", name, "-", stamp));
    (void)fs::createDirectories(directory);
    return directory;
}

} // namespace schuimm::test
