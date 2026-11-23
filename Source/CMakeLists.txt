# Library layout (arrows point at dependencies):
#
#   schuimm_core                 Common, Logging, Timing, Input (platform independent)
#   schuimm_platform_interface   <- schuimm_core          (headers only)
#   schuimm_graphics_interface   <- schuimm_core          (headers only)
#   schuimm_application          <- both interfaces       (main loop, lifecycle)
#   schuimm_platform             <- platform interface    (exactly one native backend)
#   schuimm_graphics_opengl      <- both interfaces       (OpenGL 3.3 core renderer)
#
# Only the final executable links concrete backends together.
add_subdirectory(Core)
add_subdirectory(Platform)
add_subdirectory(Graphics)
add_subdirectory(Core/Application)
