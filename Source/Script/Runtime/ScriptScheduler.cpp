#include "Script/Runtime/ScriptScheduler.h"

#include "Core/Common/Assert.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>

namespace schuimm::script {

std::optional<std::int64_t> secondsToNanoseconds(double seconds) noexcept
{
    if (!std::isfinite(seconds) || seconds < 0.0 || seconds > 1e9) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(std::llround(seconds * 1e9));
}

class ScriptScheduler::UpdateGuard {
public:
    explicit UpdateGuard(ScriptScheduler& scheduler) noexcept
        : scheduler_(scheduler)
        , exceptions_(std::uncaught_exceptions())
    {
        scheduler_.updating_ = true;
    }

    UpdateGuard(const UpdateGuard&) = delete;
    UpdateGuard& operator=(const UpdateGuard&) = delete;

    ~UpdateGuard()
    {
        const std::uint32_t running = scheduler_.running_;
        if (std::uncaught_exceptions() > exceptions_ && running < scheduler_.slots_.size()) {
            Slot& slot = scheduler_.slots_[running];
            if (slot.used && slot.state == CoroutineState::Running) {
                // Unwinding from a callback in the middle of this coroutine's run: its state is
                // unknown, so it fails. No event is emitted (a throwing callback must not run
                // again while unwinding).
                ScriptError error;
                error.kind = ScriptErrorKind::Runtime;
                error.message = "Aborted by an exception thrown from a scheduler callback";
                error.script = slot.program;
                error.function = slot.function;
                slot.error = std::move(error);
                slot.state = CoroutineState::Failed;
                slot.coroutine->releaseFrames();
            }
        }
        scheduler_.running_ = 0xFFFFFFFFu;
        scheduler_.updating_ = false;
    }

private:
    ScriptScheduler& scheduler_;
    int exceptions_;
};

ScriptScheduler::ScriptScheduler(SchedulerSettings settings)
    : settings_(settings)
{
}

ScriptScheduler::~ScriptScheduler() = default;

ScriptScheduler::Slot* ScriptScheduler::find(CoroutineHandle handle)
{
    if (handle.index >= slots_.size()) return nullptr;
    Slot& slot = slots_[handle.index];
    return (slot.used && slot.generation == handle.generation) ? &slot : nullptr;
}

const ScriptScheduler::Slot* ScriptScheduler::find(CoroutineHandle handle) const
{
    return const_cast<ScriptScheduler*>(this)->find(handle);
}

Result<CoroutineHandle> ScriptScheduler::start(std::shared_ptr<const Program> program, std::string_view function,
                                               std::vector<Value> arguments)
{
    auto coroutine = Coroutine::create(std::move(program), function, std::move(arguments), settings_.limits);
    if (!coroutine) {
        return std::unexpected(coroutine.error());
    }
    std::uint32_t index = 0;
    while (index < slots_.size() && slots_[index].used) {
        ++index;
    }
    if (index == slots_.size()) {
        slots_.emplace_back();
    }
    Slot& slot = slots_[index];
    slot.used = true;
    slot.state = CoroutineState::Ready;
    slot.program = (*coroutine)->program().name();
    slot.function = std::string(function);
    slot.coroutine = std::move(*coroutine);
    slot.wakeTime = now_;
    slot.startedInUpdate = updating_ ? updates_ : 0;
    slot.sequence = nextSequence_++;
    slot.budgetWarned = false;
    slot.error.reset();
    order_.push_back(index);
    return CoroutineHandle{index, slot.generation};
}

void ScriptScheduler::emit(CoroutineEvent::Kind kind, const Slot& slot, CoroutineHandle handle)
{
    if (callback_) {
        CoroutineEvent event;
        event.kind = kind;
        event.handle = handle;
        event.program = slot.program;
        event.function = slot.function;
        event.error = slot.error ? &*slot.error : nullptr;
        callback_(event);
    }
}

void ScriptScheduler::finish(Slot& slot, CoroutineHandle handle, CoroutineState state)
{
    slot.state = state;
    if (state != CoroutineState::Finished) {
        slot.coroutine->releaseFrames(); // Finished keeps its (frame-less) result value.
    }
    const auto kind = state == CoroutineState::Finished
                          ? CoroutineEvent::Kind::Finished
                          : (state == CoroutineState::Failed ? CoroutineEvent::Kind::Failed : CoroutineEvent::Kind::Cancelled);
    emit(kind, slot, handle);
}

bool ScriptScheduler::cancel(CoroutineHandle handle)
{
    Slot* slot = find(handle);
    if (slot == nullptr || isTerminal(slot->state) || handle.index == running_) {
        return false;
    }
    finish(*slot, handle, CoroutineState::Cancelled);
    return true;
}

void ScriptScheduler::update(double deltaSeconds)
{
    SCHUIMM_VERIFY(!updating_, "ScriptScheduler::update called re-entrantly");
    ++updates_;
    const double scaled = deltaSeconds * settings_.timeScale;
    if (std::isfinite(scaled) && scaled > 0.0) {
        const double nanoseconds = std::min(scaled * 1e9, 1e15); // At most ~11.6 days per update.
        now_ += static_cast<std::int64_t>(std::llround(nanoseconds));
    }
    const UpdateGuard guard(*this);
    // Coroutines started during this update are appended to order_ but skipped below.
    const std::size_t count = order_.size();
    for (std::size_t i = 0; i < count; ++i) {
        const std::uint32_t index = order_[i];
        Slot& slot = slots_[index];
        if (!slot.used || isTerminal(slot.state) || slot.startedInUpdate == updates_) {
            continue;
        }
        if (slot.state == CoroutineState::Waiting && slot.wakeTime > now_) {
            continue;
        }
        const CoroutineHandle handle{index, slot.generation};
        slot.state = CoroutineState::Running;
        running_ = index;
        NativeContext context{handle, slot.program, now_};
        RunResult run = runCoroutine(*slot.coroutine, settings_.instructionBudget, context, trace_ ? &trace_ : nullptr);
        running_ = 0xFFFFFFFFu;
        Slot& after = slot;
        switch (run.outcome) {
        case RunOutcome::Finished: finish(after, handle, CoroutineState::Finished); break;
        case RunOutcome::Failed:
            after.error = std::move(run.error);
            finish(after, handle, CoroutineState::Failed);
            break;
        case RunOutcome::Waiting:
            if (run.waitNanoseconds == 0) {
                after.state = CoroutineState::Ready;
            } else {
                after.state = CoroutineState::Waiting;
                after.wakeTime = run.waitNanoseconds > std::numeric_limits<std::int64_t>::max() - now_
                                     ? std::numeric_limits<std::int64_t>::max()
                                     : now_ + run.waitNanoseconds;
            }
            break;
        case RunOutcome::Yielded: after.state = CoroutineState::Ready; break;
        case RunOutcome::BudgetExhausted:
            if (settings_.budgetPolicy == BudgetPolicy::Fail) {
                after.error = std::move(run.error);
                finish(after, handle, CoroutineState::Failed);
            } else {
                after.state = CoroutineState::Ready;
                if (!after.budgetWarned) {
                    after.budgetWarned = true;
                    after.error = std::move(run.error);
                    emit(CoroutineEvent::Kind::BudgetWarning, after, handle);
                    slots_[index].error.reset();
                }
            }
            break;
        }
    }
}

std::optional<CoroutineState> ScriptScheduler::state(CoroutineHandle handle) const
{
    const Slot* slot = find(handle);
    if (slot == nullptr) return std::nullopt;
    return slot->state;
}

const ScriptError* ScriptScheduler::error(CoroutineHandle handle) const
{
    const Slot* slot = find(handle);
    return (slot != nullptr && slot->state == CoroutineState::Failed && slot->error) ? &*slot->error : nullptr;
}

const Value* ScriptScheduler::result(CoroutineHandle handle) const
{
    const Slot* slot = find(handle);
    return (slot != nullptr && slot->state == CoroutineState::Finished) ? &slot->coroutine->result() : nullptr;
}

bool ScriptScheduler::release(CoroutineHandle handle)
{
    Slot* slot = find(handle);
    if (slot == nullptr || !isTerminal(slot->state) || updating_) {
        return false;
    }
    slot->coroutine.reset();
    slot->error.reset();
    slot->used = false;
    ++slot->generation;
    order_.erase(std::remove(order_.begin(), order_.end(), handle.index), order_.end());
    return true;
}

void ScriptScheduler::releaseFinished()
{
    SCHUIMM_VERIFY(!updating_, "releaseFinished during update");
    for (std::uint32_t index = 0; index < slots_.size(); ++index) {
        if (slots_[index].used && isTerminal(slots_[index].state)) {
            release({index, slots_[index].generation});
        }
    }
}

void ScriptScheduler::clear()
{
    SCHUIMM_VERIFY(!updating_, "clear during update");
    for (std::uint32_t index = 0; index < slots_.size(); ++index) {
        Slot& slot = slots_[index];
        if (slot.used && !isTerminal(slot.state)) {
            finish(slot, {index, slot.generation}, CoroutineState::Cancelled);
        }
    }
    releaseFinished();
}

std::size_t ScriptScheduler::activeCount() const noexcept
{
    return static_cast<std::size_t>(std::count_if(slots_.begin(), slots_.end(), [](const Slot& slot) {
        return slot.used && !isTerminal(slot.state);
    }));
}

std::size_t ScriptScheduler::size() const noexcept
{
    return static_cast<std::size_t>(std::count_if(slots_.begin(), slots_.end(), [](const Slot& slot) { return slot.used; }));
}

} // namespace schuimm::script
