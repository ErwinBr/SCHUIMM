#include "TestFramework.h"

#include "Core/Common/StringBuilder.h"
#include "Platform/FileSystem/FileSystem.h"
#include "Platform/FileSystem/Path.h"

#include <chrono>
#include <cstring>

using namespace schuimm;

namespace {

std::span<const std::byte> bytesOf(const char* text)
{
    return {reinterpret_cast<const std::byte*>(text), std::strlen(text)};
}

std::string asString(const std::vector<std::byte>& data)
{
    return {reinterpret_cast<const char*>(data.data()), data.size()};
}

// A fresh, empty directory below the build tree for one test case. It is removed again by
// TempDirectory's destructor (files the test created must be removed by the test).
class TempDirectory {
public:
    explicit TempDirectory(const char* name)
    {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = path::join(SCHUIMM_TEST_OUTPUT_DIR, concat("fs-", name, "-", stamp));
        created_ = fs::createDirectories(path_).has_value();
    }
    ~TempDirectory()
    {
        if (created_) {
            (void)fs::remove(path_);
        }
    }
    TempDirectory(const TempDirectory&) = delete;
    TempDirectory& operator=(const TempDirectory&) = delete;

    [[nodiscard]] bool created() const { return created_; }
    [[nodiscard]] std::string file(std::string_view name) const { return path::join(path_, name); }
    [[nodiscard]] const std::string& path() const { return path_; }

private:
    std::string path_;
    bool created_ = false;
};

} // namespace

SCHUIMM_TEST(FileSystem_WriteReadAndInspectFile)
{
    TempDirectory directory("readwrite");
    REQUIRE(directory.created());
    const std::string file = directory.file("hello.txt");
    REQUIRE(fs::writeFile(file, bytesOf("Hello, SCHUIMM")).has_value());

    const auto info = fs::fileInfo(file);
    REQUIRE(info.has_value());
    CHECK(info->type == fs::FileType::Regular);
    CHECK_EQ(info->size, std::uint64_t{14});

    const auto data = fs::readFile(file);
    REQUIRE(data.has_value());
    CHECK_EQ(asString(*data), std::string("Hello, SCHUIMM"));

    // Overwriting truncates.
    REQUIRE(fs::writeFile(file, bytesOf("Hi")).has_value());
    CHECK_EQ(asString(*fs::readFile(file)), std::string("Hi"));

    const auto limited = fs::readFile(file, 1);
    REQUIRE(!limited.has_value());
    CHECK(limited.error().code == ErrorCode::InvalidArgument);

    CHECK(fs::remove(file).has_value());
}

SCHUIMM_TEST(FileSystem_EmptyFile)
{
    TempDirectory directory("empty");
    REQUIRE(directory.created());
    const std::string file = directory.file("empty.bin");
    REQUIRE(fs::writeFile(file, {}).has_value());
    const auto data = fs::readFile(file);
    REQUIRE(data.has_value());
    CHECK(data->empty());
    CHECK(fs::remove(file).has_value());
}

SCHUIMM_TEST(FileSystem_MissingEntriesReportNotFound)
{
    TempDirectory directory("missing");
    REQUIRE(directory.created());
    const std::string missing = directory.file("does-not-exist.png");

    const auto info = fs::fileInfo(missing);
    REQUIRE(!info.has_value());
    CHECK(info.error().code == ErrorCode::NotFound);
    CHECK(info.error().message.find("does-not-exist.png") != std::string::npos);

    const auto data = fs::readFile(missing);
    REQUIRE(!data.has_value());
    CHECK(data.error().code == ErrorCode::NotFound);

    const auto listing = fs::listDirectory(missing);
    REQUIRE(!listing.has_value());
    CHECK(listing.error().code == ErrorCode::NotFound);

    const auto nested = fs::fileInfo(directory.file("no-dir/file.txt"));
    REQUIRE(!nested.has_value());
    CHECK(nested.error().code == ErrorCode::NotFound);

    CHECK(fs::remove(missing).error().code == ErrorCode::NotFound);
}

SCHUIMM_TEST(FileSystem_DirectoriesAndListing)
{
    TempDirectory directory("listing");
    REQUIRE(directory.created());
    const std::string nested = directory.file("a/b/c");
    REQUIRE(fs::createDirectories(nested).has_value());
    CHECK(fs::createDirectories(nested).has_value());          // Existing: fine.
    CHECK(fs::createDirectory(directory.file("a")).has_value()); // Existing: fine.
    REQUIRE(fs::writeFile(directory.file("z.txt"), bytesOf("z")).has_value());
    REQUIRE(fs::writeFile(directory.file("m.txt"), bytesOf("m")).has_value());

    const auto dirInfo = fs::fileInfo(nested);
    REQUIRE(dirInfo.has_value());
    CHECK(dirInfo->type == fs::FileType::Directory);

    const auto entries = fs::listDirectory(directory.path());
    REQUIRE(entries.has_value());
    REQUIRE(entries->size() == 3);
    CHECK_EQ((*entries)[0].name, std::string("a"));
    CHECK((*entries)[0].type == fs::FileType::Directory);
    CHECK_EQ((*entries)[1].name, std::string("m.txt"));
    CHECK((*entries)[1].type == fs::FileType::Regular);
    CHECK_EQ((*entries)[2].name, std::string("z.txt"));

    // A file in the way of a directory.
    const auto blocked = fs::createDirectory(directory.file("z.txt"));
    REQUIRE(!blocked.has_value());
    CHECK(blocked.error().code == ErrorCode::AlreadyExists);
    const auto blockedNested = fs::createDirectories(directory.file("z.txt/sub"));
    CHECK(!blockedNested.has_value());

    // A missing parent for a single-level create.
    const auto orphan = fs::createDirectory(directory.file("x/y"));
    REQUIRE(!orphan.has_value());
    CHECK(orphan.error().code == ErrorCode::NotFound);

    // Reading a directory is an argument error on every platform.
    const auto readDir = fs::readFile(nested);
    REQUIRE(!readDir.has_value());
    CHECK(readDir.error().code == ErrorCode::InvalidArgument);

    // Listing a file is NotFound (there is no such directory) on every platform.
    const auto listFile = fs::listDirectory(directory.file("m.txt"));
    REQUIRE(!listFile.has_value());
    CHECK(listFile.error().code == ErrorCode::NotFound);

    CHECK(fs::remove(directory.file("z.txt")).has_value());
    CHECK(fs::remove(directory.file("m.txt")).has_value());
    CHECK(fs::remove(nested).has_value());
    CHECK(fs::remove(directory.file("a/b")).has_value());
    CHECK(fs::remove(directory.file("a")).has_value());
}

SCHUIMM_TEST(FileSystem_Utf8FileNames)
{
    TempDirectory directory("utf8");
    REQUIRE(directory.created());
    // NFC-free name (no precomposed/decomposed ambiguity) so macOS returns it unchanged.
    const std::string name = "schuim-\xCE\xB1\xCE\xB2-\xE6\xB3\xA1.txt"; // schuim-αβ-泡.txt
    REQUIRE(fs::writeFile(directory.file(name), bytesOf("utf8")).has_value());
    const auto entries = fs::listDirectory(directory.path());
    REQUIRE(entries.has_value());
    REQUIRE(entries->size() == 1);
    CHECK_EQ((*entries)[0].name, name);
    CHECK_EQ(asString(*fs::readFile(directory.file(name))), std::string("utf8"));
    CHECK(fs::remove(directory.file(name)).has_value());
}

SCHUIMM_TEST(FileSystem_ExecutablePath)
{
    const auto executable = fs::executablePath();
    REQUIRE(executable.has_value());
    CHECK(path::isAbsolute(*executable));
    CHECK(path::fileName(*executable).starts_with("FileSystemTests"));
    const auto info = fs::fileInfo(*executable);
    REQUIRE(info.has_value());
    CHECK(info->type == fs::FileType::Regular);

    const auto directory = fs::executableDirectory();
    REQUIRE(directory.has_value());
    CHECK_EQ(*directory, path::parent(*executable));
}

SCHUIMM_TEST(FileSystem_PathsLongerThanMaxPath)
{
    // Windows' classic MAX_PATH is 260 characters; the Win32 implementation adds the "\\?\"
    // prefix for longer absolute paths. Five 60-character levels exceed it on every machine.
    TempDirectory directory("long");
    REQUIRE(directory.created());
    const std::string level(60, 'd');
    std::string deepest = directory.path();
    std::vector<std::string> levels;
    for (int i = 0; i < 5; ++i) {
        deepest = path::join(deepest, level);
        levels.push_back(deepest);
    }
    REQUIRE(deepest.size() > 300);
    REQUIRE(fs::createDirectories(deepest).has_value());
    const std::string file = path::join(deepest, "long.txt");
    REQUIRE(fs::writeFile(file, bytesOf("deep")).has_value());
    CHECK_EQ(asString(*fs::readFile(file)), std::string("deep"));
    CHECK(fs::fileInfo(file).has_value());
    CHECK_EQ(fs::listDirectory(deepest)->size(), std::size_t{1});

    CHECK(fs::remove(file).has_value());
    for (auto it = levels.rbegin(); it != levels.rend(); ++it) {
        CHECK(fs::remove(*it).has_value());
    }
}
