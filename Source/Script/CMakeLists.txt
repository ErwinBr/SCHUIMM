# Scripting: three platform-independent libraries (no platform, graphics or audio dependency).
#
#   schuimm_script_bytecode   <- schuimm_core              format, opcodes, shared semantics, reader/validator
#   schuimm_script_compiler   <- schuimm_script_bytecode   source -> tokens -> AST -> types -> IR -> bytecode
#   schuimm_script_runtime    <- schuimm_script_bytecode   VM, natives, coroutine scheduler, ScriptHost
#
# The runtime never links the compiler: a game ships precompiled bytecode only.
add_library(schuimm_script_bytecode STATIC
    Bytecode/BytecodeReader.cpp
    Bytecode/BytecodeWriter.cpp
    Bytecode/Crc32.cpp
    Bytecode/Disassembler.cpp
    Bytecode/Opcode.cpp
    Bytecode/ScriptType.cpp
    Bytecode/Semantics.cpp)
add_library(Schuimm::ScriptBytecode ALIAS schuimm_script_bytecode)
target_link_libraries(schuimm_script_bytecode PUBLIC Schuimm::Core)
schuimm_configure_target(schuimm_script_bytecode)

add_library(schuimm_script_compiler STATIC
    Compiler/AstPrinter.cpp
    Compiler/CodeGen.cpp
    Compiler/Compiler.cpp
    Compiler/Diagnostics.cpp
    Compiler/Ir.cpp
    Compiler/IrBuilder.cpp
    Compiler/Lexer.cpp
    Compiler/Optimizer.cpp
    Compiler/Parser.cpp
    Compiler/SourceFile.cpp
    Compiler/Token.cpp
    Compiler/TypeChecker.cpp
    Compiler/Types.cpp)
add_library(Schuimm::ScriptCompiler ALIAS schuimm_script_compiler)
target_link_libraries(schuimm_script_compiler PUBLIC Schuimm::ScriptBytecode)
schuimm_configure_target(schuimm_script_compiler)

add_library(schuimm_script_runtime STATIC
    Runtime/Coroutine.cpp
    Runtime/Interpreter.cpp
    Runtime/NativeRegistry.cpp
    Runtime/Program.cpp
    Runtime/ScriptHost.cpp
    Runtime/ScriptScheduler.cpp
    Runtime/StandardNatives.cpp
    Runtime/Value.cpp)
add_library(Schuimm::ScriptRuntime ALIAS schuimm_script_runtime)
target_link_libraries(schuimm_script_runtime PUBLIC Schuimm::ScriptBytecode)
schuimm_configure_target(schuimm_script_runtime)
