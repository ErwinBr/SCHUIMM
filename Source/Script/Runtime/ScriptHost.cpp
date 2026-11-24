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
                                          std::vector<Value> arguments)
{
    const std::string name = program ? program->name() : std::string("<none>");
    auto handle = scheduler_.start(std::move(program), function, std::move(arguments));
    if (!handle) {
        logger_.error(category, concat("Cannot start '", function, "' of script '", name, "': ", handle.error().message));
        return handle;
    }
    ++statistics_.started;
    logger_.debug(category, concat("Started coroutine ", handle->index, " running ", name, ":", function));
    return handle;
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
