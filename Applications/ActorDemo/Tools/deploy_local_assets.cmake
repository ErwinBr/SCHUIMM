# Copies Applications/ActorDemo/LocalAssets over the deployed assets if that folder exists
# (art that is not in the repository; see LocalAssets/README.md). Run with
#   cmake -DSOURCE=<LocalAssets> -DDESTINATION=<deployed Assets> -P deploy_local_assets.cmake
if(EXISTS "${SOURCE}")
    file(COPY "${SOURCE}/" DESTINATION "${DESTINATION}" PATTERN "README.md" EXCLUDE)
endif()
