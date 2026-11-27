// schuimm-run: runs compiled SCHUIMM scripts (.schb) without a window, with simulated frames.
//
//   schuimm-run [options] <script.schb>
//
// Each simulated frame advances script time by --dt seconds and updates the scheduler once,
// exactly as the game loop does. Output of print() goes to stdout, diagnostics to stderr.
// Exit codes: 0 all coroutines finished, 1 load/start failure or a coroutine failed,
// 3 frame limit reached with coroutines still running, 2 invalid command line.

#include "Core/Common/StringBuilder.h"
#include "Core/Logging/Logger.h"
#include "Core/Logging/StandardSinks.h"
#include "Platform/FileSystem/FileSystem.h"
#include "Script/Bytecode/Disassembler.h"
#include "Script/Runtime/ScriptHost.h"
#include "Script/Runtime/ScriptState.h"

#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <string_view>
#include <vector>

using namespace schuimm;
using namespace schuimm::script;

namespace {

struct DispatchRequest {
    std::string object;
    std::string event;
    std::uint64_t id = 1;
};

// "blender.look_at" or "blender.look_at=7".
bool parseDispatch(std::string_view text, DispatchRequest& request)
{
    const std::size_t equals = text.find('=');
    if (equals != std::string_view::npos) {
        const std::string id(text.substr(equals + 1));
        char* end = nullptr;
        request.id = std::strtoull(id.c_str(), &end, 10);
        if (id.empty() || end == nullptr || *end != '\0' || request.id == 0) return false;
        text = text.substr(0, equals);
    }
    const std::size_t dot = text.find('.');
    if (dot == std::string_view::npos || dot == 0 || dot + 1 == text.size()) return false;
    request.object = std::string(text.substr(0, dot));
    request.event = std::string(text.substr(dot + 1));
    return true;
}

constexpr const char* usage =
    "Usage: schuimm-run [options] <script.schb>\n"
    "\n"
    "Options:\n"
    "  --entry NAME     Function to start (default: main); may be repeated for concurrent coroutines\n"
    "  --dispatch OBJECT.EVENT[=ID]\n"
    "                   Invoke the handler EVENT of object OBJECT (without arguments) with object id\n"
    "                   ID (nonzero) as self (default 1); may be repeated. With --dispatch, main is only\n"
    "                   started if given with --entry. A missing handler is reported and skipped.\n"
    "  --dt SECONDS     Simulated frame time (default: 0.016)\n"
    "  --frames N       Stop after N frames (default: 100000)\n"
    "  --budget N       Instruction budget per coroutine per frame (default: 100000)\n"
    "  --preempt        Pause coroutines that exceed the budget instead of failing them\n"
    "  --trace          Print every executed instruction\n"
    "  --disasm         Print the disassembly before running\n"
    "  --frame-log      Print the frame number and time with every output line\n"
    "  -h, --help       Show this help\n";

} // namespace

int main(int argc, char** argv)
{
    std::string input;
    std::vector<std::string> entries;
    std::vector<DispatchRequest> dispatches;
    double dt = 0.016;
    long long frames = 100000;
    unsigned long long budget = 100000;
    bool preempt = false;
    bool trace = false;
    bool disassembly = false;
    bool frameLog = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument = argv[i];
        const auto value = [&]() -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "%s requires a value\n", argv[i]);
                std::exit(2);
            }
            return argv[++i];
        };
        if (argument == "-h" || argument == "--help") {
            std::fputs(usage, stdout);
            return 0;
        } else if (argument == "--entry") {
            entries.emplace_back(value());
        } else if (argument == "--dispatch") {
            DispatchRequest request;
            const char* text = value();
            if (!parseDispatch(text, request)) {
                std::fprintf(stderr, "Invalid --dispatch '%s': expected OBJECT.EVENT or OBJECT.EVENT=ID with ID > 0\n", text);
                return 2;
            }
            dispatches.push_back(std::move(request));
        } else if (argument == "--dt") {
            dt = std::strtod(value(), nullptr);
        } else if (argument == "--frames") {
            frames = std::strtoll(value(), nullptr, 10);
        } else if (argument == "--budget") {
            budget = std::strtoull(value(), nullptr, 10);
        } else if (argument == "--preempt") {
            preempt = true;
        } else if (argument == "--trace") {
            trace = true;
        } else if (argument == "--disasm") {
            disassembly = true;
        } else if (argument == "--frame-log") {
            frameLog = true;
        } else if (!argument.empty() && argument[0] == '-') {
            std::fprintf(stderr, "Unknown option %s\n\n%s", argv[i], usage);
            return 2;
        } else {
            input = std::string(argument);
        }
    }
    if (input.empty() || !(dt >= 0.0) || frames <= 0 || budget == 0) {
        std::fputs(usage, stderr);
        return 2;
    }
    if (entries.empty() && dispatches.empty()) {
        entries.emplace_back("main");
    }

    Logger logger(LogLevel::Warning);
    logger.addSink(std::make_unique<ConsoleSink>());
    ScriptHostSettings settings;
    settings.logPrintOutput = false;
    settings.scheduler.instructionBudget = budget;
    settings.scheduler.budgetPolicy = preempt ? BudgetPolicy::Preempt : BudgetPolicy::Fail;
    ScriptHost host(logger, settings);

    long long frame = 0;
    host.setOutputCallback([&](std::string_view, std::string_view text) {
        if (frameLog) {
            std::printf("[frame %lld, t=%.3f] ", frame, static_cast<double>(host.scheduler().timeNanoseconds()) / 1e9);
        }
        std::printf("%.*s\n", static_cast<int>(text.size()), text.data());
    });
    if (trace) {
        host.scheduler().setTraceCallback([](const TraceEvent& event) { std::fprintf(stderr, "%s\n", formatTrace(event).c_str()); });
    }

    auto bytes = fs::readFile(input);
    if (!bytes) {
        std::fprintf(stderr, "schuimm-run: %s\n", describe(bytes.error()).c_str());
        return 1;
    }
    auto program = host.load(*bytes, input);
    if (!program) {
        return 1;
    }
    if (disassembly) {
        std::fputs(disassemble((*program)->module()).c_str(), stdout);
    }
    // One run is one game session: every started function and handler shares the global
    // variables, and each object (name and handle) keeps its instance variables across dispatches.
    const std::shared_ptr<GlobalState> globals = GlobalState::create(*program);
    for (const std::string& entry : entries) {
        if (!host.start(globals, entry)) {
            return 1;
        }
    }
    std::map<std::pair<std::string, std::uint64_t>, std::shared_ptr<BehaviourInstance>> instances;
    for (const DispatchRequest& request : dispatches) {
        const auto behaviour = (*program)->findBehaviour(BehaviourKind::Object, {}, request.object);
        if (!behaviour) {
            std::fprintf(stderr, "schuimm-run: no object %s (skipped)\n", request.object.c_str());
            continue;
        }
        std::shared_ptr<BehaviourInstance>& instance = instances[{request.object, request.id}];
        if (!instance) {
            auto created = BehaviourInstance::create(globals, *behaviour, Value::fromObject(ObjectRef{request.id}));
            if (!created) {
                std::fprintf(stderr, "schuimm-run: %s\n", describe(created.error()).c_str());
                return 1;
            }
            instance = std::move(*created);
        }
        auto result = host.dispatch(instance, request.event);
        if (!result) {
            return 1;
        }
        if (!result->handled) {
            std::fprintf(stderr, "schuimm-run: no handler %s.%s (skipped)\n", request.object.c_str(), request.event.c_str());
        }
    }
    for (frame = 1; frame <= frames && host.activeCount() > 0; ++frame) {
        host.update(frame == 1 ? 0.0 : dt);
    }
    if (host.statistics().failed > 0) {
        return 1;
    }
    if (host.activeCount() > 0) {
        std::fprintf(stderr, "schuimm-run: frame limit reached with %zu coroutine(s) still running\n", host.activeCount());
        return 3;
    }
    return 0;
}
