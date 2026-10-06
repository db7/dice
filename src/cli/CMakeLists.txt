if(CMAKE_CROSSCOMPILING)
  find_program(DICE_HOST_CC NAMES cc clang gcc REQUIRED)
  set(DICE_PACK ${CMAKE_CURRENT_BINARY_DIR}/dice-pack-host)
  add_custom_command(OUTPUT ${DICE_PACK}
    COMMAND ${DICE_HOST_CC} -D_GNU_SOURCE -std=c99 -O2
            ${CMAKE_CURRENT_SOURCE_DIR}/pack.c -o ${DICE_PACK}
    DEPENDS pack.c VERBATIM)
  add_custom_target(dice-pack DEPENDS ${DICE_PACK})
else()
  add_executable(dice-pack pack.c)
  set(DICE_PACK $<TARGET_FILE:dice-pack>)
endif()

file(GLOB_RECURSE ROLL_HEADERS CONFIGURE_DEPENDS
     "${PROJECT_SOURCE_DIR}/include/dice/*.h"
     "${PROJECT_SOURCE_DIR}/deps/libvsync/include/vsync/*.h")
file(GLOB ROLL_MODULES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/src/mod/*.c")
file(GLOB ROLL_TEMPLATES CONFIGURE_DEPENDS "templates/*.in")
set(ROLL_ASSETS ${CMAKE_CURRENT_BINARY_DIR}/assets.h)
add_custom_command(
  OUTPUT ${ROLL_ASSETS}
  COMMAND ${DICE_PACK} ${PROJECT_SOURCE_DIR} ${ROLL_ASSETS}
  DEPENDS dice-pack ${ROLL_HEADERS}
          ${ROLL_MODULES} ${ROLL_TEMPLATES}
          ${PROJECT_SOURCE_DIR}/src/dice/mempool.c
          ${PROJECT_SOURCE_DIR}/src/dice/pubsub.c
          ${PROJECT_SOURCE_DIR}/src/dice/pubsub-box.c
          ${PROJECT_SOURCE_DIR}/src/dice/tweaks.h
          ${PROJECT_SOURCE_DIR}/src/mod/tsan.c.in
          ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.libvsync ${PROJECT_SOURCE_DIR}/LICENSE
  VERBATIM)

add_executable(dice-cli roll.c slots.c ${ROLL_ASSETS})
target_include_directories(dice-cli PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(dice-cli PRIVATE tmplr-lib sxp)
set_target_properties(dice-cli PROPERTIES OUTPUT_NAME dice)
install(TARGETS dice-cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
