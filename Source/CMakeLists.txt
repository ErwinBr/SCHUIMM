# Library layout (arrows point at dependencies):
#
#   schuimm_core                 Common, Image, Logging, Timing, Input (platform independent)
#   schuimm_platform_interface   <- schuimm_core          (headers only)
#   schuimm_filesystem           <- schuimm_core          (paths and file I/O: POSIX or Win32)
#   schuimm_resource             <- schuimm_filesystem    (assets, CPU-side decoding; libpng private)
#   schuimm_graphics             <- schuimm_core          (interfaces, camera, sprite batching; no graphics API)
#   schuimm_graphics_resources   <- graphics, resource    (texture cache, fonts: assets -> GPU resources)
#   schuimm_audio                <- schuimm_core          (playback, mixer; no audio API)
#   schuimm_audio_miniaudio      <- schuimm_audio         (output device; miniaudio private)
#   schuimm_application          <- interfaces, audio     (main loop, lifecycle)
#   schuimm_platform             <- platform interface    (exactly one native backend)
#   schuimm_graphics_opengl      <- both interfaces       (OpenGL 3.3 core renderer)
#
# Only the final executable links concrete backends together.
add_subdirectory(Core)
add_subdirectory(Platform)
add_subdirectory(Resource)
add_subdirectory(Graphics)
add_subdirectory(GraphicsResources)
add_subdirectory(Audio)
add_subdirectory(Core/Application)
