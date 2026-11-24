# Resource layer: asset location, identification, CPU-side loading and decoding, caching.
# Depends on Core and the file system layer only: no windowing, no graphics API. libpng and zlib
# are private implementation details (only Image/PngDecoder.cpp includes png.h).
add_library(schuimm_resource STATIC
    AssetLocator.cpp
    AssetPath.cpp
    Image/ImageDecoder.cpp
    Image/ImageLoader.cpp
    Image/PngDecoder.cpp)
add_library(Schuimm::Resource ALIAS schuimm_resource)
target_link_libraries(schuimm_resource
    PUBLIC Schuimm::Core Schuimm::FileSystem
    PRIVATE Schuimm::ThirdParty::PNG)
schuimm_configure_target(schuimm_resource)
