#pragma once

// File system operations behind one interface for all platforms.
//
//   * Paths are UTF-8 strings. Results use the generic form from Path.h ('/' separators).
//   * The native implementation is chosen at configure time: POSIX on macOS and Linux
//     (Posix/PosixFileSystem.cpp), Win32 wide-character APIs on Windows
//     (Windows/Win32FileSystem.cpp, UTF-8 <-> UTF-16 conversion, long-path prefix).
//   * Every fallible operation returns Result/Status. Error codes are the same on every
//     platform: NotFound, PermissionDenied, AlreadyExists, InvalidArgument (e.g. invalid UTF-8),
//     IoFailure (anything else). Messages name the operation, the path and the OS reason.
//   * Relative paths are passed to the operating system unchanged and therefore resolve against
//     the process's current directory. Engine code passes absolute paths (see
//     Resource/AssetLocator.h); path::makeAbsolute() builds them without consulting the
//     current directory.
//   * No state is kept; all functions may be called from any thread.

#include "Core/Common/Error.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace schuimm::fs {

enum class FileType {
    Regular,
    Directory,
    Other, // Device, socket, pipe, ...
};

struct FileInfo {
    FileType type = FileType::Other;
    std::uint64_t size = 0; // Bytes; 0 for directories.
};

struct DirectoryEntry {
    std::string name; // File name only, UTF-8.
    FileType type = FileType::Other;
};

// Information about the entry at `path` (symbolic links are followed).
[[nodiscard]] Result<FileInfo> fileInfo(std::string_view path);

// The entries of a directory, without "." and "..", sorted by name (byte-wise).
[[nodiscard]] Result<std::vector<DirectoryEntry>> listDirectory(std::string_view path);

inline constexpr std::uint64_t defaultMaxReadBytes = 1ull << 30; // 1 GiB

// Reads a whole regular file. Fails with InvalidArgument if it is larger than `maxBytes`.
[[nodiscard]] Result<std::vector<std::byte>> readFile(std::string_view path,
                                                      std::uint64_t maxBytes = defaultMaxReadBytes);

// Creates or truncates a file and writes `data` to it.
[[nodiscard]] Status writeFile(std::string_view path, std::span<const std::byte> data);

// Creates one directory. Succeeds if it already exists as a directory; AlreadyExists if a
// non-directory is in the way; NotFound if the parent does not exist.
[[nodiscard]] Status createDirectory(std::string_view path);

// Creates a directory and all missing parents.
[[nodiscard]] Status createDirectories(std::string_view path);

// Removes a file or an empty directory.
[[nodiscard]] Status remove(std::string_view path);

// Absolute path of the running executable (on macOS the binary inside the .app bundle).
[[nodiscard]] Result<std::string> executablePath();

// Directory containing the running executable.
[[nodiscard]] Result<std::string> executableDirectory();

} // namespace schuimm::fs
