add_library(schuimm_core STATIC
    Common/Assert.cpp
    Common/Error.cpp
    Common/StringBuilder.cpp
    Input/InputState.cpp
    Input/InputTypes.cpp
    Logging/Logger.cpp
    Logging/StandardSinks.cpp
    Timing/Clock.cpp
)
add_library(Schuimm::Core ALIAS schuimm_core)
target_include_directories(schuimm_core PUBLIC "${PROJECT_SOURCE_DIR}/Source")
target_compile_features(schuimm_core PUBLIC cxx_std_23)
schuimm_configure_target(schuimm_core)

find_package(Threads REQUIRED)
target_link_libraries(schuimm_core PUBLIC Threads::Threads)
