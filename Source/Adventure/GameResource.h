#pragma once

// The game resource (*.game.json), format version 1: the minimal description of a game's
// scripting environment.
//
//   { "version": 1,
//     "id": "kitchen_demo",
//     "program": "Scripts/Game.schb",
//     "global_scripts": ["Scripts/Global.sch", "Scripts/Interface.sch"],
//     "rooms": ["Rooms/Kitchen/Kitchen.room.json", "Rooms/Garden/Garden.room.json"] }
//
//   * program: the game's compiled program (.schb), which holds every script of the game. The
//     runtime loads it; the build writes it.
//   * global_scripts: script sources (.sch) with game-wide declarations only. Room scripts and
//     actor scripts are found through the rooms' resources (their "script" and their actor
//     placements' actor resources), so they are not listed here.
//   * rooms: the room resources (.room.json) of the game, in a fixed order (the build compiles
//     their scripts; the runtime only enters these rooms).
//   * No duplicates in either list. At least one room.
//
// Source level and runtime use the same file: the build reads the sources (global_scripts and,
// through rooms, the room and actor scripts) and writes `program`; the shipped runtime reads
// `program` and `rooms` only and never compiles anything. Unknown properties and other versions
// are errors ("<source>: <path>: <reason>", InvalidData).

#include "Core/Common/Error.h"
#include "Resource/AssetLocator.h"
#include "Resource/AssetPath.h"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace schuimm::adventure {

inline constexpr int gameResourceVersion = 1;

struct GameResource {
    std::string id;
    AssetPath program;
    std::vector<AssetPath> globalScripts;
    std::vector<AssetPath> rooms;

    [[nodiscard]] bool hasRoom(const AssetPath& room) const noexcept;
};

[[nodiscard]] Result<GameResource> decodeGameResource(std::span<const std::byte> data, std::string_view sourceName);
// Reads and decodes `asset` (at most 1 MiB).
[[nodiscard]] Result<GameResource> loadGameResource(const AssetLocator& locator, const AssetPath& asset);

} // namespace schuimm::adventure
