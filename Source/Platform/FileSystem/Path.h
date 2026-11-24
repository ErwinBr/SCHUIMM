#pragma once

// Pure path manipulation on UTF-8 strings. Nothing here touches the file system or the current
// working directory; see FileSystem.h for operations.
//
// Paths produced by these functions use '/' as the only separator on every platform ("generic"
// form). The file system layer converts them to the native form (backslashes and UTF-16 on
// Windows) at the operating-system boundary.
//
// Both syntaxes are implemented on every platform, selected by PathStyle, so that Windows path
// rules are unit-tested on macOS and Linux too:
//
//   Posix:   absolute = starts with '/'.
//   Windows: absolute = "C:/..." (drive) or "//server/share/..." (UNC). '\' is a separator.
//            "/foo" (current drive) and "C:foo" (drive-relative) are *not* absolute.

#include <string>
#include <string_view>

namespace schuimm::path {

enum class PathStyle {
    Posix,
    Windows,
};

#if defined(_WIN32)
inline constexpr PathStyle hostStyle = PathStyle::Windows;
#else
inline constexpr PathStyle hostStyle = PathStyle::Posix;
#endif

// The root prefix of a path in generic form: "/", "C:/", "C:", "//server/share/" or "".
[[nodiscard]] std::string rootOf(std::string_view path, PathStyle style = hostStyle);

[[nodiscard]] bool isAbsolute(std::string_view path, PathStyle style = hostStyle);

// Lexical normalisation:
//  - separators become '/', repeated separators collapse, a trailing separator is removed;
//  - "." components are removed; ".." removes the preceding component; ".." directly after a
//    root is dropped ("/.." is "/"); leading ".." of a relative path are kept;
//  - Windows: drive letters become upper case and a "\\?\" long-path prefix is removed;
//  - an empty relative result is ".".
// Symbolic links are not resolved (that would need the file system).
[[nodiscard]] std::string normalise(std::string_view path, PathStyle style = hostStyle);

// Appends `relative` to `base` and normalises. If `relative` is absolute it is returned
// normalised and `base` is ignored.
[[nodiscard]] std::string join(std::string_view base, std::string_view relative, PathStyle style = hostStyle);

// Resolves `path` against the absolute directory `base` (never the current working directory).
// Returns an empty string if neither `path` nor `base` is absolute.
[[nodiscard]] std::string makeAbsolute(std::string_view path, std::string_view base, PathStyle style = hostStyle);

// The normalised path without its last component: "a/b/c" -> "a/b", "a" -> ".", "/a" -> "/",
// "/" -> "/".
[[nodiscard]] std::string parent(std::string_view path, PathStyle style = hostStyle);

// The last component of the normalised path ("a/b.png" -> "b.png"); empty for a bare root.
[[nodiscard]] std::string fileName(std::string_view path, PathStyle style = hostStyle);

// The extension of fileName() including the dot (".png"), or empty. A leading dot does not start
// an extension (".config" has none).
[[nodiscard]] std::string extension(std::string_view path, PathStyle style = hostStyle);

} // namespace schuimm::path
