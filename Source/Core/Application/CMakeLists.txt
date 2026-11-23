# Application lifecycle and main loop. Talks to platform and graphics only through their interfaces.
add_library(schuimm_application STATIC Application.cpp)
add_library(Schuimm::Application ALIAS schuimm_application)
target_link_libraries(schuimm_application PUBLIC
    Schuimm::Core Schuimm::PlatformInterface Schuimm::GraphicsInterface)
schuimm_configure_target(schuimm_application)
