#include "TestFramework.h"

#include "Platform/FileSystem/Path.h"

using namespace schuimm::path;

namespace {
constexpr PathStyle P = PathStyle::Posix;
constexpr PathStyle W = PathStyle::Windows;
} // namespace

SCHUIMM_TEST(Path_PosixNormalise)
{
    CHECK_EQ(normalise("a/b/c", P), std::string("a/b/c"));
    CHECK_EQ(normalise("a//b///c/", P), std::string("a/b/c"));
    CHECK_EQ(normalise("./a/./b/.", P), std::string("a/b"));
    CHECK_EQ(normalise("a/b/../c", P), std::string("a/c"));
    CHECK_EQ(normalise("a/../..", P), std::string(".."));
    CHECK_EQ(normalise("../../a", P), std::string("../../a"));
    CHECK_EQ(normalise("a/..", P), std::string("."));
    CHECK_EQ(normalise("", P), std::string("."));
    CHECK_EQ(normalise("/", P), std::string("/"));
    CHECK_EQ(normalise("//usr///lib/", P), std::string("/usr/lib"));
    CHECK_EQ(normalise("/../a", P), std::string("/a"));
    // A backslash is an ordinary file name character on POSIX.
    CHECK_EQ(normalise("a\\b", P), std::string("a\\b"));
}

SCHUIMM_TEST(Path_WindowsNormalise)
{
    CHECK_EQ(normalise("C:\\Games\\Schuimm\\..\\Assets\\", W), std::string("C:/Games/Assets"));
    CHECK_EQ(normalise("c:/a/./b", W), std::string("C:/a/b"));
    CHECK_EQ(normalise("C:\\..\\x", W), std::string("C:/x"));
    CHECK_EQ(normalise("C:", W), std::string("C:"));
    CHECK_EQ(normalise("C:\\", W), std::string("C:/"));
    CHECK_EQ(normalise("C:foo\\bar", W), std::string("C:foo/bar"));
    CHECK_EQ(normalise("\\\\server\\share\\dir\\..\\file.txt", W), std::string("//server/share/file.txt"));
    CHECK_EQ(normalise("\\\\?\\C:\\very\\long", W), std::string("C:/very/long"));
    CHECK_EQ(normalise("\\\\?\\UNC\\server\\share\\x", W), std::string("//server/share/x"));
    CHECK_EQ(normalise("a\\b/c", W), std::string("a/b/c"));
}

SCHUIMM_TEST(Path_RootAndAbsolute)
{
    CHECK(isAbsolute("/a", P));
    CHECK(!isAbsolute("a", P));
    CHECK(!isAbsolute("C:/a", P));
    CHECK_EQ(rootOf("/a/b", P), std::string("/"));
    CHECK_EQ(rootOf("a/b", P), std::string(""));

    CHECK(isAbsolute("C:/a", W));
    CHECK(isAbsolute("d:\\a", W));
    CHECK(isAbsolute("//server/share/a", W));
    CHECK(!isAbsolute("/a", W));    // Root of the current drive.
    CHECK(!isAbsolute("C:a", W));   // Relative to the drive's current directory.
    CHECK(!isAbsolute("a/b", W));
    CHECK_EQ(rootOf("c:\\x", W), std::string("C:/"));
    CHECK_EQ(rootOf("C:x", W), std::string("C:"));
    CHECK_EQ(rootOf("\\\\srv\\share\\x", W), std::string("//srv/share/"));
}

SCHUIMM_TEST(Path_JoinAndMakeAbsolute)
{
    CHECK_EQ(join("/games/demo", "Assets/sprite.png", P), std::string("/games/demo/Assets/sprite.png"));
    CHECK_EQ(join("/games/demo", "../shared/x", P), std::string("/games/shared/x"));
    CHECK_EQ(join("/games/demo", "/abs", P), std::string("/abs"));
    CHECK_EQ(join("", "a/b", P), std::string("a/b"));
    CHECK_EQ(join("C:\\Games", "Assets\\x.png", W), std::string("C:/Games/Assets/x.png"));
    CHECK_EQ(join("C:/Games", "D:/Other", W), std::string("D:/Other"));

    CHECK_EQ(makeAbsolute("Assets", "/opt/game/bin", P), std::string("/opt/game/bin/Assets"));
    CHECK_EQ(makeAbsolute("/srv/Assets", "/opt/game/bin", P), std::string("/srv/Assets"));
    // Without an absolute base there is nothing to resolve against: the current directory is
    // deliberately never used.
    CHECK_EQ(makeAbsolute("Assets", "relative/base", P), std::string(""));
    CHECK_EQ(makeAbsolute("..\\Assets", "C:\\Game\\bin", W), std::string("C:/Game/Assets"));
}

SCHUIMM_TEST(Path_ParentFileNameExtension)
{
    CHECK_EQ(parent("a/b/c", P), std::string("a/b"));
    CHECK_EQ(parent("a", P), std::string("."));
    CHECK_EQ(parent("/a", P), std::string("/"));
    CHECK_EQ(parent("/", P), std::string("/"));
    CHECK_EQ(parent("..", P), std::string("../.."));
    CHECK_EQ(parent(".", P), std::string(".."));
    CHECK_EQ(parent("C:/Games", W), std::string("C:/"));
    CHECK_EQ(parent("//srv/share/x", W), std::string("//srv/share/"));

    CHECK_EQ(fileName("dir/image.png", P), std::string("image.png"));
    CHECK_EQ(fileName("/", P), std::string(""));
    CHECK_EQ(fileName("C:file.txt", W), std::string("file.txt"));
    CHECK_EQ(fileName("C:\\dir\\file.txt", W), std::string("file.txt"));

    CHECK_EQ(extension("sprite.png", P), std::string(".png"));
    CHECK_EQ(extension("archive.tar.gz", P), std::string(".gz"));
    CHECK_EQ(extension(".config", P), std::string(""));
    CHECK_EQ(extension("dir.d/file", P), std::string(""));
    CHECK_EQ(extension("..", P), std::string(""));
}

SCHUIMM_TEST(Path_Utf8IsPreserved)
{
    CHECK_EQ(normalise("Ünïcödé/../Grafik/schuim-ü.png", P), std::string("Grafik/schuim-ü.png"));
    CHECK_EQ(extension("Grafik/schuim-ü.png", P), std::string(".png"));
}
