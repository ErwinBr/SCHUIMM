#include "Room/RoomResourceLoader.h"

#include "Core/Common/StringBuilder.h"

#include "Resource/Json/JsonDecoder.h"
#include "Navigation/NavigationMap.h"
#include "Navigation/Polygon.h"
#include "Room/ActorResource.h"

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

using json::Json;
using json::element;
using json::member;

constexpr int maxRoomSize = 65536;
constexpr std::uint64_t maxRoomFileBytes = 16u << 20;

class Decoder : public json::Decoder {
public:
    using json::Decoder::Decoder;

    Result<RoomResource> decodeRoot(const Json& root) const
    {
        int version = 0;
        SCHUIMM_JSON_TRY(readVersion(root, "room resource", roomResourceVersion, roomResourceVersion, version));
        SCHUIMM_JSON_TRY(onlyKnown(root, "", {"version", "id", "size", "script", "background", "props", "objects",
                                              "scale_map", "navigation", "regions", "actors"}));

        RoomResource room;
        const Json* value = nullptr;
        SCHUIMM_JSON_TRY(required(root, "", "id", value));
        SCHUIMM_JSON_TRY(readIdentifier(*value, "id", room.id));

        SCHUIMM_JSON_TRY(required(root, "", "size", value));
        SCHUIMM_JSON_TRY(readSize(*value, "size", room.size));

        if (const Json* script = optional(root, "script")) {
            SCHUIMM_JSON_TRY(readAssetPath(*script, "script", room.script));
            if (room.script->extension() != ".schb") {
                return fail("script", concat("must name a compiled script (.schb), found '", room.script->view(), "'"));
            }
        }
        if (const Json* background = optional(root, "background")) {
            SCHUIMM_JSON_TRY(readBackground(*background, "background", room.background));
        }

        IdMap itemIds; // Props and objects share one namespace.
        if (const Json* props = optional(root, "props")) {
            SCHUIMM_JSON_TRY(readArray(*props, "props"));
            for (std::size_t i = 0; i < props->size(); ++i) {
                SCHUIMM_JSON_TRY(readProp((*props)[i], element("props", i), itemIds, room.props));
            }
        }
        if (const Json* objects = optional(root, "objects")) {
            SCHUIMM_JSON_TRY(readArray(*objects, "objects"));
            for (std::size_t i = 0; i < objects->size(); ++i) {
                SCHUIMM_JSON_TRY(readObject((*objects)[i], element("objects", i), itemIds, room.objects));
            }
        }
        if (const Json* scaleMap = optional(root, "scale_map")) {
            ScaleMap map;
            SCHUIMM_JSON_TRY(readScaleMap(*scaleMap, "scale_map", room.size, map));
            room.scaleMap = std::move(map);
        }
        if (const Json* navigation = optional(root, "navigation")) {
            SCHUIMM_JSON_TRY(readNavigation(*navigation, "navigation", room.size, room.walkableAreas));
        }
        if (const Json* regions = optional(root, "regions")) {
            SCHUIMM_JSON_TRY(readArray(*regions, "regions"));
            IdMap regionIds;
            for (std::size_t i = 0; i < regions->size(); ++i) {
                SCHUIMM_JSON_TRY(readRegion((*regions)[i], element("regions", i), regionIds, room.regions));
            }
        }
        if (const Json* actors = optional(root, "actors")) {
            SCHUIMM_JSON_TRY(readArray(*actors, "actors"));
            IdMap actorIds; // Their own namespace: an actor may share an id with an object.
            for (std::size_t i = 0; i < actors->size(); ++i) {
                SCHUIMM_JSON_TRY(readActor((*actors)[i], element("actors", i), actorIds, room.actors));
            }
        }
        return room;
    }

private:
    // --- Values ----------------------------------------------------------------------------------

    // A simple polygon (Navigation/Polygon.h), validated on the stored float coordinates.
    Status readPolygon(const Json& value, std::string_view path, std::vector<Vec2>& out) const
    {
        SCHUIMM_JSON_TRY(readArray(value, path));
        if (value.size() > navigation::maxPolygonVertices) {
            return fail(path, concat("a polygon has at most ", navigation::maxPolygonVertices, " points, found ",
                                     value.size()));
        }
        out.resize(value.size());
        for (std::size_t i = 0; i < value.size(); ++i) {
            SCHUIMM_JSON_TRY(readPoint(value[i], element(path, i), out[i]));
        }
        if (const auto defect = navigation::findPolygonDefect(out)) {
            return fail(path, navigation::describe(*defect, out.size()));
        }
        return {};
    }

    // --- Sections --------------------------------------------------------------------------------

    Status readSize(const Json& value, std::string_view path, Extent2D& out) const
    {
        SCHUIMM_JSON_TRY(readJsonObject(value, path));
        SCHUIMM_JSON_TRY(onlyKnown(value, path, {"width", "height"}));
        const Json* field = nullptr;
        SCHUIMM_JSON_TRY(required(value, path, "width", field));
        SCHUIMM_JSON_TRY(readInteger(*field, member(path, "width"), 1, maxRoomSize, out.width));
        SCHUIMM_JSON_TRY(required(value, path, "height", field));
        SCHUIMM_JSON_TRY(readInteger(*field, member(path, "height"), 1, maxRoomSize, out.height));
        return {};
    }

    Status readBackground(const Json& value, std::string_view path, std::optional<AssetPath>& out) const
    {
        SCHUIMM_JSON_TRY(readJsonObject(value, path));
        SCHUIMM_JSON_TRY(onlyKnown(value, path, {"image"}));
        const Json* image = nullptr;
        SCHUIMM_JSON_TRY(required(value, path, "image", image));
        return readAssetPath(*image, member(path, "image"), out);
    }

    Status readPlacement(const Json& value, std::string_view path, Vec2& position, Vec2& pivot) const
    {
        const Json* field = nullptr;
        SCHUIMM_JSON_TRY(required(value, path, "position", field));
        SCHUIMM_JSON_TRY(readPoint(*field, member(path, "position"), position));
        if (const Json* pivotValue = optional(value, "pivot")) {
            SCHUIMM_JSON_TRY(readPoint(*pivotValue, member(path, "pivot"), pivot));
        }
        return {};
    }

    Status readProp(const Json& value, const std::string& path, IdMap& ids, std::vector<RoomProp>& out) const
    {
        SCHUIMM_JSON_TRY(readJsonObject(value, path));
        SCHUIMM_JSON_TRY(onlyKnown(value, path, {"id", "sprite", "position", "pivot"}));
        std::string id;
        SCHUIMM_JSON_TRY(readUniqueIdentifier(value, path, ids, id));
        const Json* field = nullptr;
        std::optional<AssetPath> sprite;
        SCHUIMM_JSON_TRY(required(value, path, "sprite", field));
        SCHUIMM_JSON_TRY(readAssetPath(*field, member(path, "sprite"), sprite));
        Vec2 position;
        Vec2 pivot;
        SCHUIMM_JSON_TRY(readPlacement(value, path, position, pivot));
        out.push_back(RoomProp{std::move(id), std::move(*sprite), position, pivot});
        return {};
    }

    Status readObject(const Json& value, const std::string& path, IdMap& ids, std::vector<RoomObjectDefinition>& out) const
    {
        SCHUIMM_JSON_TRY(readJsonObject(value, path));
        SCHUIMM_JSON_TRY(onlyKnown(value, path, {"id", "sprite", "position", "pivot"}));
        RoomObjectDefinition object;
        SCHUIMM_JSON_TRY(readUniqueIdentifier(value, path, ids, object.id));
        if (const Json* sprite = optional(value, "sprite")) {
            SCHUIMM_JSON_TRY(readAssetPath(*sprite, member(path, "sprite"), object.sprite));
        }
        SCHUIMM_JSON_TRY(readPlacement(value, path, object.position, object.pivot));
        out.push_back(std::move(object));
        return {};
    }

    Status readScaleMap(const Json& value, std::string_view path, Extent2D size, ScaleMap& out) const
    {
        SCHUIMM_JSON_TRY(readJsonObject(value, path));
        SCHUIMM_JSON_TRY(onlyKnown(value, path, {"type", "points"}));
        const Json* field = nullptr;
        std::string type;
        SCHUIMM_JSON_TRY(required(value, path, "type", field));
        SCHUIMM_JSON_TRY(readString(*field, member(path, "type"), type));
        if (type != "linear") {
            return fail(member(path, "type"), concat("unsupported scale map type '", type, "' (supported: linear)"));
        }
        out.type = ScaleMap::Type::Linear;
        SCHUIMM_JSON_TRY(required(value, path, "points", field));
        const std::string pointsPath = member(path, "points");
        SCHUIMM_JSON_TRY(readArray(*field, pointsPath));
        if (field->size() < 2) {
            return fail(pointsPath, concat("a scale map needs at least 2 points, found ", field->size()));
        }
        for (std::size_t i = 0; i < field->size(); ++i) {
            const Json& point = (*field)[i];
            const std::string pointPath = element(pointsPath, i);
            SCHUIMM_JSON_TRY(readJsonObject(point, pointPath));
            SCHUIMM_JSON_TRY(onlyKnown(point, pointPath, {"y", "scale"}));
            const Json* member_ = nullptr;
            double y = 0.0;
            SCHUIMM_JSON_TRY(required(point, pointPath, "y", member_));
            SCHUIMM_JSON_TRY(readNumber(*member_, member(pointPath, "y"), y));
            if (y < 0.0 || y > size.height) {
                return fail(member(pointPath, "y"), concat("must be within the room (0 to ", size.height, "), found ",
                                                           member_->dump()));
            }
            // Compared as stored (float), so the points stay strictly increasing after conversion.
            if (!out.points.empty() && !(static_cast<float>(y) > out.points.back().y)) {
                return fail(member(pointPath, "y"), concat("must be greater than the previous point's y (",
                                                           static_cast<double>(out.points.back().y), ")"));
            }
            SCHUIMM_JSON_TRY(required(point, pointPath, "scale", member_));
            float storedScale = 1.0f;
            SCHUIMM_JSON_TRY(readFloatInRange(*member_, member(pointPath, "scale"), minScaleMapScale, maxScaleMapScale,
                                              storedScale));
            out.points.push_back(ScalePoint{static_cast<float>(y), storedScale});
        }
        return {};
    }

    // Walkable areas must lie inside the room, and their number and size are limited so that
    // building the navigation map stays cheap (Navigation/NavigationMap.h).
    Status readNavigation(const Json& value, std::string_view path, Extent2D size, std::vector<WalkableArea>& out) const
    {
        SCHUIMM_JSON_TRY(readJsonObject(value, path));
        SCHUIMM_JSON_TRY(onlyKnown(value, path, {"walkable_areas"}));
        const Json* areas = optional(value, "walkable_areas");
        if (areas == nullptr) return {};
        const std::string areasPath = member(path, "walkable_areas");
        SCHUIMM_JSON_TRY(readArray(*areas, areasPath));
        if (areas->size() > navigation::maxNavigationPolygons) {
            return fail(areasPath, concat("a room has at most ", navigation::maxNavigationPolygons,
                                          " walkable areas, found ", areas->size()));
        }
        IdMap ids;
        std::size_t vertices = 0;
        for (std::size_t i = 0; i < areas->size(); ++i) {
            const Json& area = (*areas)[i];
            const std::string areaPath = element(areasPath, i);
            SCHUIMM_JSON_TRY(readJsonObject(area, areaPath));
            SCHUIMM_JSON_TRY(onlyKnown(area, areaPath, {"id", "points"}));
            WalkableArea walkable;
            SCHUIMM_JSON_TRY(readUniqueIdentifier(area, areaPath, ids, walkable.id));
            const Json* points = nullptr;
            SCHUIMM_JSON_TRY(required(area, areaPath, "points", points));
            const std::string pointsPath = member(areaPath, "points");
            SCHUIMM_JSON_TRY(readPolygon(*points, pointsPath, walkable.points));
            for (std::size_t p = 0; p < walkable.points.size(); ++p) {
                const Vec2 point = walkable.points[p];
                if (point.x < 0.0f || point.y < 0.0f || point.x > static_cast<float>(size.width) ||
                    point.y > static_cast<float>(size.height)) {
                    return fail(element(pointsPath, p), concat("(", point.x, ", ", point.y, ") is outside the room (0 to ",
                                                               size.width, " x 0 to ", size.height, ")"));
                }
            }
            vertices += walkable.points.size();
            if (vertices > navigation::maxNavigationVertices) {
                return fail(pointsPath, concat("a room's walkable areas have at most ", navigation::maxNavigationVertices,
                                               " points in total"));
            }
            out.push_back(std::move(walkable));
        }
        return {};
    }

    Status readActor(const Json& value, const std::string& path, IdMap& ids, std::vector<ActorPlacement>& out) const
    {
        SCHUIMM_JSON_TRY(readJsonObject(value, path));
        SCHUIMM_JSON_TRY(onlyKnown(value, path,
                                   {"id", "actor", "position", "facing", "animation", "scale", "visible", "walk_speed"}));
        std::string id;
        SCHUIMM_JSON_TRY(readUniqueIdentifier(value, path, ids, id));
        const Json* field = nullptr;
        std::optional<AssetPath> actor;
        SCHUIMM_JSON_TRY(required(value, path, "actor", field));
        SCHUIMM_JSON_TRY(readAssetPath(*field, member(path, "actor"), actor));
        ActorPlacement placement{std::move(id), std::move(*actor), {}, {}, {}, {}, {}, {}};
        SCHUIMM_JSON_TRY(required(value, path, "position", field));
        SCHUIMM_JSON_TRY(readPoint(*field, member(path, "position"), placement.position));
        if (const Json* facing = optional(value, "facing")) {
            std::string name;
            const std::string facingPath = member(path, "facing");
            SCHUIMM_JSON_TRY(readString(*facing, facingPath, name));
            placement.facing = parseFacing(name);
            if (!placement.facing) {
                return fail(facingPath, concat("unknown facing '", name,
                                               "' (north, north_east, east, south_east, south, south_west, west, north_west)"));
            }
        }
        if (const Json* animation = optional(value, "animation")) {
            std::string name;
            SCHUIMM_JSON_TRY(readIdentifier(*animation, member(path, "animation"), name));
            placement.animation = std::move(name);
        }
        if (const Json* scale = optional(value, "scale")) {
            float stored = 1.0f;
            SCHUIMM_JSON_TRY(readFloatInRange(*scale, member(path, "scale"), minActorScale, maxActorScale, stored));
            placement.scale = stored;
        }
        if (const Json* speed = optional(value, "walk_speed")) {
            float stored = 1.0f;
            SCHUIMM_JSON_TRY(readFloatInRange(*speed, member(path, "walk_speed"), minWalkSpeed, maxWalkSpeed, stored));
            placement.walkSpeed = stored;
        }
        if (const Json* visible = optional(value, "visible")) {
            bool stored = true;
            SCHUIMM_JSON_TRY(readBool(*visible, member(path, "visible"), stored));
            placement.visible = stored;
        }
        out.push_back(std::move(placement));
        return {};
    }

    Status readRegion(const Json& value, const std::string& path, IdMap& ids, std::vector<RoomRegion>& out) const
    {
        SCHUIMM_JSON_TRY(readJsonObject(value, path));
        RoomRegion region;
        const Json* field = nullptr;
        std::string type;
        SCHUIMM_JSON_TRY(required(value, path, "type", field));
        SCHUIMM_JSON_TRY(readString(*field, member(path, "type"), type));
        if (type == "rectangle") {
            SCHUIMM_JSON_TRY(onlyKnown(value, path, {"id", "type", "position", "size"}));
            SCHUIMM_JSON_TRY(readUniqueIdentifier(value, path, ids, region.id));
            region.shape = RoomRegion::Shape::Rectangle;
            Vec2 position;
            Vec2 size;
            SCHUIMM_JSON_TRY(required(value, path, "position", field));
            SCHUIMM_JSON_TRY(readPoint(*field, member(path, "position"), position));
            SCHUIMM_JSON_TRY(required(value, path, "size", field));
            SCHUIMM_JSON_TRY(readPoint(*field, member(path, "size"), size));
            if (!(size.x > 0.0f) || !(size.y > 0.0f)) {
                return fail(member(path, "size"), concat("width and height must be positive, found [", size.x, ", ",
                                                         size.y, "]"));
            }
            region.rectangle = RectF{position.x, position.y, size.x, size.y};
        } else if (type == "polygon") {
            SCHUIMM_JSON_TRY(onlyKnown(value, path, {"id", "type", "points"}));
            SCHUIMM_JSON_TRY(readUniqueIdentifier(value, path, ids, region.id));
            region.shape = RoomRegion::Shape::Polygon;
            SCHUIMM_JSON_TRY(required(value, path, "points", field));
            SCHUIMM_JSON_TRY(readPolygon(*field, member(path, "points"), region.points));
        } else {
            return fail(member(path, "type"),
                        concat("unsupported region type '", type, "' (supported: rectangle, polygon)"));
        }
        out.push_back(std::move(region));
        return {};
    }

};

} // namespace

bool isValidRoomIdentifier(std::string_view text) noexcept
{
    return json::isValidIdentifier(text);
}

Result<RoomResource> decodeRoomResource(std::span<const std::byte> data, std::string_view sourceName)
{
    const Decoder decoder(sourceName);
    return json::decodeDocument<RoomResource>(decoder, data, [&decoder](const Json& root) { return decoder.decodeRoot(root); });
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
    if (room.script) SCHUIMM_JSON_TRY(check(*room.script, "script"));
    if (room.background) SCHUIMM_JSON_TRY(check(*room.background, "background.image"));
    for (std::size_t i = 0; i < room.props.size(); ++i) {
        SCHUIMM_JSON_TRY(check(room.props[i].sprite, member(element("props", i), "sprite")));
    }
    for (std::size_t i = 0; i < room.objects.size(); ++i) {
        if (room.objects[i].sprite) {
            SCHUIMM_JSON_TRY(check(*room.objects[i].sprite, member(element("objects", i), "sprite")));
        }
    }
    for (std::size_t i = 0; i < room.actors.size(); ++i) {
        SCHUIMM_JSON_TRY(check(room.actors[i].actor, member(element("actors", i), "actor")));
    }
    return {};
}

Result<RoomResource> loadRoomResource(const AssetLocator& locator, const AssetPath& asset)
{
    auto bytes = locator.readBytes(asset, maxRoomFileBytes);
    if (!bytes) return std::unexpected(bytes.error());
    auto room = decodeRoomResource(*bytes, asset.view());
    if (!room) return room;
    SCHUIMM_JSON_TRY(verifyRoomAssets(*room, locator, asset.view()));
    return room;
}

} // namespace schuimm::room
