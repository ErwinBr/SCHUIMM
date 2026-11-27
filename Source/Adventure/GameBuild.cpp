#include "Adventure/GameBuild.h"

#include "Adventure/AdventureInterface.h"
#include "Core/Common/StringBuilder.h"
#include "Room/ActorLibrary.h"
#include "Room/RoomResourceLoader.h"

#include <map>
#include <string>

namespace schuimm::adventure {

namespace {

constexpr std::uint64_t maxSourceBytes = 16u << 20;

const char* roleName(script::ScriptRole role)
{
    switch (role) {
    case script::ScriptRole::Global: return "global script";
    case script::ScriptRole::Room: return "room script";
    case script::ScriptRole::Actor: return "actor script";
    }
    return "script";
}

} // namespace

Result<GameSources> discoverGameSources(const AssetLocator& locator, const AssetPath& game)
{
    auto resource = loadGameResource(locator, game);
    if (!resource) return std::unexpected(resource.error());
    GameSources out{std::move(*resource), {}, {}};

    // Path -> role, and who first referred to it (for messages).
    std::map<std::string, std::pair<script::ScriptRole, std::string>, std::less<>> roles;
    const auto add = [&](const AssetPath& path, script::ScriptRole role, const std::string& by) -> Status {
        if (const auto existing = roles.find(path.view()); existing != roles.end()) {
            if (existing->second.first != role) {
                return makeError(ErrorCode::InvalidData,
                                 concat(by, ": '", path.view(), "' is used as a ", roleName(role), ", but ",
                                        existing->second.second, " uses it as a ", roleName(existing->second.first)));
            }
            return {}; // Shared: it takes part once.
        }
        auto bytes = locator.readBytes(path, maxSourceBytes);
        if (!bytes) {
            return makeError(bytes.error().code, concat(by, ": script '", path.view(), "': ", bytes.error().message));
        }
        roles.emplace(std::string(path.view()), std::make_pair(role, by));
        out.files.push_back(std::make_unique<script::SourceFile>(
            std::string(path.view()), std::string(reinterpret_cast<const char*>(bytes->data()), bytes->size()),
            static_cast<std::uint32_t>(out.files.size())));
        out.sources.push_back({out.files.back().get(), role});
        return {};
    };

    const std::string gameName(game.view());
    for (const AssetPath& script : out.game.globalScripts) {
        if (auto status = add(script, script::ScriptRole::Global, gameName); !status) return std::unexpected(status.error());
    }
    room::ActorLibrary actors(locator);
    for (const AssetPath& roomPath : out.game.rooms) {
        auto room = room::loadRoomResource(locator, roomPath);
        if (!room) return makeError(room.error().code, concat(gameName, ": ", room.error().message));
        const std::string roomName(roomPath.view());
        if (room->script) {
            if (auto status = add(*room->script, script::ScriptRole::Room, roomName); !status) {
                return std::unexpected(status.error());
            }
        }
        for (std::size_t i = 0; i < room->actors.size(); ++i) {
            const room::ActorPlacement& placement = room->actors[i];
            auto actor = actors.load(placement.actor);
            if (!actor) {
                return makeError(actor.error().code, concat(roomName, ": actors[", i, "] ('", placement.id, "'): ",
                                                            actor.error().message));
            }
            if ((*actor)->script) {
                if (auto status = add(*(*actor)->script, script::ScriptRole::Actor, std::string(placement.actor.view()));
                    !status) {
                    return std::unexpected(status.error());
                }
            }
        }
    }
    return out;
}

Result<std::unique_ptr<GameBuild>> compileGame(const AssetLocator& locator, const AssetPath& game, bool optimize)
{
    auto sources = discoverGameSources(locator, game);
    if (!sources) return std::unexpected(sources.error());
    auto build = std::unique_ptr<GameBuild>(new GameBuild{std::move(*sources), {}});
    script::CompileOptions options;
    options.optimize = optimize;
    options.interfaces = {&adventureInterface()};
    build->result = script::compileProject(build->sources.sources, options);
    return build;
}

} // namespace schuimm::adventure
