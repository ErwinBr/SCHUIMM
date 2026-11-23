#pragma once

#include "Platform/Interface/WindowEvent.h"

#include <vector>

namespace schuimm {

// Small helper used by native backends to buffer events between pollEvents() and drainEvents().
class WindowEventQueue {
public:
    void push(const WindowEvent& event) { events_.push_back(event); }

    void drainInto(std::vector<WindowEvent>& out)
    {
        out.insert(out.end(), events_.begin(), events_.end());
        events_.clear();
    }

    [[nodiscard]] bool empty() const noexcept { return events_.empty(); }

private:
    std::vector<WindowEvent> events_;
};

} // namespace schuimm
