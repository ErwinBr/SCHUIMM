#include "Adventure/AdventureRuntime.h"

#include "Adventure/AdventureInterface.h"
#include "Core/Common/Assert.h"
#include "Core/Common/StringBuilder.h"

namespace schuimm::adventure {

Result<AdventureNatives> AdventureNatives::bind(script::NativeRegistry& registry)
{
    auto state = std::make_shared<State>();
    std::unordered_map<std::string, script::NativeImplementation> natives = state->context.implementations();
    // Each native keeps the state (and so the context) alive, and acts only during a session.
    for (auto& [name, implementation] : natives) {
        implementation = [state, inner = std::move(implementation)](script::NativeContext& context,
                                                                    std::span<const script::Value> arguments) {
            if (!state->session) return script::NativeResult::error("no adventure game session is running");
            return inner(context, arguments);
        };
    }
    if (auto status = registry.bind(adventureInterface(), std::move(natives)); !status) {
        return std::unexpected(status.error());
    }
    return AdventureNatives(std::move(state));
}

AdventureRuntime::AdventureRuntime(script::ScriptHost& host, AdventureNatives& natives, const AssetLocator& locator,
                                   GameResource game, std::shared_ptr<const script::Program> program)
    : host_(host)
    , natives_(natives)
    , game_(std::move(game))
    , program_(program)
    , globals_(script::GlobalState::create(program))
    , library_(locator, std::move(program))
    , owner_(host.createOwner())
{
}

Result<std::unique_ptr<AdventureRuntime>> AdventureRuntime::create(script::ScriptHost& host, AdventureNatives& natives,
                                                                   const AssetLocator& locator, const AssetPath& game,
                                                                   Camera2D* camera)
{
    if (natives.inSession()) {
        return makeError(ErrorCode::InvalidState, "another adventure game session is running");
    }
    auto resource = loadGameResource(locator, game);
    if (!resource) return std::unexpected(resource.error());
    auto bytes = locator.readBytes(resource->program);
    if (!bytes) {
        return makeError(bytes.error().code, concat(game.view(), ": program: ", bytes.error().message));
    }
    auto program = host.load(*bytes, resource->program.string());
    if (!program) {
        return makeError(program.error().code, concat(game.view(), ": program '", resource->program.view(),
                                                      "' cannot be loaded: ", program.error().message));
    }
    std::unique_ptr<AdventureRuntime> runtime(
        new AdventureRuntime(host, natives, locator, std::move(*resource), std::move(*program)));
    natives.state_->session = true;
    natives.context().setCamera(camera);
    return runtime;
}

AdventureRuntime::~AdventureRuntime()
{
    SCHUIMM_VERIFY(!host_.isUpdating(), "an AdventureRuntime must not be destroyed while the ScriptHost updates");
    host_.cancelOwner(owner_);
    leaveRoom();
    natives_.context().setCamera(nullptr);
    natives_.state_->session = false;
}

Status AdventureRuntime::enterRoom(const AssetPath& room)
{
    SCHUIMM_VERIFY(!host_.isUpdating(), "rooms must not be entered while the ScriptHost updates");
    if (!game_.hasRoom(room)) {
        return makeError(ErrorCode::InvalidArgument, concat("'", room.view(), "' is not a room of the game '", game_.id, "'"));
    }
    auto loaded = library_.load(room);
    if (!loaded) return std::unexpected(loaded.error());
    // A room without scripts has no program in LoadedRoom; it still shares the session's.
    auto instance = room::RoomInstance::create(*loaded, host_, loaded->script ? globals_ : nullptr);
    if (!instance) return std::unexpected(instance.error());
    leaveRoom();
    room_ = std::move(*instance);
    natives_.context().setActiveRoom(room_.get());
    return {};
}

void AdventureRuntime::leaveRoom()
{
    SCHUIMM_VERIFY(!host_.isUpdating(), "rooms must not be left while the ScriptHost updates");
    if (!room_) return;
    natives_.context().setActiveRoom(nullptr);
    room_.reset();
}

Result<script::DispatchResult> AdventureRuntime::dispatchRoom(std::string_view event, std::vector<script::Value> arguments)
{
    if (!room_) return script::DispatchResult{};
    return room_->dispatchRoom(event, std::move(arguments));
}

Result<script::DispatchResult> AdventureRuntime::dispatchObject(std::string_view object, std::string_view event,
                                                                std::vector<script::Value> arguments)
{
    if (!room_) return script::DispatchResult{};
    return room_->dispatch(object, event, std::move(arguments));
}

Result<script::DispatchResult> AdventureRuntime::dispatchActor(std::string_view actor, std::string_view event,
                                                               std::vector<script::Value> arguments)
{
    if (!room_) return script::DispatchResult{};
    return room_->dispatchActor(actor, event, std::move(arguments));
}

Result<script::CoroutineHandle> AdventureRuntime::start(std::string_view function, std::vector<script::Value> arguments)
{
    return host_.start(globals_, function, std::move(arguments), owner_);
}

void AdventureRuntime::update(double deltaSeconds)
{
    if (room_) room_->update(deltaSeconds);
    natives_.context().publish();
}

} // namespace schuimm::adventure
