#include "Room/RoomResourceLoader.h"

#include "Core/Common/StringBuilder.h"

// The only engine file that includes nlohmann/json (a private dependency of schuimm_room).
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace schuimm::room {

namespace {

using Json = nlohmann::json;

constexpr int maxRoomSize = 65536;
// Room coordinates are stored as float; beyond 2^20 pixels a float no longer has sub-pixel precision.
constexpr double maxCoordinate = 1048576.0;
// Polygons with an area below this (square pixels) are degenerate: all points on one line.
constexpr double minPolygonArea = 1e-6;
constexpr std::uint64_t maxRoomFileBytes = 16u << 20;

#define SCHUIMM_ROOM_TRY(expression)                                                               \
    do {                                                                                           \
        if (auto status_ = (expression); !status_) return std::unexpected(std::move(status_.error())); \
    } while (false)

std::string member(std::string_view parent, std::string_view key)
{
    return parent.empty() ? std::string(key) : concat(parent, ".", key);
}

std::string element(std::string_view parent, std::size_t index)
{
    return concat(parent, "[", index, "]");
}

std::string describeValue(const Json& value)
{
    switch (value.type()) {
    case Json::value_t::object: return "an object";
    case Json::value_t::array: return "an array";
    case Json::value_t::string: return "a string";
    case Json::value_t::boolean: return "a boolean";
    case Json::value_t::null: return "null";
    case Json::value_t::number_integer:
    case Json::value_t::number_unsigned:
    case Json::value_t::number_float: return concat("the number ", value.dump());
    default: return "an unsupported value";
    }
}

// Records the first duplicate key of any JSON object while nlohmann parses (its objects are maps
// and would silently keep the last value), with the property path where it occurs.
class DuplicateKeyFinder {
public:
    bool onEvent(Json::parse_event_t event, const Json& parsed)
    {
        switch (event) {
        case Json::parse_event_t::object_start: stack_.push_back(Frame{false, 0, {}, {}}); break;
        case Json::parse_event_t::array_start: stack_.push_back(Frame{true, 0, {}, {}}); break;
        case Json::parse_event_t::key: {
            Frame& frame = stack_.back();
            frame.key = parsed.get<std::string>();
            if (!frame.keys.insert(frame.key).second && !duplicate_) {
                duplicate_ = std::make_pair(path(), frame.key);
            }
            break;
        }
        case Json::parse_event_t::object_end:
        case Json::parse_event_t::array_end:
            stack_.pop_back();
            elementDone();
            break;
        case Json::parse_event_t::value: elementDone(); break;
        }
        return true;
    }

    // {path of the duplicate member, key}.
    [[nodiscard]] const std::optional<std::pair<std::string, std::string>>& duplicate() const noexcept { return duplicate_; }

private:
    struct Frame {
        bool array = false;
        std::size_t index = 0;
        std::string key;
        std::set<std::string> keys;
    };

    void elementDone()
    {
        if (!stack_.empty() && stack_.back().array) ++stack_.back().index;
    }

    [[nodiscard]] std::string path() const
    {
        std::string text;
        for (const Frame& frame : stack_) {
            text = frame.array ? element(text, frame.index) : member(text, frame.key);
        }
        return text;
    }

    std::vector<Frame> stack_;
    std::optional<std::pair<std::string, std::string>> duplicate_;
};

class Decoder {
public:
    explicit Decoder(std::string_view source)
        : source_(source)
    {
    }

    [[nodiscard]] std::unexpected<Error> fail(std::string_view path, std::string_view reason) const
    {
        return makeError(ErrorCode::InvalidData,
                         path.empty() ? concat(source_, ": ", reason) : concat(source_, ": ", path, ": ", reason));
    }

    Result<RoomResource> decodeRoot(const Json& root)
    {
        if (!root.is_object()) {
            return fail("", concat("a room resource must be a JSON object, found ", describeValue(root)));
        }
        // The version decides how everything else is read, so it is checked first.
        const Json* version = nullptr;
        SCHUIMM_ROOM_TRY(required(root, "", "version", version));
        if (!version->is_number_integer()) {
            return fail("version", concat("expected an integer, found ", describeValue(*version)));
        }
        if (version->is_number_unsigned() ? version->get<std::uint64_t>() != static_cast<std::uint64_t>(roomResourceVersion)
                                          : version->get<std::int64_t>() != roomResourceVersion) {
            return fail("version", concat("unsupported room resource version ", version->dump(),
                                          " (this engine reads version ", roomResourceVersion, ")"));
        }
        SCHUIMM_ROOM_TRY(onlyKnown(root, "", {"version", "id", "size", "script", "background", "props", "objects",
                                              "scale_map", "navigation", "regions"}));

        RoomResource room;
        const Json* value = nullptr;
        SCHUIMM_ROOM_TRY(required(root, "", "id", value));
        SCHUIMM_ROOM_TRY(readIdentifier(*value, "id", room.id));

        SCHUIMM_ROOM_TRY(required(root, "", "size", value));
        SCHUIMM_ROOM_TRY(readSize(*value, "size", room.size));

        if (const Json* script = optional(root, "script")) {
            SCHUIMM_ROOM_TRY(readAssetPath(*script, "script", room.script));
            if (room.script->extension() != ".schb") {
                return fail("script", concat("must name a compiled script (.schb), found '", room.script->view(), "'"));
            }
        }
        if (const Json* background = optional(root, "background")) {
            SCHUIMM_ROOM_TRY(readBackground(*background, "background", room.background));
        }

        std::map<std::string, std::string, std::less<>> itemIds; // Props and objects share one namespace.
        if (const Json* props = optional(root, "props")) {
            SCHUIMM_ROOM_TRY(readArray(*props, "props"));
            for (std::size_t i = 0; i < props->size(); ++i) {
                SCHUIMM_ROOM_TRY(readProp((*props)[i], element("props", i), itemIds, room.props));
            }
        }
        if (const Json* objects = optional(root, "objects")) {
            SCHUIMM_ROOM_TRY(readArray(*objects, "objects"));
            for (std::size_t i = 0; i < objects->size(); ++i) {
                SCHUIMM_ROOM_TRY(readObject((*objects)[i], element("objects", i), itemIds, room.objects));
            }
        }
        if (const Json* scaleMap = optional(root, "scale_map")) {
            ScaleMap map;
            SCHUIMM_ROOM_TRY(readScaleMap(*scaleMap, "scale_map", room.size, map));
            room.scaleMap = std::move(map);
        }
        if (const Json* navigation = optional(root, "navigation")) {
            SCHUIMM_ROOM_TRY(readNavigation(*navigation, "navigation", room.walkableAreas));
        }
        if (const Json* regions = optional(root, "regions")) {
            SCHUIMM_ROOM_TRY(readArray(*regions, "regions"));
            std::map<std::string, std::string, std::less<>> regionIds;
            for (std::size_t i = 0; i < regions->size(); ++i) {
                SCHUIMM_ROOM_TRY(readRegion((*regions)[i], element("regions", i), regionIds, room.regions));
            }
        }
        return room;
    }

private:
    using IdMap = std::map<std::string, std::string, std::less<>>;

    // --- Structure -------------------------------------------------------------------------------

    Status readJsonObject(const Json& value, std::string_view path) const
    {
        if (!value.is_object()) return fail(path, concat("expected an object, found ", describeValue(value)));
        return {};
    }

    Status readArray(const Json& value, std::string_view path) const
    {
        if (!value.is_array()) return fail(path, concat("expected an array, found ", describeValue(value)));
        return {};
    }

    Status required(const Json& object, std::string_view path, const char* key, const Json*& out) const
    {
        const auto found = object.find(key);
        if (found == object.end() || found->is_null()) return fail(member(path, key), "missing required property");
        out = &*found;
        return {};
    }

    // Optional properties may be omitted or null.
    static const Json* optional(const Json& object, const char* key)
    {
        const auto found = object.find(key);
        return found == object.end() || found->is_null() ? nullptr : &*found;
    }

    // Unknown properties are errors: they are usually typos, and new properties come with a new
    // format version.
    Status onlyKnown(const Json& object, std::string_view path, std::initializer_list<std::string_view> known) const
    {
        for (const auto& item : object.items()) {
            bool isKnown = false;
            for (std::string_view name : known) isKnown = isKnown || item.key() == name;
            if (!isKnown) return fail(member(path, item.key()), "unknown property");
        }
        return {};
    }

    // --- Values ----------------------------------------------------------------------------------

    Status readString(const Json& value, std::string_view path, std::string& out) const
    {
        if (!value.is_string()) return fail(path, concat("expected a string, found ", describeValue(value)));
        out = value.get<std::string>();
        return {};
    }

    Status readIdentifier(const Json& value, std::string_view path, std::string& out) const
    {
        SCHUIMM_ROOM_TRY(readString(value, path, out));
        if (!isValidRoomIdentifier(out)) {
            return fail(path, concat("invalid identifier '", out,
                                     "' (letters, digits and '_' only, not starting with a digit)"));
        }
        return {};
    }

    Status readUniqueIdentifier(const Json& object, std::string_view path, IdMap& ids, std::string& out) const
    {
        const Json* value = nullptr;
        SCHUIMM_ROOM_TRY(required(object, path, "id", value));
        const std::string idPath = member(path, "id");
        SCHUIMM_ROOM_TRY(readIdentifier(*value, idPath, out));
        const auto [existing, inserted] = ids.try_emplace(out, std::string(path));
        if (!inserted) {
            return fail(idPath, concat("duplicate identifier '", out, "' (first used by ", existing->second, ")"));
        }
        return {};
    }

    Status readInteger(const Json& value, std::string_view path, int minimum, int maximum, int& out) const
    {
        const auto outOfRange = [&] {
            return fail(path, concat("must be an integer from ", minimum, " to ", maximum, ", found ", value.dump()));
        };
        if (!value.is_number_integer()) return fail(path, concat("expected an integer, found ", describeValue(value)));
        if (value.is_number_unsigned()) {
            const std::uint64_t number = value.get<std::uint64_t>();
            if (number > static_cast<std::uint64_t>(maximum)) return outOfRange();
            out = static_cast<int>(number);
        } else {
            const std::int64_t number = value.get<std::int64_t>();
            if (number < minimum || number > maximum) return outOfRange();
            out = static_cast<int>(number);
        }
        if (out < minimum) return outOfRange();
        return {};
    }

    Status readNumber(const Json& value, std::string_view path, double& out) const
    {
        if (!value.is_number()) return fail(path, concat("expected a number, found ", describeValue(value)));
        out = value.get<double>();
        if (!std::isfinite(out)) return fail(path, "must be a finite number");
        return {};
    }

    Status readCoordinate(const Json& value, std::string_view path, float& out) const
    {
        double number = 0.0;
        SCHUIMM_ROOM_TRY(readNumber(value, path, number));
        if (std::fabs(number) > maxCoordinate) {
            return fail(path, concat("coordinate ", value.dump(), " is out of range (at most ", maxCoordinate,
                                     " pixels from the origin)"));
        }
        out = static_cast<float>(number);
        return {};
    }

    // [x, y]
    Status readPoint(const Json& value, std::string_view path, Vec2& out) const
    {
        if (!value.is_array() || value.size() != 2) {
            return fail(path, concat("expected a point [x, y], found ", describeValue(value)));
        }
        SCHUIMM_ROOM_TRY(readCoordinate(value[0], element(path, 0), out.x));
        SCHUIMM_ROOM_TRY(readCoordinate(value[1], element(path, 1), out.y));
        return {};
    }

    Status readAssetPath(const Json& value, std::string_view path, std::optional<AssetPath>& out) const
    {
        std::string text;
        SCHUIMM_ROOM_TRY(readString(value, path, text));
        auto parsed = AssetPath::parse(text);
        if (!parsed) return fail(path, parsed.error().message);
        out = std::move(*parsed);
        return {};
    }

    Status readPolygon(const Json& value, std::string_view path, std::vector<Vec2>& out) const
    {
        SCHUIMM_ROOM_TRY(readArray(value, path));
        if (value.size() < 3) {
            return fail(path, concat("a polygon needs at least 3 points, found ", value.size()));
        }
        out.resize(value.size());
        for (std::size_t i = 0; i < value.size(); ++i) {
            SCHUIMM_ROOM_TRY(readPoint(value[i], element(path, i), out[i]));
        }
        for (std::size_t i = 1; i < out.size(); ++i) {
            if (out[i] == out[i - 1]) {
                return fail(element(path, i), concat("repeats the previous point (", out[i].x, ", ", out[i].y, ")"));
            }
        }
        if (out.back() == out.front()) {
            return fail(element(path, out.size() - 1),
                        "repeats the first point (polygons are closed implicitly; do not repeat the first point)");
        }
        double twiceArea = 0.0; // Shoelace formula.
        for (std::size_t i = 0; i < out.size(); ++i) {
            const Vec2 a = out[i];
            const Vec2 b = out[(i + 1) % out.size()];
            twiceArea += static_cast<double>(a.x) * static_cast<double>(b.y) - static_cast<double>(b.x) * static_cast<double>(a.y);
        }
        if (std::fabs(twiceArea) / 2.0 < minPolygonArea) {
            return fail(path, "polygon has zero area (all points lie on one line)");
        }
        return {};
    }

    // --- Sections --------------------------------------------------------------------------------

    Status readSize(const Json& value, std::string_view path, Extent2D& out) const
    {
        SCHUIMM_ROOM_TRY(readJsonObject(value, path));
        SCHUIMM_ROOM_TRY(onlyKnown(value, path, {"width", "height"}));
        const Json* field = nullptr;
        SCHUIMM_ROOM_TRY(required(value, path, "width", field));
        SCHUIMM_ROOM_TRY(readInteger(*field, member(path, "width"), 1, maxRoomSize, out.width));
        SCHUIMM_ROOM_TRY(required(value, path, "height", field));
        SCHUIMM_ROOM_TRY(readInteger(*field, member(path, "height"), 1, maxRoomSize, out.height));
        return {};
    }

    Status readBackground(const Json& value, std::string_view path, std::optional<AssetPath>& out) const
    {
        SCHUIMM_ROOM_TRY(readJsonObject(value, path));
        SCHUIMM_ROOM_TRY(onlyKnown(value, path, {"image"}));
        const Json* image = nullptr;
        SCHUIMM_ROOM_TRY(required(value, path, "image", image));
        return readAssetPath(*image, member(path, "image"), out);
    }

    Status readPlacement(const Json& value, std::string_view path, Vec2& position, Vec2& pivot) const
    {
        const Json* field = nullptr;
        SCHUIMM_ROOM_TRY(required(value, path, "position", field));
        SCHUIMM_ROOM_TRY(readPoint(*field, member(path, "position"), position));
        if (const Json* pivotValue = optional(value, "pivot")) {
            SCHUIMM_ROOM_TRY(readPoint(*pivotValue, member(path, "pivot"), pivot));
        }
        return {};
    }

    Status readProp(const Json& value, const std::string& path, IdMap& ids, std::vector<RoomProp>& out) const
    {
        SCHUIMM_ROOM_TRY(readJsonObject(value, path));
        SCHUIMM_ROOM_TRY(onlyKnown(value, path, {"id", "sprite", "position", "pivot"}));
        std::string id;
        SCHUIMM_ROOM_TRY(readUniqueIdentifier(value, path, ids, id));
        const Json* field = nullptr;
        std::optional<AssetPath> sprite;
        SCHUIMM_ROOM_TRY(required(value, path, "sprite", field));
        SCHUIMM_ROOM_TRY(readAssetPath(*field, member(path, "sprite"), sprite));
        Vec2 position;
        Vec2 pivot;
        SCHUIMM_ROOM_TRY(readPlacement(value, path, position, pivot));
        out.push_back(RoomProp{std::move(id), std::move(*sprite), position, pivot});
        return {};
    }

    Status readObject(const Json& value, const std::string& path, IdMap& ids, std::vector<RoomObjectDefinition>& out) const
    {
        SCHUIMM_ROOM_TRY(readJsonObject(value, path));
        SCHUIMM_ROOM_TRY(onlyKnown(value, path, {"id", "sprite", "position", "pivot"}));
        RoomObjectDefinition object;
        SCHUIMM_ROOM_TRY(readUniqueIdentifier(value, path, ids, object.id));
        if (const Json* sprite = optional(value, "sprite")) {
            SCHUIMM_ROOM_TRY(readAssetPath(*sprite, member(path, "sprite"), object.sprite));
        }
        SCHUIMM_ROOM_TRY(readPlacement(value, path, object.position, object.pivot));
        out.push_back(std::move(object));
        return {};
    }

    Status readScaleMap(const Json& value, std::string_view path, Extent2D size, ScaleMap& out) const
    {
        SCHUIMM_ROOM_TRY(readJsonObject(value, path));
        SCHUIMM_ROOM_TRY(onlyKnown(value, path, {"type", "points"}));
        const Json* field = nullptr;
        std::string type;
        SCHUIMM_ROOM_TRY(required(value, path, "type", field));
        SCHUIMM_ROOM_TRY(readString(*field, member(path, "type"), type));
        if (type != "linear") {
            return fail(member(path, "type"), concat("unsupported scale map type '", type, "' (supported: linear)"));
        }
        out.type = ScaleMap::Type::Linear;
        SCHUIMM_ROOM_TRY(required(value, path, "points", field));
        const std::string pointsPath = member(path, "points");
        SCHUIMM_ROOM_TRY(readArray(*field, pointsPath));
        if (field->size() < 2) {
            return fail(pointsPath, concat("a scale map needs at least 2 points, found ", field->size()));
        }
        for (std::size_t i = 0; i < field->size(); ++i) {
            const Json& point = (*field)[i];
            const std::string pointPath = element(pointsPath, i);
            SCHUIMM_ROOM_TRY(readJsonObject(point, pointPath));
            SCHUIMM_ROOM_TRY(onlyKnown(point, pointPath, {"y", "scale"}));
            const Json* member_ = nullptr;
            double y = 0.0;
            double scale = 0.0;
            SCHUIMM_ROOM_TRY(required(point, pointPath, "y", member_));
            SCHUIMM_ROOM_TRY(readNumber(*member_, member(pointPath, "y"), y));
            if (y < 0.0 || y > size.height) {
                return fail(member(pointPath, "y"), concat("must be within the room (0 to ", size.height, "), found ",
                                                           member_->dump()));
            }
            // Compared as stored (float), so the points stay strictly increasing after conversion.
            if (!out.points.empty() && !(static_cast<float>(y) > out.points.back().y)) {
                return fail(member(pointPath, "y"), concat("must be greater than the previous point's y (",
                                                           static_cast<double>(out.points.back().y), ")"));
            }
            SCHUIMM_ROOM_TRY(required(point, pointPath, "scale", member_));
            SCHUIMM_ROOM_TRY(readNumber(*member_, member(pointPath, "scale"), scale));
            if (!(scale > 0.0) || scale > 1e6) {
                return fail(member(pointPath, "scale"), concat("must be a positive number, found ", member_->dump()));
            }
            out.points.push_back(ScalePoint{static_cast<float>(y), static_cast<float>(scale)});
        }
        return {};
    }

    Status readNavigation(const Json& value, std::string_view path, std::vector<WalkableArea>& out) const
    {
        SCHUIMM_ROOM_TRY(readJsonObject(value, path));
        SCHUIMM_ROOM_TRY(onlyKnown(value, path, {"walkable_areas"}));
        const Json* areas = optional(value, "walkable_areas");
        if (areas == nullptr) return {};
        const std::string areasPath = member(path, "walkable_areas");
        SCHUIMM_ROOM_TRY(readArray(*areas, areasPath));
        IdMap ids;
        for (std::size_t i = 0; i < areas->size(); ++i) {
            const Json& area = (*areas)[i];
            const std::string areaPath = element(areasPath, i);
            SCHUIMM_ROOM_TRY(readJsonObject(area, areaPath));
            SCHUIMM_ROOM_TRY(onlyKnown(area, areaPath, {"id", "points"}));
            WalkableArea walkable;
            SCHUIMM_ROOM_TRY(readUniqueIdentifier(area, areaPath, ids, walkable.id));
            const Json* points = nullptr;
            SCHUIMM_ROOM_TRY(required(area, areaPath, "points", points));
            SCHUIMM_ROOM_TRY(readPolygon(*points, member(areaPath, "points"), walkable.points));
            out.push_back(std::move(walkable));
        }
        return {};
    }

    Status readRegion(const Json& value, const std::string& path, IdMap& ids, std::vector<RoomRegion>& out) const
    {
        SCHUIMM_ROOM_TRY(readJsonObject(value, path));
        RoomRegion region;
        const Json* field = nullptr;
        std::string type;
        SCHUIMM_ROOM_TRY(required(value, path, "type", field));
        SCHUIMM_ROOM_TRY(readString(*field, member(path, "type"), type));
        if (type == "rectangle") {
            SCHUIMM_ROOM_TRY(onlyKnown(value, path, {"id", "type", "position", "size"}));
            SCHUIMM_ROOM_TRY(readUniqueIdentifier(value, path, ids, region.id));
            region.shape = RoomRegion::Shape::Rectangle;
            Vec2 position;
            Vec2 size;
            SCHUIMM_ROOM_TRY(required(value, path, "position", field));
            SCHUIMM_ROOM_TRY(readPoint(*field, member(path, "position"), position));
            SCHUIMM_ROOM_TRY(required(value, path, "size", field));
            SCHUIMM_ROOM_TRY(readPoint(*field, member(path, "size"), size));
            if (!(size.x > 0.0f) || !(size.y > 0.0f)) {
                return fail(member(path, "size"), concat("width and height must be positive, found [", size.x, ", ",
                                                         size.y, "]"));
            }
            region.rectangle = RectF{position.x, position.y, size.x, size.y};
        } else if (type == "polygon") {
            SCHUIMM_ROOM_TRY(onlyKnown(value, path, {"id", "type", "points"}));
            SCHUIMM_ROOM_TRY(readUniqueIdentifier(value, path, ids, region.id));
            region.shape = RoomRegion::Shape::Polygon;
            SCHUIMM_ROOM_TRY(required(value, path, "points", field));
            SCHUIMM_ROOM_TRY(readPolygon(*field, member(path, "points"), region.points));
        } else {
            return fail(member(path, "type"),
                        concat("unsupported region type '", type, "' (supported: rectangle, polygon)"));
        }
        out.push_back(std::move(region));
        return {};
    }

    std::string_view source_;
};

// "parse error at line 4, column 17: syntax error ..." -> "syntax error ...".
std::string parseErrorReason(const char* what)
{
    const std::string_view text(what);
    const std::size_t column = text.find("column ");
    if (column != std::string_view::npos) {
        const std::size_t colon = text.find(": ", column);
        if (colon != std::string_view::npos) return std::string(text.substr(colon + 2));
    }
    return std::string(text);
}

} // namespace

bool isValidRoomIdentifier(std::string_view text) noexcept
{
    if (text.empty()) return false;
    const auto isLetter = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; };
    if (!isLetter(text[0])) return false;
    for (char c : text) {
        if (!isLetter(c) && !(c >= '0' && c <= '9')) return false;
    }
    return true;
}

Result<RoomResource> decodeRoomResource(std::span<const std::byte> data, std::string_view sourceName)
{
    Decoder decoder(sourceName);
    const char* const begin = reinterpret_cast<const char*>(data.data());
    const char* const end = begin + data.size();
    DuplicateKeyFinder duplicates;
    Json root;
    try {
        root = Json::parse(begin, end, [&duplicates](int, Json::parse_event_t event, Json& parsed) {
            return duplicates.onEvent(event, parsed);
        });
    } catch (const Json::parse_error& error) {
        // error.byte: 1-based offset of the character where parsing failed.
        std::size_t line = 1;
        std::size_t column = 1;
        const std::size_t stop = error.byte == 0 ? 0 : std::min<std::size_t>(error.byte - 1, data.size());
        for (std::size_t i = 0; i < stop; ++i) {
            if (begin[i] == '\n') {
                ++line;
                column = 1;
            } else {
                ++column;
            }
        }
        return decoder.fail("", concat("invalid JSON at line ", line, ", column ", column, ": ", parseErrorReason(error.what())));
    } catch (const Json::exception& error) {
        return decoder.fail("", concat("invalid JSON: ", error.what()));
    }
    if (const auto& duplicate = duplicates.duplicate()) {
        return decoder.fail(duplicate->first, concat("duplicate property '", duplicate->second, "'"));
    }
    try {
        return decoder.decodeRoot(root);
    } catch (const Json::exception& error) {
        // Not expected (every value's type is checked before it is read); keeps the API non-throwing.
        return decoder.fail("", concat("cannot read the room resource: ", error.what()));
    }
}

Status verifyRoomAssets(const RoomResource& room, const AssetLocator& locator, std::string_view sourceName)
{
    const auto check = [&](const AssetPath& asset, const std::string& path) -> Status {
        auto info = locator.info(asset);
        if (!info) return makeError(info.error().code, concat(sourceName, ": ", path, ": ", info.error().message));
        if (info->type != fs::FileType::Regular) {
            return makeError(ErrorCode::NotFound,
                             concat(sourceName, ": ", path, ": asset '", asset.view(), "' is not a regular file"));
        }
        return {};
    };
    if (room.script) SCHUIMM_ROOM_TRY(check(*room.script, "script"));
    if (room.background) SCHUIMM_ROOM_TRY(check(*room.background, "background.image"));
    for (std::size_t i = 0; i < room.props.size(); ++i) {
        SCHUIMM_ROOM_TRY(check(room.props[i].sprite, member(element("props", i), "sprite")));
    }
    for (std::size_t i = 0; i < room.objects.size(); ++i) {
        if (room.objects[i].sprite) {
            SCHUIMM_ROOM_TRY(check(*room.objects[i].sprite, member(element("objects", i), "sprite")));
        }
    }
    return {};
}

Result<RoomResource> loadRoomResource(const AssetLocator& locator, const AssetPath& asset)
{
    auto bytes = locator.readBytes(asset, maxRoomFileBytes);
    if (!bytes) return std::unexpected(bytes.error());
    auto room = decodeRoomResource(*bytes, asset.view());
    if (!room) return room;
    SCHUIMM_ROOM_TRY(verifyRoomAssets(*room, locator, asset.view()));
    return room;
}

} // namespace schuimm::room
