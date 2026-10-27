cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Human-readable firmware version (vMAJOR.MINOR[.PATCH][-YYYY-MM]), the only place it is defined.
# Shown in Settings, the boot log and the status replies as FW_VERSION.
# Keep it equal to the next release tag so the test build and the release report the same
# version; .github/workflows/release-image.yml also rewrites this line to the release tag
# so the published pocketgame.bin always reports exactly the tag.
set(POCKETGAME_FW_VERSION "v1.2.0")
idf_build_set_property(COMPILE_DEFINITIONS "FW_VERSION=\"${POCKETGAME_FW_VERSION}\"" APPEND)

project(pocketgame)
