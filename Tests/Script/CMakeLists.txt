# Scripting: lexer, parser, type checker, IR, optimizer, bytecode, VM, natives, scheduler and
# end-to-end tests. Headless and deterministic (simulated time, no sleeps).
schuimm_add_test(ScriptTests
    SOURCES
        BytecodeTests.cpp
        CoroutineTests.cpp
        EndToEndTests.cpp
        IrTests.cpp
        LanguageTypesTests.cpp
        LexerTests.cpp
        NativeTests.cpp
        ObjectTests.cpp
        OptimizerTests.cpp
        ParserTests.cpp
        ProjectTests.cpp
        SchedulerTests.cpp
        TypeCheckerTests.cpp
        VmTests.cpp
    LIBRARIES Schuimm::ScriptCompiler Schuimm::ScriptRuntime Schuimm::FileSystem
    LABELS unit)
target_compile_definitions(ScriptTests PRIVATE
    SCHUIMM_TEST_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}"
    SCHUIMM_SCRIPT_EXAMPLES_DIR="${PROJECT_SOURCE_DIR}/Documentation/Scripting/Examples"
    SCHUIMM_DEMO_SCRIPTS_DIR="${PROJECT_SOURCE_DIR}/Applications/EngineDemo/Scripts")

# Command-line tools: compile an example with schuimmc and run the bytecode with schuimm-run.
if(TARGET schuimmc AND TARGET schuimm-run)
    set(example "${PROJECT_SOURCE_DIR}/Documentation/Scripting/Examples/counter.sch")
    set(bytecode "${CMAKE_CURRENT_BINARY_DIR}/counter.schb")
    add_test(NAME ScriptCompilerCli
        COMMAND schuimmc "${example}" -o "${bytecode}" --verify --name counter.sch)
    add_test(NAME ScriptRunnerCli COMMAND schuimm-run "${bytecode}" --dt 0.5)
    set_tests_properties(ScriptCompilerCli PROPERTIES LABELS unit FIXTURES_SETUP script_bytecode)
    set_tests_properties(ScriptRunnerCli PROPERTIES LABELS unit FIXTURES_REQUIRED script_bytecode
        PASS_REGULAR_EXPRESSION "Welcome to SCHUIMM!.*0.*1.*2.*Finished!")
    # Object handlers: compile a script with an object declaration and dispatch its handlers.
    set(objects_bytecode "${CMAKE_CURRENT_BINARY_DIR}/objects.schb")
    add_test(NAME ScriptCompilerObjectsCli
        COMMAND schuimmc "${PROJECT_SOURCE_DIR}/Tests/Script/Data/objects.sch" -o "${objects_bytecode}" --verify)
    add_test(NAME ScriptRunnerDispatchCli
        COMMAND schuimm-run "${objects_bytecode}" --dt 0.25 --dispatch lamp.look_at=7 --dispatch lamp.use
                --dispatch lamp.open)
    set_tests_properties(ScriptCompilerObjectsCli PROPERTIES LABELS unit FIXTURES_SETUP script_objects_bytecode)
    set_tests_properties(ScriptRunnerDispatchCli PROPERTIES LABELS unit FIXTURES_REQUIRED script_objects_bytecode
        PASS_REGULAR_EXPRESSION "You see a lamp\\..*object#7.*Click\\..*The lamp is on\\.")
    # Project compilation: two sources, named relative to their root, compiled into one program.
    set(project_dir "${PROJECT_SOURCE_DIR}/Tests/Script/Data/Project")
    set(project_bytecode "${CMAKE_CURRENT_BINARY_DIR}/project.schb")
    add_test(NAME ScriptCompilerProjectCli
        COMMAND schuimmc --source-root "${project_dir}" --global "${project_dir}/Scripts/Global.sch"
                --room "${project_dir}/Rooms/Kitchen.sch" -o "${project_bytecode}" --verify --disasm)
    set_tests_properties(ScriptCompilerProjectCli PROPERTIES LABELS unit FIXTURES_SETUP script_project_bytecode
        PASS_REGULAR_EXPRESSION "s0 room \"Rooms/Kitchen.sch\".*s1 global \"Scripts/Global.sch\"")
    add_test(NAME ScriptRunnerProjectCli COMMAND schuimm-run "${project_bytecode}")
    set_tests_properties(ScriptRunnerProjectCli PROPERTIES LABELS unit FIXTURES_REQUIRED script_project_bytecode
        PASS_REGULAR_EXPRESSION "Hello, kitchen")
    add_test(NAME ScriptCompilerCliRejectsErrors
        COMMAND schuimmc "${PROJECT_SOURCE_DIR}/Tests/Script/Data/type_error.sch")
    set_tests_properties(ScriptCompilerCliRejectsErrors PROPERTIES LABELS unit WILL_FAIL TRUE)
endif()
