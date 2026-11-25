// Room resources: JSON parsing and validation, asset verification, compiled script association
// and room instances (object handles, state, coroutines).

#include "ScriptTestSupport.h"
#include "TestAssets.h"
#include "TestFramework.h"

#include "Core/Logging/Logger.h"
#include "Platform/FileSystem/FileSystem.h"
#include "Platform/FileSystem/Path.h"
#include "Room/RoomInstance.h"
#include "Room/RoomLibrary.h"
#include "Room/RoomResourceLoader.h"
#include "Room/RoomScript.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using namespace schuimm;
using namespace schuimm::room;
using Json = nlohmann::json;

namespace {

constexpr std::string_view roomName = "Rooms/Kitchen/Kitchen.room.json";

std::span<const std::byte> bytesOf(std::string_view text)
{
    return {reinterpret_cast<const std::byte*>(text.data()), text.size()};
}

Result<RoomResource> decode(std::string_view text)
{
    return decodeRoomResource(bytesOf(text), roomName);
}

// The full example room of Documentation/Architecture/Rooms.md.
const char* const fullRoom = R"({
    "version": 1,
    "id": "kitchen",
    "size": { "width": 2560, "height": 1440 },
    "script": "Rooms/Kitchen/Kitchen.schb",
    "background": { "image": "Rooms/Kitchen/Assets/background.png" },
    "props": [
        { "id": "table", "sprite": "Rooms/Kitchen/Assets/table.png", "position": [800, 500], "pivot": [0, 0] }
    ],
    "objects": [
        { "id": "blender", "sprite": "Rooms/Kitchen/Assets/blender.png", "position": [420, 310], "pivot": [16, 40] },
        { "id": "cupboard", "sprite": "Rooms/Kitchen/Assets/cupboard.png", "position": [720.5, 280] }
    ],
    "scale_map": {
        "type": "linear",
        "points": [ { "y": 200, "scale": 0.55 }, { "y": 1100, "scale": 1.0 } ]
    },
    "navigation": {
        "walkable_areas": [
            { "id": "floor", "points": [ [100, 800], [2400, 800], [2400, 1350], [100, 1350] ] }
        ]
    },
    "regions": [
        { "id": "kitchen_exit", "type": "rectangle", "position": [2300, 700], "size": [200, 400] },
        { "id": "window", "type": "polygon", "points": [ [10, 10], [60, 10], [35, 50] ] }
    ]
})";

const char* const minimalRoom = R"({ "version": 1, "id": "hall", "size": { "width": 640, "height": 360 } })";

// fullRoom with `change` applied, as JSON text.
std::string mutated(const std::function<void(Json&)>& change)
{
    Json json = Json::parse(fullRoom);
    change(json);
    return json.dump();
}

// The error message of decoding `text`; reports unexpected success.
std::string errorOf(std::string_view text)
{
    auto room = decode(text);
    if (room) {
        std::fprintf(stderr, "  unexpectedly valid: %.*s\n", static_cast<int>(text.size()), text.data());
        return "<valid>";
    }
    CHECK(room.error().code == ErrorCode::InvalidData);
    return room.error().message;
}

bool contains(const std::string& text, std::string_view part)
{
    if (text.find(part) != std::string::npos) return true;
    std::fprintf(stderr, "  '%s'\n  does not contain '%.*s'\n", text.c_str(), static_cast<int>(part.size()), part.data());
    return false;
}

#define CHECK_ERROR(text, expected) CHECK(contains(errorOf(text), expected))

// The milestone's example script, with the extern declaration the language requires and tabs.
const char* const kitchenScript =
    "extern func say(text: string)\n"
    "\n"
    "func enter():\n"
    "\tprint(\"Entering the kitchen\")\n"
    "\n"
    "object blender:\n"
    "\tfunc look_at():\n"
    "\t\tsay(\"An old blender.\")\n"
    "\n"
    "object cupboard:\n"
    "\tfunc look_at():\n"
    "\t\tsay(\"A wooden cupboard.\")\n";

// Natives of the fixture's host that test scripts may use.
const char* const testPrelude =
    "extern func say(text: string)\n"
    "extern func record_self(o: object)\n";

std::vector<std::byte> compileToBytecode(std::string source)
{
    if (source.find("extern func say(") == std::string::npos) source = testPrelude + source;
    script::test::Compiled compiled = script::test::compileSource(source, true, "Kitchen.sch");
    if (!compiled.result.success) std::fprintf(stderr, "%s", compiled.diagnostics().c_str());
    REQUIRE(compiled.result.success);
    auto bytes = script::writeBytecode(compiled.result.module);
    REQUIRE(bytes.has_value());
    return std::move(*bytes);
}

// An asset root in the build tree with a room, its images and (optionally) its compiled script,
// plus a ScriptHost with the natives room scripts use (say records its text).
struct RoomFixture {
    std::string root = test::makeScratchDirectory("room");
    Logger logger{LogLevel::Error};
    script::ScriptHost host{logger};
    std::vector<std::string> said;
    std::vector<script::ObjectRef> recorded;

    RoomFixture()
    {
        REQUIRE(host.natives()
                    .add({"say", {script::ScriptType::String}, script::ScriptType::None,
                          [this](script::NativeContext&, std::span<const script::Value> arguments) {
                              said.emplace_back(arguments[0].asString());
                              return script::NativeResult::returnValue();
                          },
                          {}})
                    .has_value());
        REQUIRE(host.natives()
                    .add({"record_self", {script::ScriptType::Object}, script::ScriptType::None,
                          [this](script::NativeContext&, std::span<const script::Value> arguments) {
                              recorded.push_back(arguments[0].asObject());
                              return script::NativeResult::returnValue();
                          },
                          {}})
                    .has_value());
    }

    void writeText(std::string_view asset, std::string_view text) const { writeBytes(asset, bytesOf(text)); }

    void writeBytes(std::string_view asset, std::span<const std::byte> bytes) const
    {
        const std::string file = path::join(root, asset);
        REQUIRE(fs::createDirectories(path::parent(file)).has_value());
        REQUIRE(fs::writeFile(file, bytes).has_value());
    }

    void writeImages() const
    {
        auto png = fs::readFile(path::join(test::testAssetRoot(), "Images/rgba_4x2.png"));
        REQUIRE(png.has_value());
        for (const char* name : {"background", "table", "blender", "cupboard"}) {
            writeBytes(concat("Rooms/Kitchen/Assets/", name, ".png"), *png);
        }
    }

    void writeScript(const std::string& source, std::string_view asset = "Rooms/Kitchen/Kitchen.schb") const
    {
        writeBytes(asset, compileToBytecode(source));
    }

    // The full room with its images and `source` as its compiled script.
    void writeFullRoom(const std::string& source = kitchenScript) const
    {
        writeImages();
        writeScript(source);
        writeText(roomName, fullRoom);
    }

    [[nodiscard]] AssetLocator locator() const { return AssetLocator::create(root).value(); }
};

} // namespace

// --- JSON parsing -------------------------------------------------------------------------------

SCHUIMM_TEST(Room_MinimalRoom)
{
    auto room = decode(minimalRoom);
    if (!room) std::fprintf(stderr, "  %s\n", room.error().message.c_str());
    REQUIRE(room.has_value());
    CHECK_EQ(room->id, std::string("hall"));
    CHECK(room->size == (Extent2D{640, 360}));
    CHECK(!room->script.has_value());
    CHECK(!room->background.has_value());
    CHECK(room->props.empty());
    CHECK(room->objects.empty());
    CHECK(!room->scaleMap.has_value());
    CHECK(room->walkableAreas.empty());
    CHECK(room->regions.empty());
}

SCHUIMM_TEST(Room_FullRoom)
{
    auto room = decode(fullRoom);
    if (!room) std::fprintf(stderr, "  %s\n", room.error().message.c_str());
    REQUIRE(room.has_value());
    CHECK_EQ(room->id, std::string("kitchen"));
    CHECK(room->size == (Extent2D{2560, 1440}));
    REQUIRE(room->script.has_value());
    CHECK_EQ(room->script->string(), std::string("Rooms/Kitchen/Kitchen.schb"));
    REQUIRE(room->background.has_value());
    CHECK_EQ(room->background->string(), std::string("Rooms/Kitchen/Assets/background.png"));
    REQUIRE(room->props.size() == 1);
    CHECK_EQ(room->props[0].id, std::string("table"));
    CHECK(room->props[0].position == (Vec2{800.0f, 500.0f}));
    REQUIRE(room->objects.size() == 2);
    CHECK_EQ(room->objects[0].id, std::string("blender"));
    CHECK(room->objects[0].pivot == (Vec2{16.0f, 40.0f}));
    CHECK(room->objects[1].position == (Vec2{720.5f, 280.0f}));
    CHECK(room->objects[1].pivot == (Vec2{0.0f, 0.0f})); // Default.
    CHECK(room->findObject("cupboard") == &room->objects[1]);
    CHECK(room->findObject("table") == nullptr); // A prop, not an object.
    REQUIRE(room->scaleMap.has_value());
    REQUIRE(room->scaleMap->points.size() == 2);
    CHECK(room->scaleMap->points[0] == (ScalePoint{200.0f, 0.55f}));
    CHECK(room->scaleMap->points[1] == (ScalePoint{1100.0f, 1.0f}));
    REQUIRE(room->walkableAreas.size() == 1);
    CHECK_EQ(room->walkableAreas[0].id, std::string("floor"));
    CHECK_EQ(room->walkableAreas[0].points.size(), std::size_t{4});
    REQUIRE(room->regions.size() == 2);
    CHECK(room->regions[0].shape == RoomRegion::Shape::Rectangle);
    CHECK(room->regions[0].rectangle == (RectF{2300.0f, 700.0f, 200.0f, 400.0f}));
    CHECK(room->regions[1].shape == RoomRegion::Shape::Polygon);
    CHECK_EQ(room->regions[1].points.size(), std::size_t{3});
}

SCHUIMM_TEST(Room_OptionalPropertiesMayBeNull)
{
    auto room = decode(mutated([](Json& j) {
        j["script"] = nullptr;
        j["background"] = nullptr;
        j["scale_map"] = nullptr;
        j["objects"][0]["sprite"] = nullptr; // An invisible object (hotspot).
        j["objects"][0]["pivot"] = nullptr;
    }));
    REQUIRE(room.has_value());
    CHECK(!room->script.has_value());
    CHECK(!room->background.has_value());
    CHECK(!room->scaleMap.has_value());
    CHECK(!room->objects[0].sprite.has_value());
    CHECK(room->objects[0].pivot == (Vec2{0.0f, 0.0f}));
}

SCHUIMM_TEST(Room_VersionAndRequiredFields)
{
    CHECK_ERROR(mutated([](Json& j) { j["version"] = 2; }),
                "Kitchen.room.json: version: unsupported room resource version 2 (this engine reads version 1)");
    CHECK_ERROR(mutated([](Json& j) { j["version"] = "1"; }), "version: expected an integer, found a string");
    CHECK_ERROR(mutated([](Json& j) { j["version"] = 1.0; }), "version: expected an integer, found the number 1.0");
    // The version is checked before anything else, so a future file reports its version.
    CHECK_ERROR(mutated([](Json& j) {
                    j["version"] = 3;
                    j["foreground"] = Json::object();
                }),
                "unsupported room resource version 3");
    CHECK_ERROR(mutated([](Json& j) { j.erase("version"); }), "version: missing required property");
    CHECK_ERROR(mutated([](Json& j) { j.erase("id"); }), "Kitchen.room.json: id: missing required property");
    CHECK_ERROR(mutated([](Json& j) { j.erase("size"); }), "size: missing required property");
    CHECK_ERROR(mutated([](Json& j) { j["size"].erase("height"); }), "size.height: missing required property");
    CHECK_ERROR(mutated([](Json& j) { j["objects"][1].erase("position"); }), "objects[1].position: missing required property");
    CHECK_ERROR(mutated([](Json& j) { j["props"][0].erase("sprite"); }), "props[0].sprite: missing required property");
    CHECK_ERROR(mutated([](Json& j) { j["background"].erase("image"); }), "background.image: missing required property");
    CHECK_ERROR(mutated([](Json& j) { j["regions"][0].erase("type"); }), "regions[0].type: missing required property");
}

SCHUIMM_TEST(Room_InvalidDimensions)
{
    CHECK_ERROR(mutated([](Json& j) { j["size"]["width"] = 0; }), "size.width: must be an integer from 1 to 65536, found 0");
    CHECK_ERROR(mutated([](Json& j) { j["size"]["height"] = -5; }), "size.height: must be an integer from 1 to 65536, found -5");
    CHECK_ERROR(mutated([](Json& j) { j["size"]["width"] = 70000; }), "found 70000");
    CHECK_ERROR(mutated([](Json& j) { j["size"]["width"] = 2.5; }), "size.width: expected an integer, found the number 2.5");
    CHECK_ERROR(mutated([](Json& j) { j["size"]["width"] = "wide"; }), "size.width: expected an integer, found a string");
    CHECK_ERROR(mutated([](Json& j) { j["size"]["width"] = 18446744073709551615ull; }), "size.width: must be an integer");
    CHECK_ERROR(mutated([](Json& j) { j["size"] = Json::array({2560, 1440}); }), "size: expected an object, found an array");
}

SCHUIMM_TEST(Room_Identifiers)
{
    CHECK_ERROR(mutated([](Json& j) { j["id"] = "1kitchen"; }), "id: invalid identifier '1kitchen'");
    CHECK_ERROR(mutated([](Json& j) { j["id"] = ""; }), "id: invalid identifier ''");
    CHECK_ERROR(mutated([](Json& j) { j["objects"][0]["id"] = "old blender"; }), "objects[0].id: invalid identifier 'old blender'");
    CHECK_ERROR(mutated([](Json& j) { j["objects"][0]["id"] = "blender-1"; }), "invalid identifier 'blender-1'");
    CHECK_ERROR(mutated([](Json& j) { j["objects"][0]["id"] = 7; }), "objects[0].id: expected a string, found the number 7");
    CHECK(isValidRoomIdentifier("_kitchen_2"));
    CHECK(!isValidRoomIdentifier("k\xC3\xBC"
                                 "che")); // Non-ASCII.
}

SCHUIMM_TEST(Room_DuplicateIdentifiers)
{
    CHECK_ERROR(mutated([](Json& j) { j["objects"][1]["id"] = "blender"; }),
                "Rooms/Kitchen/Kitchen.room.json: objects[1].id: duplicate identifier 'blender' (first used by objects[0])");
    CHECK_ERROR(mutated([](Json& j) { j["props"].push_back(j["props"][0]); }),
                "props[1].id: duplicate identifier 'table' (first used by props[0])");
    // Props and objects share one namespace.
    CHECK_ERROR(mutated([](Json& j) { j["objects"][0]["id"] = "table"; }),
                "objects[0].id: duplicate identifier 'table' (first used by props[0])");
    CHECK_ERROR(mutated([](Json& j) {
                    j["navigation"]["walkable_areas"].push_back(j["navigation"]["walkable_areas"][0]);
                }),
                "navigation.walkable_areas[1].id: duplicate identifier 'floor'");
    CHECK_ERROR(mutated([](Json& j) { j["regions"][1]["id"] = "kitchen_exit"; }),
                "regions[1].id: duplicate identifier 'kitchen_exit' (first used by regions[0])");
    // Regions and walkable areas have namespaces of their own.
    CHECK(decode(mutated([](Json& j) { j["regions"][1]["id"] = "blender"; })).has_value());
    CHECK(decode(mutated([](Json& j) { j["navigation"]["walkable_areas"][0]["id"] = "window"; })).has_value());
}

SCHUIMM_TEST(Room_ScaleMap)
{
    CHECK_ERROR(mutated([](Json& j) { j["scale_map"]["points"].erase(1); }),
                "scale_map.points: a scale map needs at least 2 points, found 1");
    CHECK_ERROR(mutated([](Json& j) { j["scale_map"]["points"][1]["y"] = 150; }),
                "scale_map.points[1].y: must be greater than the previous point's y (200)");
    CHECK_ERROR(mutated([](Json& j) { j["scale_map"]["points"][1]["y"] = 200; }), "scale_map.points[1].y: must be greater");
    CHECK_ERROR(mutated([](Json& j) { j["scale_map"]["points"][1]["y"] = 1500; }),
                "scale_map.points[1].y: must be within the room (0 to 1440), found 1500");
    CHECK_ERROR(mutated([](Json& j) { j["scale_map"]["points"][0]["y"] = -1; }), "scale_map.points[0].y: must be within the room");
    CHECK_ERROR(mutated([](Json& j) { j["scale_map"]["points"][0]["scale"] = 0; }),
                "scale_map.points[0].scale: must be a positive number, found 0");
    CHECK_ERROR(mutated([](Json& j) { j["scale_map"]["points"][0]["scale"] = -0.5; }), "scale_map.points[0].scale: must be a positive");
    CHECK_ERROR(mutated([](Json& j) { j["scale_map"]["points"][0]["scale"] = "big"; }), "scale_map.points[0].scale: expected a number");
    CHECK_ERROR(mutated([](Json& j) { j["scale_map"]["type"] = "curve"; }),
                "scale_map.type: unsupported scale map type 'curve' (supported: linear)");
    CHECK_ERROR(mutated([](Json& j) { j["scale_map"].erase("points"); }), "scale_map.points: missing required property");
    CHECK_ERROR(mutated([](Json& j) { j["scale_map"]["points"][0]["x"] = 1; }), "scale_map.points[0].x: unknown property");
}

SCHUIMM_TEST(Room_Polygons)
{
    const auto area = [](Json points) {
        return mutated([&](Json& j) { j["navigation"]["walkable_areas"][0]["points"] = points; });
    };
    const std::string path = "navigation.walkable_areas[0].points";
    CHECK_ERROR(area(Json::parse("[[0, 0], [10, 0]]")), path + ": a polygon needs at least 3 points, found 2");
    CHECK_ERROR(area(Json::parse("[[0, 0], [10, 0], [10, 0], [0, 10]]")), path + "[2]: repeats the previous point (10, 0)");
    CHECK_ERROR(area(Json::parse("[[0, 0], [10, 0], [0, 10], [0, 0]]")), path + "[3]: repeats the first point");
    CHECK_ERROR(area(Json::parse("[[0, 0], [5, 5], [10, 10]]")), path + ": polygon has zero area (all points lie on one line)");
    CHECK_ERROR(area(Json::parse("[[0, 0], [10, 0], [0, 1e7]]")), path + "[2][1]: coordinate 10000000.0 is out of range");
    CHECK_ERROR(area(Json::parse("[[0, 0], [10, 0, 3], [0, 10]]")), path + "[1]: expected a point [x, y], found an array");
    CHECK_ERROR(area(Json::parse("[[0, 0], [10, \"0\"], [0, 10]]")), path + "[1][1]: expected a number, found a string");
    CHECK_ERROR(area(Json::parse("{}")), path + ": expected an array, found an object");
    // Clockwise and counter-clockwise are both fine; negative coordinates are allowed.
    CHECK(decode(area(Json::parse("[[0, 0], [0, 10], [10, 0]]"))).has_value());
    CHECK(decode(area(Json::parse("[[-5, -5], [10, -5], [0, 10]]"))).has_value());
}

SCHUIMM_TEST(Room_Regions)
{
    CHECK_ERROR(mutated([](Json& j) { j["regions"][0]["size"] = Json::parse("[0, 400]"); }),
                "regions[0].size: width and height must be positive, found [0, 400]");
    CHECK_ERROR(mutated([](Json& j) { j["regions"][0]["size"] = Json::parse("[200, -1]"); }), "regions[0].size: width and height must be positive");
    CHECK_ERROR(mutated([](Json& j) { j["regions"][0]["type"] = "circle"; }),
                "regions[0].type: unsupported region type 'circle' (supported: rectangle, polygon)");
    CHECK_ERROR(mutated([](Json& j) { j["regions"][0]["points"] = Json::parse("[[0, 0], [1, 0], [0, 1]]"); }),
                "regions[0].points: unknown property");
    CHECK_ERROR(mutated([](Json& j) { j["regions"][1]["points"] = Json::parse("[[0, 0], [1, 1], [2, 2]]"); }),
                "regions[1].points: polygon has zero area");
    CHECK_ERROR(mutated([](Json& j) { j["regions"][0]["on_enter"] = "open_door"; }), "regions[0].on_enter: unknown property");
}

SCHUIMM_TEST(Room_AssetPaths)
{
    CHECK_ERROR(mutated([](Json& j) { j["objects"][0]["sprite"] = "../Shared/blender.png"; }),
                "objects[0].sprite: invalid asset path '../Shared/blender.png': leaves the asset root");
    CHECK_ERROR(mutated([](Json& j) { j["objects"][0]["sprite"] = "C:/blender.png"; }), "objects[0].sprite: invalid asset path 'C:/blender.png'");
    CHECK_ERROR(mutated([](Json& j) { j["props"][0]["sprite"] = "Rooms\\Kitchen\\table.png"; }), "props[0].sprite: invalid asset path");
    CHECK_ERROR(mutated([](Json& j) { j["background"]["image"] = "/Rooms/bg.png"; }),
                "background.image: invalid asset path '/Rooms/bg.png': must be relative to the asset root");
    CHECK_ERROR(mutated([](Json& j) { j["script"] = "Rooms/Kitchen/Kitchen.sch"; }),
                "script: must name a compiled script (.schb), found 'Rooms/Kitchen/Kitchen.sch'");
    CHECK_ERROR(mutated([](Json& j) { j["script"] = 5; }), "script: expected a string, found the number 5");
    // Asset paths are normalised like every other asset path.
    auto room = decode(mutated([](Json& j) { j["objects"][0]["sprite"] = "Rooms/./Kitchen//Assets/blender.png"; }));
    REQUIRE(room.has_value());
    CHECK_EQ(room->objects[0].sprite->string(), std::string("Rooms/Kitchen/Assets/blender.png"));
}

SCHUIMM_TEST(Room_MalformedJson)
{
    CHECK_ERROR("", "Kitchen.room.json: invalid JSON at line 1");
    CHECK_ERROR("{\n  \"version\": 1,\n  \"id\": tru\n}", "invalid JSON at line 3, column ");
    CHECK_ERROR("{ \"version\": 1, }", "invalid JSON at line 1");
    CHECK_ERROR(std::string(minimalRoom) + " {}", "invalid JSON");
    CHECK_ERROR("{ \"version\": 1, \"id\": \"k\xFF\" }", "invalid JSON"); // Not UTF-8.
    CHECK_ERROR("[1, 2]", "Kitchen.room.json: a room resource must be a JSON object, found an array");
    // Duplicate keys are rejected (a JSON object would silently keep the last one).
    CHECK_ERROR("{ \"version\": 1, \"version\": 1, \"id\": \"k\", \"size\": { \"width\": 1, \"height\": 1 } }",
                "Kitchen.room.json: version: duplicate property 'version'");
    std::string nested = fullRoom;
    nested.replace(nested.find("\"pivot\": [16, 40]"), 17, "\"id\": \"again\"");
    CHECK_ERROR(nested, "objects[0].id: duplicate property 'id'");
    // Unknown properties are errors (usually typos).
    CHECK_ERROR(mutated([](Json& j) { j["foreground"] = Json::object(); }), "Kitchen.room.json: foreground: unknown property");
    CHECK_ERROR(mutated([](Json& j) { j["objects"][1]["events"] = Json::array({"look_at"}); }), "objects[1].events: unknown property");
    CHECK_ERROR(mutated([](Json& j) { j["objects"][1]["sprtie"] = "x.png"; }), "objects[1].sprtie: unknown property");
}

// --- Asset references ---------------------------------------------------------------------------

SCHUIMM_TEST(Room_AssetsAreVerifiedWithoutDecoding)
{
    RoomFixture fixture;
    fixture.writeFullRoom();
    const AssetLocator locator = fixture.locator();
    auto room = loadRoomResource(locator, test::assetPath(roomName));
    if (!room) std::fprintf(stderr, "  %s\n", room.error().message.c_str());
    REQUIRE(room.has_value());

    // The verification is separate from parsing: a decoded room can name missing assets.
    auto decoded = decode(mutated([](Json& j) { j["objects"][1]["sprite"] = "Rooms/Kitchen/Assets/missing.png"; }));
    REQUIRE(decoded.has_value());
    auto status = verifyRoomAssets(*decoded, locator, roomName);
    REQUIRE(!status.has_value());
    CHECK(status.error().code == ErrorCode::NotFound);
    CHECK(contains(status.error().message, "Kitchen.room.json: objects[1].sprite: asset 'Rooms/Kitchen/Assets/missing.png' not found"));

    decoded = decode(mutated([](Json& j) { j["background"]["image"] = "Rooms/Kitchen/Assets/sky.png"; }));
    status = verifyRoomAssets(*decoded, locator, roomName);
    REQUIRE(!status.has_value());
    CHECK(contains(status.error().message, "background.image: asset 'Rooms/Kitchen/Assets/sky.png' not found"));

    decoded = decode(mutated([](Json& j) { j["props"][0]["sprite"] = "Rooms/Kitchen"; })); // A directory.
    status = verifyRoomAssets(*decoded, locator, roomName);
    REQUIRE(!status.has_value());
    CHECK(contains(status.error().message, "props[0].sprite: asset 'Rooms/Kitchen' is not a regular file"));

    // Images are only checked for existence, not decoded: a file that is not an image passes.
    fixture.writeText("Rooms/Kitchen/Assets/table.png", "not a png");
    CHECK(loadRoomResource(locator, test::assetPath(roomName)).has_value());

    auto missing = loadRoomResource(locator, test::assetPath("Rooms/Attic/Attic.room.json"));
    REQUIRE(!missing.has_value());
    CHECK(missing.error().code == ErrorCode::NotFound);
}

// --- Script association -------------------------------------------------------------------------

SCHUIMM_TEST(Room_RoomWithoutScript)
{
    RoomFixture fixture;
    fixture.writeImages();
    fixture.writeText(roomName, mutated([](Json& j) { j.erase("script"); }));
    const AssetLocator locator = fixture.locator();
    RoomLibrary library(locator, fixture.host);
    auto loaded = library.load(test::assetPath(roomName));
    REQUIRE(loaded.has_value());
    CHECK(loaded->script == nullptr);
    auto instance = RoomInstance::create(*loaded, fixture.host);
    REQUIRE(instance.has_value());
    auto result = (*instance)->dispatch("blender", "look_at");
    REQUIRE(result.has_value());
    CHECK(!result->handled);
    auto started = (*instance)->start("enter");
    REQUIRE(!started.has_value());
    CHECK(started.error().code == ErrorCode::NotFound);
}

SCHUIMM_TEST(Room_CompletionCriteria)
{
    // The milestone's example: the room and its compiled script load, the instance gets its own
    // object handles, and handlers run through ScriptHost with those handles as self.
    RoomFixture fixture;
    fixture.writeScript(kitchenScript);
    fixture.writeImages();
    fixture.writeText(roomName, R"({
        "version": 1,
        "id": "kitchen",
        "size": { "width": 2560, "height": 1440 },
        "script": "Rooms/Kitchen/Kitchen.schb",
        "objects": [
            { "id": "blender", "sprite": "Rooms/Kitchen/Assets/blender.png", "position": [420, 310] },
            { "id": "cupboard", "sprite": "Rooms/Kitchen/Assets/cupboard.png", "position": [720, 280] }
        ]
    })");
    const AssetLocator locator = fixture.locator();
    RoomLibrary library(locator, fixture.host);
    auto loaded = library.load(test::assetPath(roomName));
    if (!loaded) std::fprintf(stderr, "  %s\n", loaded.error().message.c_str());
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->script != nullptr);
    CHECK_EQ(loaded->script->name(), std::string("Rooms/Kitchen/Kitchen.schb"));
    CHECK(loaded->script->hasObject("blender"));

    auto instance = RoomInstance::create(*loaded, fixture.host);
    REQUIRE(instance.has_value());
    RoomInstance& room = **instance;
    REQUIRE(room.objects().size() == 2);
    const RoomObject* blender = room.object("blender");
    const RoomObject* cupboard = room.object("cupboard");
    REQUIRE(blender != nullptr);
    REQUIRE(cupboard != nullptr);
    CHECK(!blender->handle.isNull());
    CHECK(!cupboard->handle.isNull());
    CHECK(!(blender->handle == cupboard->handle));
    CHECK(room.object(blender->handle) == blender);
    CHECK(blender->position == (Vec2{420.0f, 310.0f}));

    REQUIRE(room.dispatch("cupboard", "look_at")->handled);
    REQUIRE(room.dispatch("blender", "look_at")->handled);
    REQUIRE(room.start("enter").has_value());
    fixture.host.update(0.0);
    REQUIRE(fixture.said.size() == 2);
    CHECK_EQ(fixture.said[0], std::string("A wooden cupboard."));
    CHECK_EQ(fixture.said[1], std::string("An old blender."));
    CHECK_EQ(fixture.host.statistics().failed, std::uint64_t{0});
}

SCHUIMM_TEST(Room_ObjectsWithoutDeclarationsAreFine)
{
    RoomFixture fixture;
    fixture.writeFullRoom("object blender:\n\tfunc look_at():\n\t\tsay(\"An old blender.\")\n");
    const AssetLocator locator = fixture.locator();
    RoomLibrary library(locator, fixture.host);
    auto loaded = library.load(test::assetPath(roomName));
    REQUIRE(loaded.has_value());
    auto instance = RoomInstance::create(*loaded, fixture.host);
    REQUIRE(instance.has_value());
    // The cupboard has no declaration: its events have no script behaviour.
    auto result = (*instance)->dispatch("cupboard", "look_at");
    REQUIRE(result.has_value());
    CHECK(!result->handled);
    // A missing handler of a declared object is not an error either.
    result = (*instance)->dispatch("blender", "use");
    REQUIRE(result.has_value());
    CHECK(!result->handled);
    // An object the room does not have is.
    result = (*instance)->dispatch("fridge", "look_at");
    REQUIRE(!result.has_value());
    CHECK(result.error().code == ErrorCode::NotFound);
    CHECK(contains(result.error().message, "room 'kitchen' has no object 'fridge'"));
}

SCHUIMM_TEST(Room_ScriptObjectWithoutRoomObjectIsAnError)
{
    RoomFixture fixture;
    fixture.writeFullRoom("object blendr:\n\tfunc look_at():\n\t\tsay(\"typo\")\n");
    const AssetLocator locator = fixture.locator();
    RoomLibrary library(locator, fixture.host);
    auto loaded = library.load(test::assetPath(roomName));
    REQUIRE(!loaded.has_value());
    CHECK(loaded.error().code == ErrorCode::InvalidData);
    CHECK(contains(loaded.error().message, "Rooms/Kitchen/Kitchen.room.json: script 'Rooms/Kitchen/Kitchen.schb' declares "
                                           "object 'blendr', which room 'kitchen' does not define"));
    // RoomInstance::create checks again, so a mismatched pair cannot be assembled by hand.
    auto resource = decode(fullRoom);
    REQUIRE(resource.has_value());
    auto program = fixture.host.load(compileToBytecode("object fridge:\n\tfunc open():\n\t\tsay(\"cold\")\n"), "fridge.schb");
    REQUIRE(program.has_value());
    auto instance = RoomInstance::create(LoadedRoom{std::make_shared<const RoomResource>(std::move(*resource)), *program},
                                         fixture.host);
    REQUIRE(!instance.has_value());
    CHECK(contains(instance.error().message, "declares object 'fridge', which room 'kitchen' does not define"));
}

SCHUIMM_TEST(Room_InvalidOrMissingScript)
{
    RoomFixture fixture;
    fixture.writeImages();
    fixture.writeText(roomName, fullRoom);
    const AssetLocator locator = fixture.locator();
    RoomLibrary library(locator, fixture.host);
    // Missing .schb: found by the asset verification.
    auto loaded = library.load(test::assetPath(roomName));
    REQUIRE(!loaded.has_value());
    CHECK(loaded.error().code == ErrorCode::NotFound);
    CHECK(contains(loaded.error().message, "Kitchen.room.json: script: asset 'Rooms/Kitchen/Kitchen.schb' not found"));
    // Not bytecode at all.
    fixture.writeText("Rooms/Kitchen/Kitchen.schb", "func enter():\n\tpass\n");
    loaded = library.load(test::assetPath(roomName));
    REQUIRE(!loaded.has_value());
    CHECK(contains(loaded.error().message, "Kitchen.room.json: script 'Rooms/Kitchen/Kitchen.schb' cannot be loaded"));
    // Corrupted bytecode (checksum).
    std::vector<std::byte> bytes = compileToBytecode(kitchenScript);
    bytes[bytes.size() / 2] ^= std::byte{0x40};
    fixture.writeBytes("Rooms/Kitchen/Kitchen.schb", bytes);
    loaded = library.load(test::assetPath(roomName));
    REQUIRE(!loaded.has_value());
    CHECK(contains(loaded.error().message, "cannot be loaded"));
    // A script calling a native the engine does not provide fails to link.
    fixture.writeScript("extern func open_fridge()\nobject blender:\n\tfunc use():\n\t\topen_fridge()\n");
    loaded = library.load(test::assetPath(roomName));
    REQUIRE(!loaded.has_value());
    CHECK(contains(loaded.error().message, "open_fridge"));
    CHECK_EQ(library.scriptCount(), std::size_t{0}); // Failures are not cached.
}

SCHUIMM_TEST(Room_InstancesShareTheResourceAndProgram)
{
    RoomFixture fixture;
    fixture.writeFullRoom();
    const AssetLocator locator = fixture.locator();
    RoomLibrary library(locator, fixture.host);
    auto first = library.load(test::assetPath(roomName));
    auto second = library.load(test::assetPath(roomName));
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    CHECK(first->resource == second->resource);
    CHECK(first->script == second->script);
    CHECK_EQ(library.resourceCount(), std::size_t{1});
    CHECK_EQ(library.scriptCount(), std::size_t{1});

    auto a = RoomInstance::create(*first, fixture.host);
    auto b = RoomInstance::create(*second, fixture.host);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK((*a)->script() == (*b)->script());
    CHECK(&(*a)->resource() == &(*b)->resource());
    // Unloading the library drops its references only.
    library.unloadAll();
    CHECK_EQ(library.resourceCount(), std::size_t{0});
    CHECK_EQ((*a)->resource().id, std::string("kitchen"));
    REQUIRE((*a)->dispatch("blender", "look_at")->handled);
    fixture.host.update(0.0);
    CHECK_EQ(fixture.said.size(), std::size_t{1});
}

// --- Room instances -----------------------------------------------------------------------------

SCHUIMM_TEST(Room_InstancesHaveIndependentHandlesAndState)
{
    RoomFixture fixture;
    fixture.writeFullRoom();
    const AssetLocator locator = fixture.locator();
    RoomLibrary library(locator, fixture.host);
    auto loaded = library.load(test::assetPath(roomName));
    REQUIRE(loaded.has_value());
    auto a = RoomInstance::create(*loaded, fixture.host).value();
    auto b = RoomInstance::create(*loaded, fixture.host).value();

    std::vector<std::uint64_t> ids;
    for (const RoomInstance* instance : {a.get(), b.get()}) {
        for (const RoomObject& object : instance->objects()) {
            CHECK(!object.handle.isNull());
            CHECK(object.definition == instance->resource().findObject(object.id()));
            ids.push_back(object.handle.id);
        }
    }
    REQUIRE(ids.size() == 4);
    for (std::size_t i = 0; i < ids.size(); ++i) {
        for (std::size_t j = i + 1; j < ids.size(); ++j) CHECK(ids[i] != ids[j]);
    }
    // A handle belongs to one instance only.
    CHECK(b->object(a->object("blender")->handle) == nullptr);
    CHECK(a->object(script::ObjectRef{}) == nullptr);

    // State changes stay in the instance; the shared resource never changes.
    a->object("blender")->flags["is_running"] = true;
    a->object("blender")->position = Vec2{1.0f, 2.0f};
    a->object("cupboard")->visible = false;
    CHECK(b->object("blender")->flags.empty());
    CHECK(b->object("blender")->position == (Vec2{420.0f, 310.0f}));
    CHECK(b->object("cupboard")->visible);
    CHECK(a->resource().objects[0].position == (Vec2{420.0f, 310.0f}));
    CHECK(a->resource() == decode(fullRoom).value());

    // A newly created instance gets new handles, never ones used before.
    const std::uint64_t oldBlender = a->object("blender")->handle.id;
    a.reset();
    auto c = RoomInstance::create(*loaded, fixture.host).value();
    for (const RoomObject& object : c->objects()) CHECK(object.handle.id > oldBlender);
}

SCHUIMM_TEST(Room_DispatchPassesTheObjectHandleAsSelf)
{
    RoomFixture fixture;
    fixture.writeFullRoom("object blender:\n\tfunc look_at():\n\t\trecord_self(self)\n"
                          "object cupboard:\n\tfunc look_at():\n\t\trecord_self(self)\n");
    const AssetLocator locator = fixture.locator();
    RoomLibrary library(locator, fixture.host);
    auto loaded = library.load(test::assetPath(roomName));
    REQUIRE(loaded.has_value());
    auto a = RoomInstance::create(*loaded, fixture.host).value();
    auto b = RoomInstance::create(*loaded, fixture.host).value();
    REQUIRE(a->dispatch("blender", "look_at")->handled);
    REQUIRE(b->dispatch("blender", "look_at")->handled);
    REQUIRE(a->dispatch("cupboard", "look_at")->handled);
    fixture.host.update(0.0);
    REQUIRE(fixture.recorded.size() == 3);
    CHECK(fixture.recorded[0] == a->object("blender")->handle);
    CHECK(fixture.recorded[1] == b->object("blender")->handle);
    CHECK(fixture.recorded[2] == a->object("cupboard")->handle);
}

SCHUIMM_TEST(Room_DestroyingAnInstanceCancelsItsCoroutines)
{
    RoomFixture fixture;
    fixture.writeFullRoom("object blender:\n\tfunc use():\n\t\twait(2.0)\n\t\trecord_self(self)\n"
                          "object cupboard:\n\tfunc look_at():\n\t\tsay(\"A wooden cupboard.\")\n");
    const AssetLocator locator = fixture.locator();
    RoomLibrary library(locator, fixture.host);
    auto loaded = library.load(test::assetPath(roomName));
    REQUIRE(loaded.has_value());
    auto a = RoomInstance::create(*loaded, fixture.host).value();
    auto b = RoomInstance::create(*loaded, fixture.host).value();
    const script::ObjectRef survivor = b->object("blender")->handle;
    REQUIRE(a->dispatch("blender", "use")->handled);
    REQUIRE(b->dispatch("blender", "use")->handled);
    fixture.host.update(0.0); // Both handlers now wait.
    CHECK_EQ(a->activeCoroutineCount(), std::size_t{1});
    CHECK_EQ(b->activeCoroutineCount(), std::size_t{1});

    a.reset(); // Between frames: cancels a's handler, leaves b's alone.
    CHECK_EQ(fixture.host.statistics().cancelled, std::uint64_t{1});
    CHECK_EQ(fixture.host.activeCount(), std::size_t{1});
    for (int i = 0; i < 10; ++i) fixture.host.update(0.5);
    REQUIRE(fixture.recorded.size() == 1); // Only b's handler resumed.
    CHECK(fixture.recorded[0] == survivor);
    CHECK_EQ(b->activeCoroutineCount(), std::size_t{0});
    CHECK_EQ(fixture.host.statistics().failed, std::uint64_t{0});

    // Ended coroutines are forgotten; b can keep dispatching.
    REQUIRE(b->dispatch("cupboard", "look_at")->handled);
    fixture.host.update(0.0);
    CHECK_EQ(fixture.said.size(), std::size_t{1});
}

// --- Documentation ------------------------------------------------------------------------------

SCHUIMM_TEST(Room_DocumentedExampleIsValid)
{
    // Documentation/Rooms/Examples: the room resource and its script load together.
    const std::string examples = SCHUIMM_ROOM_EXAMPLES_DIR;
    auto json = fs::readFile(path::join(examples, "Kitchen.room.json"));
    auto source = fs::readFile(path::join(examples, "Kitchen.sch"));
    REQUIRE(json.has_value());
    REQUIRE(source.has_value());
    RoomFixture fixture;
    fixture.writeImages();
    fixture.writeBytes(roomName, *json);
    fixture.writeScript(std::string(reinterpret_cast<const char*>(source->data()), source->size()));
    const AssetLocator locator = fixture.locator();
    RoomLibrary library(locator, fixture.host);
    auto loaded = library.load(test::assetPath(roomName));
    if (!loaded) std::fprintf(stderr, "  %s\n", loaded.error().message.c_str());
    REQUIRE(loaded.has_value());
    auto instance = RoomInstance::create(*loaded, fixture.host);
    REQUIRE(instance.has_value());
    CHECK_EQ((*instance)->objects().size(), std::size_t{3});
    CHECK(!(*instance)->resource().objects[2].sprite.has_value()); // The window: an invisible object.
    CHECK(!(*instance)->dispatch("window", "look_at")->handled);   // No declaration: no handlers.
    REQUIRE((*instance)->dispatch("blender", "look_at")->handled);
    fixture.host.update(0.0);
    REQUIRE(fixture.said.size() == 1);
    CHECK_EQ(fixture.said[0], std::string("An old blender."));
}
