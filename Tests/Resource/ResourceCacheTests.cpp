#include "TestFramework.h"

#include "Resource/Image/ImageLoader.h"
#include "Resource/ResourceCache.h"
#include "TestAssets.h"

#include <type_traits>

using namespace schuimm;
using namespace schuimm::test;

namespace {

// A resource type that counts live instances, to observe ownership and destruction.
struct Probe {
    static inline int alive = 0;
    explicit Probe(std::string origin) : source(std::move(origin)) { ++alive; }
    Probe(Probe&& other) noexcept : source(std::move(other.source)) { ++alive; }
    Probe(const Probe&) = delete;
    Probe& operator=(const Probe&) = delete;
    Probe& operator=(Probe&&) = delete;
    ~Probe() { --alive; }
    std::string source;
};

struct Font {}; // Stand-in for a future resource type.

// Loader that reads the asset (so missing files fail like real loaders) and counts calls.
struct CountingLoader {
    int* calls;
    Result<Probe> operator()(const AssetLocator& locator, const AssetPath& asset) const
    {
        ++*calls;
        auto bytes = locator.readBytes(asset);
        if (!bytes) {
            return std::unexpected(bytes.error());
        }
        return Probe(asset.string());
    }
};

ResourceId<Probe> probeId(std::string_view text)
{
    return ResourceId<Probe>::parse(text).value();
}

} // namespace

// Identifiers of different resource types are different types: no accidental mix-ups.
static_assert(!std::is_convertible_v<ResourceId<Image>, ResourceId<Font>>);
static_assert(!std::is_constructible_v<ResourceId<Font>, ResourceId<Image>>);
static_assert(!std::is_convertible_v<AssetPath, ResourceId<Image>>); // explicit construction only

SCHUIMM_TEST(ResourceId_EqualityHashAndParsing)
{
    const auto a = ResourceId<Image>::parse("Images/../Images/rgba_4x2.png");
    const auto b = ResourceId<Image>::parse("Images/rgba_4x2.png");
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK(*a == *b);
    CHECK_EQ(std::hash<ResourceId<Image>>{}(*a), std::hash<ResourceId<Image>>{}(*b));
    CHECK_EQ(a->path().string(), std::string("Images/rgba_4x2.png"));

    const auto invalid = ResourceId<Image>::parse("../outside.png");
    REQUIRE(!invalid.has_value());
    CHECK(invalid.error().code == ErrorCode::InvalidArgument);
}

SCHUIMM_TEST(ResourceCache_LoadsOnceAndDeduplicates)
{
    const AssetLocator locator = testAssetLocator();
    int calls = 0;
    ResourceCache<Probe> cache(locator, CountingLoader{&calls});

    const auto first = cache.load(probeId("Images/rgba_4x2.png"));
    REQUIRE(first.has_value());
    const auto second = cache.load(probeId("Images/./rgba_4x2.png")); // same resource
    REQUIRE(second.has_value());
    CHECK_EQ(calls, 1);
    CHECK(first->get() == second->get());
    CHECK_EQ((*first)->source, std::string("Images/rgba_4x2.png"));
    CHECK_EQ(cache.size(), std::size_t{1});
    CHECK(cache.isLoaded(probeId("Images/rgba_4x2.png")));
    CHECK(cache.find(probeId("Images/rgba_4x2.png")) == *first);

    const auto other = cache.load(probeId("Images/rgb_3x1.png"));
    REQUIRE(other.has_value());
    CHECK_EQ(calls, 2);
    CHECK_EQ(cache.size(), std::size_t{2});
}

SCHUIMM_TEST(ResourceCache_FailuresAreReportedAndNotCached)
{
    const std::string root = makeScratchDirectory("cache-retry");
    const AssetLocator locator = AssetLocator::create(root).value();
    int calls = 0;
    ResourceCache<Probe> cache(locator, CountingLoader{&calls});

    const auto missing = cache.load(probeId("late.txt"));
    REQUIRE(!missing.has_value());
    CHECK(missing.error().code == ErrorCode::NotFound);
    CHECK(!cache.isLoaded(probeId("late.txt")));
    CHECK(cache.find(probeId("late.txt")) == nullptr);
    CHECK_EQ(cache.size(), std::size_t{0});

    // The file appears later: the next load retries and succeeds.
    const char text[] = "now here";
    REQUIRE(fs::writeFile(path::join(root, "late.txt"),
                          std::as_bytes(std::span<const char>(text, sizeof(text) - 1)))
                .has_value());
    const auto present = cache.load(probeId("late.txt"));
    REQUIRE(present.has_value());
    CHECK_EQ(calls, 2);
}

SCHUIMM_TEST(ResourceCache_UnloadAndOwnership)
{
    const AssetLocator locator = testAssetLocator();
    int calls = 0;
    Probe::alive = 0;
    ResourceCache<Probe>::Handle survivor;
    {
        ResourceCache<Probe> cache(locator, CountingLoader{&calls});
        const ResourceId<Probe> id = probeId("Images/rgba_4x2.png");

        // Unloading a resource nobody else holds destroys it.
        REQUIRE(cache.load(id).has_value());
        CHECK_EQ(Probe::alive, 1);
        CHECK(cache.unload(id));
        CHECK_EQ(Probe::alive, 0);
        CHECK(!cache.isLoaded(id));
        CHECK(!cache.unload(id)); // already unloaded

        // A holder keeps an unloaded resource alive and valid; it dies with the last holder.
        ResourceCache<Probe>::Handle held = cache.load(id).value();
        CHECK_EQ(calls, 2); // reloaded after unload
        CHECK(cache.unload(id));
        CHECK_EQ(Probe::alive, 1);
        CHECK_EQ(held->source, std::string("Images/rgba_4x2.png"));
        held.reset();
        CHECK_EQ(Probe::alive, 0);

        // unloadAll() and the cache's destructor release everything they own.
        REQUIRE(cache.load(id).has_value());
        REQUIRE(cache.load(probeId("Images/rgb_3x1.png")).has_value());
        CHECK_EQ(Probe::alive, 2);
        cache.unloadAll();
        CHECK_EQ(Probe::alive, 0);
        CHECK_EQ(cache.size(), std::size_t{0});

        survivor = cache.load(id).value();
        REQUIRE(cache.load(probeId("Images/rgb_3x1.png")).has_value());
        CHECK_EQ(Probe::alive, 2);
    }
    // Cache destroyed: the resource only it owned is gone, the one held outside survives.
    CHECK_EQ(Probe::alive, 1);
    CHECK_EQ(survivor->source, std::string("Images/rgba_4x2.png"));
    survivor.reset();
    CHECK_EQ(Probe::alive, 0);
}

SCHUIMM_TEST(ResourceCache_ImagesThroughLoadImage)
{
    // The real CPU-side image path: locator -> bytes -> PNG decoder -> cached Image.
    // No window or graphics context exists in this test.
    const AssetLocator locator = testAssetLocator();
    ResourceCache<Image> images(locator, loadImage);

    const auto image = images.load(ResourceId<Image>::parse("Images/rgba_4x2.png").value());
    REQUIRE(image.has_value());
    CHECK_EQ((*image)->width(), 4);
    CHECK_EQ((*image)->height(), 2);

    const auto invalid = images.load(ResourceId<Image>::parse("Images/not_an_image.png").value());
    REQUIRE(!invalid.has_value());
    CHECK(invalid.error().code == ErrorCode::InvalidData);
    CHECK(invalid.error().message.find("Images/not_an_image.png") != std::string::npos);

    const auto missing = images.load(ResourceId<Image>::parse("Images/none.png").value());
    REQUIRE(!missing.has_value());
    CHECK(missing.error().code == ErrorCode::NotFound);
    CHECK_EQ(images.size(), std::size_t{1});
}
