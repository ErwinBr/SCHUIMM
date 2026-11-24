# schuimmc: the standalone script compiler. Runs without the engine runtime, window or GPU.
add_executable(schuimmc main.cpp)
target_link_libraries(schuimmc PRIVATE Schuimm::ScriptCompiler Schuimm::FileSystem)
schuimm_configure_target(schuimmc)
