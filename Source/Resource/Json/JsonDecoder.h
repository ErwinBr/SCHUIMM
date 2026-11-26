#pragma once

// Internal helpers for the engine's JSON resource formats (rooms, animations, actors): strict
// parsing with duplicate-key detection, and readers that check every value's type and range and
// report errors as "<source>: <property path>: <reason>".
//
// Internal: include this header only from .cpp files of libraries that link
// Schuimm::ResourceJson privately, so nlohmann/json never appears in a public interface.
//
// Conventions shared by every format:
//   * Unknown properties are errors (they are usually typos; new properties come with a new
//     format version).
//   * Optional properties may be omitted or null.
//   * Identifiers: [A-Za-z_][A-Za-z0-9_]*.
//   * Coordinates are stored as float and limited to 2^20 pixels from the origin.

#include "Core/Common/Error.h"
#include "Core/Common/Geometry.h"
#include "Resource/AssetPath.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#define SCHUIMM_JSON_TRY(expression)                                                               \
    do {                                                                                           \
        if (auto status_ = (expression); !status_) return std::unexpected(std::move(status_.error())); \
    } while (false)

namespace schuimm::json {

using Json = nlohmann::json;

// Beyond 2^20 pixels a float no longer has sub-pixel precision.
inline constexpr double maxCoordinate = 1048576.0;

// "parent.key" ("key" at the root) and "parent[index]".
[[nodiscard]] std::string member(std::string_view parent, std::string_view key);
[[nodiscard]] std::string element(std::string_view parent, std::size_t index);
// "an object", "a string", "the number 3", ...
[[nodiscard]] std::string describeValue(const Json& value);

[[nodiscard]] bool isValidIdentifier(std::string_view text) noexcept;

class Decoder {
public:
    // Identifier -> property path of its first use.
    using IdMap = std::map<std::string, std::string, std::less<>>;

    explicit Decoder(std::string_view source)
        : source_(source)
    {
    }

    [[nodiscard]] std::string_view source() const noexcept { return source_; }

    // InvalidData "<source>: <path>: <reason>" ("<source>: <reason>" for an empty path).
    [[nodiscard]] std::unexpected<Error> fail(std::string_view path, std::string_view reason) const;

    // Parses a document. Syntax errors give the line and column; a duplicate key in any object
    // is an error (nlohmann would silently keep the last value).
    [[nodiscard]] Result<Json> parse(std::span<const std::byte> data) const;

    // The root must be an object whose integer "version" is in [minimum, maximum]. Checked first,
    // because the version decides how everything else is read. `kind` names the format in
    // messages ("room resource").
    [[nodiscard]] Status readVersion(const Json& root, std::string_view kind, int minimum, int maximum, int& out) const;

    // --- Structure ---------------------------------------------------------------------------
    [[nodiscard]] Status readJsonObject(const Json& value, std::string_view path) const;
    [[nodiscard]] Status readArray(const Json& value, std::string_view path) const;
    [[nodiscard]] Status required(const Json& object, std::string_view path, const char* key, const Json*& out) const;
    // nullptr if omitted or null.
    [[nodiscard]] static const Json* optional(const Json& object, const char* key);
    [[nodiscard]] Status onlyKnown(const Json& object, std::string_view path,
                                   std::initializer_list<std::string_view> known) const;

    // --- Values ------------------------------------------------------------------------------
    [[nodiscard]] Status readString(const Json& value, std::string_view path, std::string& out) const;
    [[nodiscard]] Status readBool(const Json& value, std::string_view path, bool& out) const;
    [[nodiscard]] Status readIdentifier(const Json& value, std::string_view path, std::string& out) const;
    // The object's required "id", which must not be in `ids` yet (then it is added).
    [[nodiscard]] Status readUniqueIdentifier(const Json& object, std::string_view path, IdMap& ids,
                                              std::string& out) const;
    [[nodiscard]] Status readInteger(const Json& value, std::string_view path, int minimum, int maximum,
                                     int& out) const;
    // Any finite number.
    [[nodiscard]] Status readNumber(const Json& value, std::string_view path, double& out) const;
    // A number in [minimum, maximum] (minimum > 0), stored as a float that stays within the range.
    [[nodiscard]] Status readFloatInRange(const Json& value, std::string_view path, double minimum, double maximum,
                                          float& out) const;
    [[nodiscard]] Status readCoordinate(const Json& value, std::string_view path, float& out) const;
    // [x, y] of coordinates.
    [[nodiscard]] Status readPoint(const Json& value, std::string_view path, Vec2& out) const;
    [[nodiscard]] Status readAssetPath(const Json& value, std::string_view path, std::optional<AssetPath>& out) const;
    [[nodiscard]] Status readAssetPath(const Json& value, std::string_view path, AssetPath& out) const;

private:
    std::string_view source_;
};

// Runs `decode` on the parsed document, turning any (unexpected) nlohmann exception into an
// error, so loaders never throw.
template <typename T>
[[nodiscard]] Result<T> decodeDocument(const Decoder& decoder, std::span<const std::byte> data,
                                       const std::function<Result<T>(const Json&)>& decode)
{
    auto root = decoder.parse(data);
    if (!root) return std::unexpected(root.error());
    try {
        return decode(*root);
    } catch (const Json::exception& error) {
        // Not expected (every value's type is checked before it is read).
        return decoder.fail("", std::string("cannot read the document: ") + error.what());
    }
}

} // namespace schuimm::json
