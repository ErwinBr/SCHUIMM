#include "TestFramework.h"

#include "TestAssets.h"

using namespace schuimm;
using namespace schuimm::test;

SCHUIMM_TEST(AssetLocator_AbsoluteRoot)
{
    const auto locator = AssetLocator::create(testAssetRoot());
    REQUIRE(locator.has_value());
    CHECK_EQ(locator->root(), path::normalise(testAssetRoot()));
    CHECK(path::isAbsolute(locator->root()));
}

SCHUIMM_TEST(AssetLocator_RootIsNormalised)
{
    const std::string messy = testAssetRoot() + "/Images/../";
    const auto locator = AssetLocator::create(messy);
    REQUIRE(locator.has_value());
    CHECK_EQ(locator->root(), path::normalise(testAssetRoot()));
}

SCHUIMM_TEST(AssetLocator_RelativeRootUsesBaseDirectoryNotWorkingDirectory)
{
    // "Images" relative to the asset directory: only correct if the explicit base is used,
    // whatever the current working directory of the test process is.
    const auto locator = AssetLocator::create("Images", testAssetRoot());
    REQUIRE(locator.has_value());
    CHECK_EQ(locator->root(), path::join(testAssetRoot(), "Images"));
    CHECK(locator->exists(assetPath("rgba_4x2.png")));

    const auto parentRelative = AssetLocator::create("..", path::join(testAssetRoot(), "Images"));
    REQUIRE(parentRelative.has_value());
    CHECK_EQ(parentRelative->root(), path::normalise(testAssetRoot()));

    // Without an absolute base, a relative root is refused instead of silently using the CWD.
    const auto noBase = AssetLocator::create("Images");
    REQUIRE(!noBase.has_value());
    CHECK(noBase.error().code == ErrorCode::InvalidArgument);
    const auto relativeBase = AssetLocator::create("Images", "Tests/Assets");
    CHECK(!relativeBase.has_value());
}

SCHUIMM_TEST(AssetLocator_InvalidRoots)
{
    const auto missing = AssetLocator::create(path::join(testAssetRoot(), "NoSuchDirectory"));
    REQUIRE(!missing.has_value());
    CHECK(missing.error().code == ErrorCode::NotFound);
    CHECK(missing.error().message.find("NoSuchDirectory") != std::string::npos);

    const auto file = AssetLocator::create(path::join(testAssetRoot(), "Images/rgba_4x2.png"));
    REQUIRE(!file.has_value());
    CHECK(file.error().code == ErrorCode::InvalidArgument);

    CHECK(!AssetLocator::create("").has_value());
}

SCHUIMM_TEST(AssetLocator_ResolvesRelativePaths)
{
    const AssetLocator locator = testAssetLocator();
    const AssetPath asset = assetPath("Images/./sub/../rgba_4x2.png");
    CHECK_EQ(asset.string(), std::string("Images/rgba_4x2.png"));
    CHECK_EQ(locator.resolve(asset), path::join(testAssetRoot(), "Images/rgba_4x2.png"));
    CHECK(locator.exists(asset));

    const auto info = locator.info(asset);
    REQUIRE(info.has_value());
    CHECK(info->type == fs::FileType::Regular);
    CHECK(info->size > 0);

    const auto bytes = locator.readBytes(asset);
    REQUIRE(bytes.has_value());
    CHECK_EQ(bytes->size(), static_cast<std::size_t>(info->size));

    // A directory is not an asset.
    CHECK(!locator.exists(assetPath("Images")));
}

SCHUIMM_TEST(AssetLocator_MissingAssetError)
{
    const AssetLocator locator = testAssetLocator();
    const AssetPath missing = assetPath("Images/missing.png");
    CHECK(!locator.exists(missing));

    const auto bytes = locator.readBytes(missing);
    REQUIRE(!bytes.has_value());
    CHECK(bytes.error().code == ErrorCode::NotFound);
    // The message names the asset, the root and (through the file system error) the file.
    CHECK(bytes.error().message.find("'Images/missing.png'") != std::string::npos);
    CHECK(bytes.error().message.find(locator.root()) != std::string::npos);
    CHECK(bytes.error().message.find("not found") != std::string::npos);

    const auto info = locator.info(missing);
    REQUIRE(!info.has_value());
    CHECK(info.error().code == ErrorCode::NotFound);
}

SCHUIMM_TEST(AssetLocator_DefaultRootCandidates)
{
    const auto plain = defaultAssetRootCandidates("/opt/game/bin/Game");
    REQUIRE(plain.size() == 1);
    CHECK_EQ(plain[0], std::string("/opt/game/bin/Assets"));

    const auto bundle = defaultAssetRootCandidates("/Applications/Game.app/Contents/MacOS/Game");
    REQUIRE(bundle.size() == 2);
    CHECK_EQ(bundle[0], std::string("/Applications/Game.app/Contents/Resources/Assets"));
    CHECK_EQ(bundle[1], std::string("/Applications/Game.app/Contents/MacOS/Assets"));

#if defined(_WIN32)
    const auto windows = defaultAssetRootCandidates("C:\\Games\\Game\\Game.exe");
    REQUIRE(windows.size() == 1);
    CHECK_EQ(windows[0], std::string("C:/Games/Game/Assets"));
#endif
}

SCHUIMM_TEST(AssetLocator_LocateAssetRootPicksFirstExisting)
{
    const std::string missing = path::join(testAssetRoot(), "NotHere");
    const auto found = locateAssetRoot({missing, testAssetRoot()});
    REQUIRE(found.has_value());
    CHECK_EQ(found->root(), path::normalise(testAssetRoot()));

    const auto none = locateAssetRoot({missing});
    REQUIRE(!none.has_value());
    CHECK(none.error().code == ErrorCode::NotFound);
    CHECK(none.error().message.find("NotHere") != std::string::npos);
}
