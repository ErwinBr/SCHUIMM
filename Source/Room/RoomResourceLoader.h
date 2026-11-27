#pragma once

// Reading room resource files. Parsing and validation (decodeRoomResource) are separate from
// checking the referenced assets on disk (verifyRoomAssets); nothing here decodes images,
// touches the GPU or loads the script program (see RoomScript.h).
//
// Errors are InvalidData (or the locator's NotFound/IoFailure for missing files) with messages
// of the form "<source>: <property path>: <reason>", e.g.
//   Rooms/Kitchen/Kitchen.room.json: objects[1].id: duplicate identifier 'blender' (first used by objects[0])

#include "Core/Common/Error.h"
#include "Resource/AssetLocator.h"
#include "Resource/AssetPath.h"
#include "Room/RoomResource.h"

#include <cstddef>
#include <span>
#include <string_view>

namespace schuimm::room {

// Parses and validates a room resource (UTF-8 JSON). `sourceName` names it in error messages.
[[nodiscard]] Result<RoomResource> decodeRoomResource(std::span<const std::byte> data, std::string_view sourceName);

// Checks that the background and every sprite exist as regular files below the asset root. Fails
// with the first missing one. The script is a source path, not a shipped file: the game program
// holds its compiled form.
[[nodiscard]] Status verifyRoomAssets(const RoomResource& room, const AssetLocator& locator, std::string_view sourceName);

// Reads, decodes and verifies `asset`: the loader for ResourceCache<RoomResource>.
[[nodiscard]] Result<RoomResource> loadRoomResource(const AssetLocator& locator, const AssetPath& asset);

// Whether `text` is a valid room identifier: the script identifier rule [A-Za-z_][A-Za-z0-9_]*,
// so that object ids can match "object <id>:" declarations.
[[nodiscard]] bool isValidRoomIdentifier(std::string_view text) noexcept;

} // namespace schuimm::room
