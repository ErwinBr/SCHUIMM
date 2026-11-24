# schuimm-run: loads precompiled bytecode and runs it headlessly with simulated frames.
# Links the runtime only (no compiler): it shows that bytecode runs without the compiler.
add_executable(schuimm-run main.cpp)
target_link_libraries(schuimm-run PRIVATE Schuimm::ScriptRuntime Schuimm::FileSystem)
schuimm_configure_target(schuimm-run)
