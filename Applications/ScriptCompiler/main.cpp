// schuimmc: compiles a SCHUIMM script (.sch) into bytecode (.schb) and prints debug dumps.
//
//   schuimmc [options] <input.sch>
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
    "Usage: schuimmc [options] <input.sch>\n"
    "\n"
    "Compiles a SCHUIMM script into platform-independent bytecode.\n"
    "\n"
    "Options:\n"
    "  -o FILE        Write the bytecode to FILE (default: none, only check)\n"
    "  -O0            Disable the optimizer\n"
    "  -O1            Enable the optimizer (default)\n"
    "  --name NAME    Source name recorded in diagnostics and debug information\n"
    "                 (default: the input path as given)\n"
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

struct Options {
    std::vector<const NativeInterface*> interfaces;
    std::string input;
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
        } else if (argument == "-o" || argument == "--name") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "%s requires a value\n\n%s", argv[i], usage);
                return 2;
            }
            (argument == "-o" ? options.output : options.name) = argv[++i];
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
        } else if (options.input.empty()) {
            options.input = std::string(argument);
        } else {
            std::fprintf(stderr, "Only one input file is accepted\n\n%s", usage);
            return 2;
        }
    }
    if (options.input.empty()) {
        std::fputs(usage, stderr);
        return 2;
    }

    auto bytes = fs::readFile(options.input, 16u << 20);
    if (!bytes) {
        std::fprintf(stderr, "schuimmc: %s\n", describe(bytes.error()).c_str());
        return 1;
    }
    const SourceFile source(options.name.empty() ? options.input : options.name,
                            std::string(reinterpret_cast<const char*>(bytes->data()), bytes->size()));
    CompileOptions compileOptions;
    compileOptions.optimize = options.optimize;
    compileOptions.interfaces = options.interfaces;
    const CompileResult result = compile(source, compileOptions);

    if (options.tokens) print("tokens", dumpTokens(result.tokens));
    if (options.ast && result.ast) print("ast", dumpAst(*result.ast));
    if (options.types && result.ast && !result.diagnostics.hasErrors()) print("types", dumpAst(*result.ast, &result.semantics));
    if (options.ir && result.success) print("ir", dumpIr(result.ir));
    if (options.irOptimized && result.success) print(options.optimize ? "optimized ir" : "ir (-O0)", dumpIr(result.optimizedIr));
    if (options.disassembly && result.success) print("bytecode", disassemble(result.module));

    const std::string diagnostics = formatDiagnostics(result, source);
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
        std::printf("%s -> %s (%zu bytes, %zu functions%s)\n", options.input.c_str(), options.output.c_str(), encoded->size(),
                    result.module.functions.size(), options.optimize ? "" : ", unoptimized");
    }
    return 0;
}
