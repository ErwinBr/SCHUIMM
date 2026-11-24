#include "TestFramework.h"

#include "Resource/AssetPath.h"

using namespace schuimm;

namespace {
std::string normalised(std::string_view text)
{
    auto path = AssetPath::parse(text);
    return path ? path->string() : std::string("<error: ") + path.error().message + ">";
}

bool rejected(std::string_view text)
{
    auto path = AssetPath::parse(text);
    return !path && path.error().code == ErrorCode::InvalidArgument;
}
} // namespace

SCHUIMM_TEST(AssetPath_Normalisation)
{
    CHECK_EQ(normalised("sprites/hero.png"), std::string("sprites/hero.png"));
    CHECK_EQ(normalised("./sprites//hero.png"), std::string("sprites/hero.png"));
    CHECK_EQ(normalised("sprites/old/../hero.png"), std::string("sprites/hero.png"));
    CHECK_EQ(normalised("sprites/"), std::string("sprites"));
    CHECK_EQ(normalised("Grafik/schuim-\xC3\xBC.png"), std::string("Grafik/schuim-\xC3\xBC.png"));
}

SCHUIMM_TEST(AssetPath_RejectsInvalidPaths)
{
    CHECK(rejected(""));
    CHECK(rejected("."));
    CHECK(rejected("a/.."));
    CHECK(rejected("/etc/passwd"));        // absolute
    CHECK(rejected("../secret.png"));      // escapes the root
    CHECK(rejected("a/../../secret.png")); // escapes the root after normalisation
    CHECK(rejected("C:/Windows/x.png"));   // drive letter
    CHECK(rejected("sprites\\hero.png"));  // Windows separator
    CHECK(rejected("file.png:stream"));    // alternate data stream / URL scheme
    CHECK(rejected(std::string_view("a\0b", 3)));
    CHECK(rejected("tab\there"));

    const auto error = AssetPath::parse("../x.png");
    REQUIRE(!error.has_value());
    CHECK(error.error().message.find("../x.png") != std::string::npos);
}

SCHUIMM_TEST(AssetPath_NameExtensionEqualityAndHash)
{
    const AssetPath a = AssetPath::parse("ui/../sprites/hero.png").value();
    const AssetPath b = AssetPath::parse("sprites/hero.png").value();
    const AssetPath c = AssetPath::parse("sprites/Hero.png").value();
    CHECK(a == b);
    CHECK_EQ(a.hash(), b.hash());
    CHECK(!(a == c)); // Case-sensitive on every platform.
    CHECK(c < b);
    CHECK_EQ(std::string(a.fileName()), std::string("hero.png"));
    CHECK_EQ(std::string(a.extension()), std::string(".png"));
    CHECK_EQ(std::string(AssetPath::parse("README").value().extension()), std::string(""));
    // FNV-1a 64 reference values: the hash is stable across platforms and runs.
    CHECK_EQ(fnv1a64(""), std::uint64_t{0xcbf29ce484222325ull});
    CHECK_EQ(fnv1a64("a"), std::uint64_t{0xaf63dc4c8601ec8cull});
}
