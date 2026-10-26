cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Human-readable firmware version (vMAJOR.MINOR[.PATCH][-YYYY-MM]), the only place it is defined.
# Shown in Settings, the boot log and the status replies as FW_VERSION.
# The test build uses this value; .github/workflows/release-image.yml rewrites this line
# to the release tag so the published pocketgame.bin reports the release version.
set(POCKETGAME_FW_VERSION "v1.0-2026-10")
idf_build_set_property(COMPILE_DEFINITIONS "FW_VERSION=\"${POCKETGAME_FW_VERSION}\"" APPEND)

project(pocketgame)
