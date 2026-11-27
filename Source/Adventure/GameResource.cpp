#include "Adventure/GameResource.h"

#include "Core/Common/StringBuilder.h"
#include "Resource/Json/JsonDecoder.h"

#include <algorithm>
#include <optional>
#include <set>

namespace schuimm::adventure {

namespace {

using json::Json;
using json::element;

constexpr std::uint64_t maxGameFileBytes = 1u << 20;

class Decoder : public json::Decoder {
public:
    using json::Decoder::Decoder;

    Result<GameResource> decodeRoot(const Json& root) const
    {
        int version = 0;
        SCHUIMM_JSON_TRY(readVersion(root, "game resource", gameResourceVersion, gameResourceVersion, version));
        SCHUIMM_JSON_TRY(onlyKnown(root, "", {"version", "id", "program", "global_scripts", "rooms"}));
        std::string id;
        std::optional<AssetPath> program;
        std::vector<AssetPath> globalScripts;
        std::vector<AssetPath> rooms;
        const Json* value = nullptr;
        SCHUIMM_JSON_TRY(required(root, "", "id", value));
        SCHUIMM_JSON_TRY(readIdentifier(*value, "id", id));
        SCHUIMM_JSON_TRY(required(root, "", "program", value));
        SCHUIMM_JSON_TRY(readAssetPath(*value, "program", program));
        if (!program) return fail("program", "a game needs its compiled program");
        if (program->extension() != ".schb") {
            return fail("program", concat("must name a compiled program (.schb), found '", program->view(), "'"));
        }
        if (const Json* scripts = optional(root, "global_scripts")) {
            SCHUIMM_JSON_TRY(readPaths(*scripts, "global_scripts", ".sch", "a script source", globalScripts));
        }
        SCHUIMM_JSON_TRY(required(root, "", "rooms", value));
        SCHUIMM_JSON_TRY(readPaths(*value, "rooms", ".json", "a room resource", rooms));
        if (rooms.empty()) return fail("rooms", "a game has at least one room");
        return GameResource{std::move(id), std::move(*program), std::move(globalScripts), std::move(rooms)};
    }

private:
    Status readPaths(const Json& value, std::string_view path, std::string_view extension, std::string_view what,
                     std::vector<AssetPath>& out) const
    {
        SCHUIMM_JSON_TRY(readArray(value, path));
        std::set<std::string> seen;
        for (std::size_t i = 0; i < value.size(); ++i) {
            const std::string at = element(path, i);
            std::optional<AssetPath> asset;
            SCHUIMM_JSON_TRY(readAssetPath(value[i], at, asset));
            if (!asset) return fail(at, "expected an asset path, found null");
            if (asset->extension() != extension) {
                return fail(at, concat("must name ", what, " (", extension, "), found '", asset->view(), "'"));
            }
            if (!seen.insert(std::string(asset->view())).second) {
                return fail(at, concat("'", asset->view(), "' is listed twice"));
            }
            out.push_back(std::move(*asset));
        }
        return {};
    }
};

} // namespace

bool GameResource::hasRoom(const AssetPath& room) const noexcept
{
    return std::find(rooms.begin(), rooms.end(), room) != rooms.end();
}

Result<GameResource> decodeGameResource(std::span<const std::byte> data, std::string_view sourceName)
{
    const Decoder decoder(sourceName);
    return json::decodeDocument<GameResource>(decoder, data, [&decoder](const Json& root) { return decoder.decodeRoot(root); });
}

Result<GameResource> loadGameResource(const AssetLocator& locator, const AssetPath& asset)
{
    auto bytes = locator.readBytes(asset, maxGameFileBytes);
    if (!bytes) return std::unexpected(bytes.error());
    return decodeGameResource(*bytes, asset.view());
}

} // namespace schuimm::adventure
