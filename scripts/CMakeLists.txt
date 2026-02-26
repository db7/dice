# Copyright (C) 2025 Huawei Technologies Co., Ltd.
# SPDX-License-Identifier: 0BSD

# Add the dispatcher generator tool
add_executable(generate-dispatcher scripts/generate-dispatcher-finegrained.c)

# Add the module order resolver tool
add_executable(generate-module-order scripts/generate-module-order.c)

# Link against tmplr library if available
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(TMPLR tmplr)
    if(TMPLR_FOUND)
        target_link_libraries(generate-dispatcher ${TMPLR_LIBRARIES})
        target_include_directories(generate-dispatcher PRIVATE ${TMPLR_INCLUDE_DIRS})
        target_link_libraries(generate-module-order ${TMPLR_LIBRARIES})
        target_include_directories(generate-module-order PRIVATE ${TMPLR_INCLUDE_DIRS})
    else()
        # Fallback to system tmplr
        find_library(TMPLR_LIBRARY tmplr)
        if(TMPLR_LIBRARY)
            target_link_libraries(generate-dispatcher ${TMPLR_LIBRARY})
            target_link_libraries(generate-module-order ${TMPLR_LIBRARY})
        endif()
    endif()
endif()

# Install the tools
install(
    TARGETS generate-dispatcher
    DESTINATION bin
    PERMISSIONS
        OWNER_READ
        OWNER_WRITE
        OWNER_EXECUTE
        GROUP_READ
        GROUP_EXECUTE
        WORLD_READ
        WORLD_EXECUTE)

install(
    TARGETS generate-module-order
    DESTINATION bin
    PERMISSIONS
        OWNER_READ
        OWNER_WRITE
        OWNER_EXECUTE
        GROUP_READ
        GROUP_EXECUTE
        WORLD_READ
        WORLD_EXECUTE)

configure_file(dice dice)
install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/dice
    DESTINATION bin
    PERMISSIONS
        OWNER_READ
        OWNER_WRITE
        OWNER_EXECUTE
        GROUP_READ
        GROUP_EXECUTE
        WORLD_READ
        WORLD_EXECUTE)

# for tests
set(DICE_SCRIPT
    ${PROJECT_SOURCE_DIR}/scripts/dice
    PARENT_SCOPE)