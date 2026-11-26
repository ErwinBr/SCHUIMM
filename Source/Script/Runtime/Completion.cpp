#include "Script/Runtime/Completion.h"

#include "Script/Runtime/ScriptScheduler.h"

#include <algorithm>
#include <utility>

namespace schuimm::script {

namespace {
std::int64_t liveTokens = 0;
} // namespace

const char* toString(CompletionToken::State state) noexcept
{
    switch (state) {
    case CompletionToken::State::Pending: return "pending";
    case CompletionToken::State::Completed: return "completed";
    case CompletionToken::State::Cancelled: return "cancelled";
    }
    return "?";
}

CompletionToken::CompletionToken() { ++liveTokens; }

CompletionToken::~CompletionToken() { --liveTokens; }

std::int64_t CompletionToken::liveCount() noexcept { return liveTokens; }

CompletionRef CompletionToken::create() { return CompletionRef::adopt(new CompletionToken()); }

void CompletionToken::release() noexcept
{
    if (--references_ == 0) {
        delete this;
    }
}

void CompletionToken::complete() { resolve(State::Completed, {}); }

void CompletionToken::cancel(std::string reason) { resolve(State::Cancelled, std::move(reason)); }

void CompletionToken::setNotAwaitable(std::string message)
{
    awaitable_ = false;
    notAwaitable_ = std::move(message);
}

void CompletionToken::resolve(State state, std::string reason)
{
    if (state_ != State::Pending) {
        return;
    }
    state_ = state;
    reason_ = std::move(reason);
    abandon_ = nullptr; // Resolved: nothing left to abandon (and no captured state to keep).
    // Notifying may drop the last other references (a cancelled waiter releases its frames).
    const CompletionRef keepAlive = CompletionRef::share(this);
    std::vector<Waiter> waiters = std::move(waiters_);
    waiters_.clear();
    for (const Waiter& waiter : waiters) {
        waiter.scheduler->onTokenResolved(waiter.coroutine, *this);
    }
}

void CompletionToken::addWaiter(ScriptScheduler& scheduler, CoroutineHandle coroutine)
{
    waiters_.push_back({&scheduler, coroutine});
}

void CompletionToken::removeWaiter(const ScriptScheduler& scheduler, CoroutineHandle coroutine)
{
    std::erase_if(waiters_, [&](const Waiter& waiter) { return waiter.scheduler == &scheduler && waiter.coroutine == coroutine; });
}

void CompletionToken::abandon()
{
    if (state_ != State::Pending || !abandon_) {
        return;
    }
    std::function<void()> handler = std::move(abandon_);
    abandon_ = nullptr;
    handler();
}

} // namespace schuimm::script
