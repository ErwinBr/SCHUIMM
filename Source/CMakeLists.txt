# Library layout (arrows point at dependencies):
#
#   schuimm_core                 Common, Image, Logging, Timing, Input (platform independent)
#   schuimm_platform_interface   <- schuimm_core          (headers only)
#   schuimm_filesystem           <- schuimm_core          (paths and file I/O: POSIX or Win32)
#   schuimm_resource             <- schuimm_filesystem    (assets, CPU-side decoding; libpng private)
#   schuimm_graphics_interface   <- schuimm_core          (headers only)
#   schuimm_application          <- both interfaces       (main loop, lifecycle)
#   schuimm_platform             <- platform interface    (exactly one native backend)
#   schuimm_graphics_opengl      <- both interfaces       (OpenGL 3.3 core renderer)
#
# Only the final executable links concrete backends together.
add_subdirectory(Core)
add_subdirectory(Platform)
add_subdirectory(Resource)
add_subdirectory(Graphics)
add_subdirectory(Core/Application)
