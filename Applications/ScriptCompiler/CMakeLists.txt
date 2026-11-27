# schuimmc: the standalone script compiler. Runs without a window, GPU or audio; --game reads the
# game's resources through the game build library (GameBuild.h).
add_executable(schuimmc main.cpp)
target_link_libraries(schuimmc PRIVATE Schuimm::GameBuild) # With the compiler, the adventure API and the file system
schuimm_configure_target(schuimmc)
