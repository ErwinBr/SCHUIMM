#include "Script/Runtime/ScriptHost.h"

#include "Core/Common/Assert.h"
#include "Core/Common/StringBuilder.h"
#include "Script/Runtime/StandardNatives.h"

namespace schuimm::script {

namespace {
constexpr const char* category = "Script";
} // namespace

ScriptHost::ScriptHost(Logger& logger, ScriptHostSettings settings)
    : logger_(logger)
    , settings_(settings)
    , scheduler_(settings.scheduler)
{
    // Registration of the prelude into an empty registry cannot fail.
    const Status status = registerStandardNatives(natives_, [this](std::string_view program, std::string_view text) {
        if (settings_.logPrintOutput) {
            logger_.info(category, text);
        }
        if (output_) {
            output_(program, text);
        }
    });
    SCHUIMM_VERIFY(status.has_value(), "standard natives failed to register");
    scheduler_.setEventCallback([this](const CoroutineEvent& event) { onEvent(event); });
}

ScriptHost::~ScriptHost()
{
    scheduler_.setEventCallback({});
    scheduler_.clear();
}

Result<std::shared_ptr<const Program>> ScriptHost::load(std::span<const std::byte> bytecode, std::string name)
{
    auto program = Program::load(bytecode, natives_, name);
    if (!program) {
        logger_.error(category, concat("Cannot load script '", name, "': ", describe(program.error())));
        return std::unexpected(program.error());
    }
    const Module& module = (*program)->module();
    logger_.info(category, concat("Loaded script '", (*program)->name(), "': ", module.functions.size(), " functions, ",
                                  module.imports.size(), " natives, ", bytecode.size(), " bytes (validated)"));
    return program;
}

Result<CoroutineHandle> ScriptHost::start(std::shared_ptr<const Program> program, std::string_view function,
                                          std::vector<Value> arguments, ExecutionOwner owner)
{
    const std::string name = program ? program->name() : std::string("<none>");
    auto handle = scheduler_.start(std::move(program), function, std::move(arguments), owner);
    if (!handle) {
        logger_.error(category, concat("Cannot start '", function, "' of script '", name, "': ", handle.error().message));
        return handle;
    }
    ++statistics_.started;
    logger_.debug(category, concat("Started coroutine ", handle->index, " running ", name, ":", function));
    return handle;
}

Result<CoroutineHandle> ScriptHost::start(std::shared_ptr<GlobalState> globals, std::string_view function,
                                          std::vector<Value> arguments, ExecutionOwner owner)
{
    const std::string name = globals && globals->program() ? globals->program()->name() : std::string("<none>");
    auto handle = scheduler_.start(std::move(globals), function, std::move(arguments), owner);
    if (!handle) {
        logger_.error(category, concat("Cannot start '", function, "' of script '", name, "': ", handle.error().message));
        return handle;
    }
    ++statistics_.started;
    logger_.debug(category, concat("Started coroutine ", handle->index, " running ", name, ":", function));
    return handle;
}

Result<DispatchResult> ScriptHost::dispatch(std::shared_ptr<BehaviourInstance> instance, std::string_view event,
                                            std::vector<Value> arguments, ExecutionOwner owner)
{
    std::string name = "<none>";
    std::string target = "?";
    if (instance) {
        const Program& program = instance->program();
        name = program.name();
        const BehaviourEntry& behaviour = program.module().behaviours[instance->behaviour()];
        target = behaviour.kind == BehaviourKind::Object ? program.module().string(behaviour.nameIndex)
                                                         : std::string(toString(behaviour.kind));
    }
    auto result = scheduler_.dispatch(std::move(instance), event, std::move(arguments), owner);
    if (!result) {
        logger_.error(category, concat("Cannot dispatch '", target, ".", event, "' of script '", name, "': ",
                                       result.error().message));
        return result;
    }
    if (!result->handled) {
        logger_.debug(category, concat("No handler ", name, ":", target, ".", event));
        return result;
    }
    ++statistics_.started;
    logger_.debug(category, concat("Started coroutine ", result->handle.index, " running ", name, ":", target, ".", event));
    return result;
}

Result<DispatchResult> ScriptHost::dispatch(std::shared_ptr<const Program> program, std::string_view object,
                                            std::string_view event, ObjectRef self, std::vector<Value> arguments,
                                            ExecutionOwner owner)
{
    const std::string name = program ? program->name() : std::string("<none>");
    auto result = scheduler_.dispatch(std::move(program), object, event, self, std::move(arguments), owner);
    if (!result) {
        logger_.error(category, concat("Cannot dispatch '", object, ".", event, "' of script '", name, "': ",
                                       result.error().message));
        return result;
    }
    if (!result->handled) {
        logger_.debug(category, concat("No handler ", name, ":", object, ".", event));
        return result;
    }
    ++statistics_.started;
    logger_.debug(category, concat("Started coroutine ", result->handle.index, " running ", name, ":", object, ".", event,
                                   " for object#", self.id));
    return result;
}

void ScriptHost::update(double deltaSeconds)
{
    scheduler_.update(deltaSeconds);
    scheduler_.releaseFinished();
}

void ScriptHost::stopAll() { scheduler_.clear(); }

void ScriptHost::onEvent(const CoroutineEvent& event)
{
    switch (event.kind) {
    case CoroutineEvent::Kind::Finished:
        ++statistics_.finished;
        logger_.debug(category, concat("Coroutine ", event.program, ":", event.function, " finished"));
        break;
    case CoroutineEvent::Kind::Failed:
        ++statistics_.failed;
        logger_.error(category, event.error != nullptr ? describe(*event.error) : std::string("Coroutine failed"));
        break;
    case CoroutineEvent::Kind::Cancelled:
        ++statistics_.cancelled;
        logger_.debug(category, concat("Coroutine ", event.program, ":", event.function, " cancelled"));
        break;
    case CoroutineEvent::Kind::BudgetWarning:
        logger_.warning(category, concat(event.error != nullptr ? describe(*event.error) : std::string("Budget exceeded"),
                                         " (preempted; it continues next update)"));
        break;
    }
    if (events_) {
        events_(event);
    }
}

} // namespace schuimm::script
