#pragma once

// Completion of engine operations: the bridge between gameplay systems (movement, animation,
// later dialogue) and the coroutines that wait for them. Generic: nothing here knows what an
// operation does.
//
// A CompletionToken stands for ONE accepted engine request or playback, never for an entity's
// current state. The engine creates it when it accepts the request and later resolves it once:
// complete() or cancel(reason). The outcome stays readable for as long as anything holds the
// token (a waiting coroutine, an `operation` value in a script, the engine), so a waiter sees
// the outcome of its own request even if another request has started since. The token is freed
// with its last reference: there is no history of past operations.
//
// Coroutines wait through NativeResult::await (Runtime/NativeRegistry.h). The scheduler
// subscribes the waiting coroutine; resolving the token makes it Ready (completed) or cancels it
// (cancelled), through the scheduler and never by running script code directly. Each outcome is
// delivered exactly once to each waiter. Resolving an already resolved token does nothing.
//
// Tokens use a non-atomic reference count, like strings: game thread only.

#include "Script/Runtime/RuntimeTypes.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace schuimm::script {

class ScriptScheduler;
class CompletionRef;

class CompletionToken {
public:
    enum class State : std::uint8_t { Pending, Completed, Cancelled };

    [[nodiscard]] static CompletionRef create();

    CompletionToken(const CompletionToken&) = delete;
    CompletionToken& operator=(const CompletionToken&) = delete;

    [[nodiscard]] State state() const noexcept { return state_; }
    [[nodiscard]] bool isPending() const noexcept { return state_ == State::Pending; }
    // Why the operation was cancelled ("stopped", "replaced", "destroyed", ...); empty otherwise.
    [[nodiscard]] const std::string& cancelReason() const noexcept { return reason_; }

    // Resolve the token. Only the first call has an effect; it notifies every waiter.
    void complete();
    void cancel(std::string reason);

    // An operation that never completes on its own (a looping animation) cannot be waited for:
    // waiting is a script error with `message`. Tokens are awaitable by default.
    void setNotAwaitable(std::string message);
    [[nodiscard]] bool isAwaitable() const noexcept { return awaitable_; }
    [[nodiscard]] const std::string& notAwaitableMessage() const noexcept { return notAwaitable_; }

    // Called when a coroutine that waits with NativeResult::await(token, true) is cancelled while
    // the token is pending: the waiter gave up, so the engine may stop the operation it started
    // for it (walk_to stops the walk). Called at most once.
    void setAbandonHandler(std::function<void()> handler) { abandon_ = std::move(handler); }

    [[nodiscard]] std::size_t waiterCount() const noexcept { return waiters_.size(); }
    [[nodiscard]] std::uint32_t referenceCount() const noexcept { return references_; }

    void retain() noexcept { ++references_; }
    void release() noexcept;

    // Live tokens in the process (single-threaded use): for leak checks in tests.
    [[nodiscard]] static std::int64_t liveCount() noexcept;

private:
    friend class ScriptScheduler;

    struct Waiter {
        ScriptScheduler* scheduler = nullptr;
        CoroutineHandle coroutine;
    };

    CompletionToken();
    ~CompletionToken();

    void resolve(State state, std::string reason);
    void addWaiter(ScriptScheduler& scheduler, CoroutineHandle coroutine);
    void removeWaiter(const ScriptScheduler& scheduler, CoroutineHandle coroutine);
    void abandon();

    std::uint32_t references_ = 1;
    State state_ = State::Pending;
    bool awaitable_ = true;
    std::string reason_;
    std::string notAwaitable_;
    std::vector<Waiter> waiters_;
    std::function<void()> abandon_;
};

[[nodiscard]] const char* toString(CompletionToken::State state) noexcept;

// Shared ownership of a CompletionToken (an intrusive pointer). Null is a valid value.
class CompletionRef {
public:
    CompletionRef() noexcept = default;
    // Takes over one reference.
    static CompletionRef adopt(CompletionToken* token) noexcept
    {
        CompletionRef ref;
        ref.token_ = token;
        return ref;
    }
    // Adds a reference.
    static CompletionRef share(CompletionToken* token) noexcept
    {
        if (token != nullptr) token->retain();
        return adopt(token);
    }
    CompletionRef(const CompletionRef& other) noexcept
        : token_(other.token_)
    {
        if (token_ != nullptr) token_->retain();
    }
    CompletionRef(CompletionRef&& other) noexcept
        : token_(other.token_)
    {
        other.token_ = nullptr;
    }
    CompletionRef& operator=(CompletionRef other) noexcept
    {
        std::swap(token_, other.token_);
        return *this;
    }
    ~CompletionRef()
    {
        if (token_ != nullptr) token_->release();
    }

    [[nodiscard]] CompletionToken* get() const noexcept { return token_; }
    CompletionToken* operator->() const noexcept { return token_; }
    CompletionToken& operator*() const noexcept { return *token_; }
    explicit operator bool() const noexcept { return token_ != nullptr; }
    // Gives up the reference without releasing it.
    [[nodiscard]] CompletionToken* detach() noexcept
    {
        CompletionToken* token = token_;
        token_ = nullptr;
        return token;
    }

private:
    CompletionToken* token_ = nullptr;
};

} // namespace schuimm::script
