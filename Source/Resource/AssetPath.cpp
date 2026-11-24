#include "Resource/AssetPath.h"

#include "Core/Common/StringBuilder.h"
#include "Platform/FileSystem/Path.h"

namespace schuimm {

std::uint64_t fnv1a64(std::string_view text) noexcept
{
    std::uint64_t hash = 0xcbf29ce484222325ull;
    for (const char c : text) {
        hash ^= static_cast<std::uint8_t>(c);
        hash *= 0x100000001b3ull;
    }
    return hash;
}

Result<AssetPath> AssetPath::parse(std::string_view text)
{
    const auto invalid = [text](std::string_view reason) {
        return makeError(ErrorCode::InvalidArgument, concat("invalid asset path '", text, "': ", reason));
    };
    if (text.empty()) {
        return invalid("empty");
    }
    for (const char c : text) {
        const auto byte = static_cast<unsigned char>(c);
        if (c == '\\') {
            return invalid("use '/' as the separator");
        }
        if (c == ':') {
            return invalid("':' is not allowed (no drive letters or URLs)");
        }
        if (byte < 0x20 || byte == 0x7F) {
            return invalid("control characters are not allowed");
        }
    }
    if (text.front() == '/') {
        return invalid("must be relative to the asset root");
    }
    std::string normalised = path::normalise(text, path::PathStyle::Posix);
    if (normalised == ".") {
        return invalid("does not name a file");
    }
    if (normalised == ".." || normalised.starts_with("../")) {
        return invalid("leaves the asset root");
    }
    return AssetPath(std::move(normalised));
}

AssetPath::AssetPath(std::string normalised)
    : path_(std::move(normalised))
    , hash_(fnv1a64(path_))
{
}

std::string_view AssetPath::fileName() const noexcept
{
    const std::size_t slash = path_.rfind('/');
    return slash == std::string::npos ? std::string_view(path_) : std::string_view(path_).substr(slash + 1);
}

std::string_view AssetPath::extension() const noexcept
{
    const std::string_view name = fileName();
    const std::size_t dot = name.rfind('.');
    return dot == std::string_view::npos || dot == 0 ? std::string_view() : name.substr(dot);
}

} // namespace schuimm
