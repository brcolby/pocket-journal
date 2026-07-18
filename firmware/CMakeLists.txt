cmake_minimum_required(VERSION 3.16)

# Keep source-archive builds aligned with the release even when Git metadata is
# unavailable. Release builders may override this with -D PROJECT_VER=X.Y.Z.
if(NOT DEFINED PROJECT_VER)
    set(PROJECT_VER "0.0.0")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(pocket_journal)

# ESP-IDF's built-in app_check_size guards against overflow. This stricter
# project gate preserves capacity for signed-image padding and planned fixes.
idf_build_get_property(pj_python PYTHON)
idf_build_get_property(pj_build_dir BUILD_DIR)
idf_build_get_property(pj_project_bin PROJECT_BIN)
set(pj_app_size_signing_args)
if(NOT CONFIG_SECURE_BOOT_BUILD_SIGNED_BINARIES)
    list(APPEND pj_app_size_signing_args --unsigned-secure-boot-v2)
endif()
add_custom_command(TARGET app POST_BUILD
    COMMAND "${pj_python}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_app_size.py"
        --image "${pj_build_dir}/${pj_project_bin}"
        --partition-table "${pj_build_dir}/partition_table/partition-table.bin"
        --min-headroom 0x60000
        --configured-max "${CONFIG_PJ_OTA_MAX_IMAGE_BYTES}"
        ${pj_app_size_signing_args}
    COMMAND_EXPAND_LISTS
    VERBATIM)
