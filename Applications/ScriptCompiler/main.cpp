// schuimmc: compiles SCHUIMM scripts (.sch) into bytecode (.schb) and prints debug dumps.
//
//   schuimmc [options] <input.sch>                      one room script on its own
//   schuimmc [options] --global a.sch --room b.sch ...  several scripts into one program
//
// Exit codes: 0 success, 1 compile errors or I/O failure, 2 invalid command line.

#include "Core/Common/StringBuilder.h"
#include "Adventure/AdventureInterface.h"
#include "Platform/FileSystem/FileSystem.h"
#include "Script/Bytecode/BytecodeReader.h"
#include "Script/Bytecode/BytecodeWriter.h"
#include "Script/Bytecode/Disassembler.h"
#include "Script/Compiler/AstPrinter.h"
#include "Script/Compiler/Compiler.h"
#include "Script/Compiler/Ir.h"

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

using namespace schuimm;
using namespace schuimm::script;

namespace {

constexpr const char* usage =
    "Usage: schuimmc [options] <input.sch>...\n"
    "       schuimmc [options] [--global FILE] [--room FILE] [--actor FILE] ...\n"
    "\n"
    "Compiles SCHUIMM scripts into platform-independent bytecode. All inputs form one\n"
    "program: their global variables, functions and enums share one namespace.\n"
    "\n"
    "Options:\n"
    "  -o FILE        Write the bytecode to FILE (default: none, only check)\n"
    "  -O0            Disable the optimizer\n"
    "  -O1            Enable the optimizer (default)\n"
    "  --global FILE  A global script: game-wide declarations only (repeatable)\n"
    "  --room FILE    A room script (repeatable; also: an input without option)\n"
    "  --actor FILE   An actor script (repeatable)\n"
    "  --source-root DIR\n"
    "                 Name each source by its path relative to DIR, as resources refer\n"
    "                 to it (default: the path as given)\n"
    "  --name NAME    Source name of the only input (diagnostics, debug information)\n"
    "  --interface N  Let the script call the native interface N besides the core\n"
    "                 interface (repeatable); see --list-interfaces\n"
    "  --list-interfaces\n"
    "                 Print the native interfaces this compiler knows and exit\n"
    "  --tokens       Print the token stream\n"
    "  --ast          Print the syntax tree\n"
    "  --types        Print the syntax tree with types (after semantic analysis)\n"
    "  --ir           Print the IR before optimization\n"
    "  --ir-opt       Print the IR after optimization\n"
    "  --disasm       Print the bytecode disassembly\n"
    "  --verify       Re-read the written file and validate it\n"
    "  -h, --help     Show this help\n";

// The native interfaces scripts can be compiled against. Interfaces are pure declarations, so
// the compiler links them without any engine subsystem. The core interface is always included.
struct KnownInterface {
    const char* name;
    const NativeInterface& (*get)();
};

constexpr KnownInterface knownInterfaces[] = {
    {"core", &coreInterface},
    {"adventure", &adventure::adventureInterface},
};

const NativeInterface* findInterface(std::string_view name)
{
    for (const KnownInterface& known : knownInterfaces) {
        if (name == known.name) return &known.get();
    }
    return nullptr;
}

struct Input {
    std::string path;
    ScriptRole role = ScriptRole::Room;
};

// `path` relative to `root` when it lies inside it ("Rooms/Kitchen.sch"), else unchanged.
std::string relativeName(const std::string& path, const std::string& root)
{
    if (root.empty()) return path;
    std::string prefix = root;
    if (prefix.back() != '/' && prefix.back() != '\\') prefix += '/';
    if (path.size() > prefix.size() && path.compare(0, prefix.size(), prefix) == 0) {
        std::string name = path.substr(prefix.size());
        for (char& c : name) {
            if (c == '\\') c = '/';
        }
        return name;
    }
    return path;
}

struct Options {
    std::vector<const NativeInterface*> interfaces;
    std::vector<Input> inputs;
    std::string sourceRoot;
    std::string output;
    std::string name;
    bool optimize = true;
    bool tokens = false;
    bool ast = false;
    bool types = false;
    bool ir = false;
    bool irOptimized = false;
    bool disassembly = false;
    bool verify = false;
};

void print(const std::string& title, const std::string& text)
{
    std::printf("=== %s ===\n%s\n", title.c_str(), text.c_str());
}

} // namespace

int main(int argc, char** argv)
{
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument = argv[i];
        if (argument == "-h" || argument == "--help") {
            std::fputs(usage, stdout);
            return 0;
        } else if (argument == "-o" || argument == "--name" || argument == "--source-root") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "%s requires a value\n\n%s", argv[i], usage);
                return 2;
            }
            (argument == "-o" ? options.output : argument == "--name" ? options.name : options.sourceRoot) = argv[++i];
        } else if (argument == "--global" || argument == "--room" || argument == "--actor") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "%s requires a value\n\n%s", argv[i], usage);
                return 2;
            }
            const ScriptRole role = argument == "--global" ? ScriptRole::Global
                                    : argument == "--room" ? ScriptRole::Room
                                                           : ScriptRole::Actor;
            options.inputs.push_back({argv[++i], role});
        } else if (argument == "--interface") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "%s requires a value\n\n%s", argv[i], usage);
                return 2;
            }
            const NativeInterface* interface = findInterface(argv[++i]);
            if (interface == nullptr) {
                std::fprintf(stderr, "Unknown native interface '%s' (see --list-interfaces)\n", argv[i]);
                return 2;
            }
            options.interfaces.push_back(interface);
        } else if (argument == "--list-interfaces") {
            for (const KnownInterface& known : knownInterfaces) {
                const NativeInterface& interface = known.get();
                std::printf("%s\n", interface.name.c_str());
                for (const EnumDeclaration& declaration : interface.enums) {
                    std::string members;
                    for (const std::string& member : declaration.members) members += " " + member;
                    std::printf("  enum %s:%s\n", declaration.name.c_str(), members.c_str());
                }
                for (const NativeDeclaration& declaration : interface.functions) {
                    std::printf("  %s  [%s]\n", formatSignature(declaration).c_str(), toString(declaration.category));
                }
            }
            return 0;
        } else if (argument == "-O0") {
            options.optimize = false;
        } else if (argument == "-O1") {
            options.optimize = true;
        } else if (argument == "--tokens") {
            options.tokens = true;
        } else if (argument == "--ast") {
            options.ast = true;
        } else if (argument == "--types") {
            options.types = true;
        } else if (argument == "--ir") {
            options.ir = true;
        } else if (argument == "--ir-opt") {
            options.irOptimized = true;
        } else if (argument == "--disasm") {
            options.disassembly = true;
        } else if (argument == "--verify") {
            options.verify = true;
        } else if (!argument.empty() && argument[0] == '-') {
            std::fprintf(stderr, "Unknown option %s\n\n%s", argv[i], usage);
            return 2;
        } else {
            options.inputs.push_back({std::string(argument), ScriptRole::Room});
        }
    }
    if (options.inputs.empty()) {
        std::fputs(usage, stderr);
        return 2;
    }
    if (!options.name.empty() && options.inputs.size() != 1) {
        std::fprintf(stderr, "--name needs exactly one input; use --source-root to name several\n\n%s", usage);
        return 2;
    }

    std::vector<SourceFile> sources;
    sources.reserve(options.inputs.size());
    for (std::size_t i = 0; i < options.inputs.size(); ++i) {
        const Input& input = options.inputs[i];
        auto bytes = fs::readFile(input.path, 16u << 20);
        if (!bytes) {
            std::fprintf(stderr, "schuimmc: %s\n", describe(bytes.error()).c_str());
            return 1;
        }
        std::string name = options.name.empty() ? relativeName(input.path, options.sourceRoot) : options.name;
        sources.emplace_back(std::move(name), std::string(reinterpret_cast<const char*>(bytes->data()), bytes->size()),
                             static_cast<std::uint32_t>(i));
    }
    std::vector<ProjectSource> project;
    for (std::size_t i = 0; i < sources.size(); ++i) {
        project.push_back({&sources[i], options.inputs[i].role});
    }
    CompileOptions compileOptions;
    compileOptions.optimize = options.optimize;
    compileOptions.interfaces = options.interfaces;
    const CompileResult result = compileProject(project, compileOptions);

    const auto unitTitle = [&](const char* title, const CompiledUnit& unit) {
        return result.units.size() == 1 ? std::string(title) : concat(title, ": ", unit.source->name());
    };
    for (const CompiledUnit& unit : result.units) {
        if (options.tokens) print(unitTitle("tokens", unit), dumpTokens(unit.tokens));
        if (options.ast && unit.ast) print(unitTitle("ast", unit), dumpAst(*unit.ast));
        if (options.types && unit.ast && !result.diagnostics.hasErrors()) {
            print(unitTitle("types", unit), dumpAst(*unit.ast, &result.semantics));
        }
    }
    if (options.ir && result.success) print("ir", dumpIr(result.ir));
    if (options.irOptimized && result.success) print(options.optimize ? "optimized ir" : "ir (-O0)", dumpIr(result.optimizedIr));
    if (options.disassembly && result.success) print("bytecode", disassemble(result.module));

    const std::string diagnostics = formatDiagnostics(result);
    if (!diagnostics.empty()) {
        std::fputs(diagnostics.c_str(), stderr);
    }
    if (!result.success) {
        std::fprintf(stderr, "schuimmc: %zu error(s), no output written\n", result.diagnostics.errorCount());
        return 1;
    }
    if (!options.output.empty()) {
        auto encoded = writeBytecode(result.module);
        if (!encoded) {
            std::fprintf(stderr, "schuimmc: %s\n", describe(encoded.error()).c_str());
            return 1;
        }
        if (auto status = fs::writeFile(options.output, *encoded); !status) {
            std::fprintf(stderr, "schuimmc: %s\n", describe(status.error()).c_str());
            return 1;
        }
        if (options.verify) {
            auto reread = fs::readFile(options.output);
            if (!reread) {
                std::fprintf(stderr, "schuimmc: %s\n", describe(reread.error()).c_str());
                return 1;
            }
            if (auto module = readBytecode(*reread); !module) {
                std::fprintf(stderr, "schuimmc: written file does not validate: %s\n", describe(module.error()).c_str());
                return 1;
            }
        }
        const std::string inputs = options.inputs.size() == 1 ? options.inputs.front().path
                                                              : concat(options.inputs.size(), " sources");
        std::printf("%s -> %s (%zu bytes, %zu functions%s)\n", inputs.c_str(), options.output.c_str(), encoded->size(),
                    result.module.functions.size(), options.optimize ? "" : ", unoptimized");
    }
    return 0;
}
