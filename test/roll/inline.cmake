function(run)
  execute_process(COMMAND ${ARGV} RESULT_VARIABLE result OUTPUT_VARIABLE stdout
                  ERROR_VARIABLE stderr TIMEOUT 30)
  if(NOT result STREQUAL "0")
    message(FATAL_ERROR "${ARGV}: ${result}\n${stdout}${stderr}")
  endif()
endfunction()
function(expect_failure expected)
  execute_process(COMMAND ${ARGN} RESULT_VARIABLE result OUTPUT_VARIABLE stdout
                  ERROR_VARIABLE stderr TIMEOUT 30)
  if(result STREQUAL "0" OR NOT "${stdout}${stderr}" MATCHES "${expected}")
    message(FATAL_ERROR "Expected '${expected}': ${ARGN}\n${result}\n${stdout}${stderr}")
  endif()
endfunction()

set(out "${WORK}/inline")
file(MAKE_DIRECTORY "${out}" "${out}/plain")
set(flags -std=gnu11 -D_GNU_SOURCE -DDICE_DISPATCH_MODULE
          -DDICE_DISABLE_PUBSUB_CTOR_INIT -Wall -Wextra -Werror -fPIC "-I${out}"
          "-I${SOURCE}/include" "-I${SOURCE}/src/dice"
          "-I${SOURCE}/deps/libvsync/include" "-I${FIXTURES}/inline")
include("${FIXTURES}/core.cmake")
set(link -pthread ${DL_LIBS})
set(lto_modes 0 1)
if(CC_ID MATCHES "Clang")
  list(APPEND lto_modes thin)
endif()
if(APPLE)
  set(shared -dynamiclib)
  set(suffix .dylib)
else()
  set(shared -shared)
  set(suffix .so)
endif()
foreach(callbacks false true)
  set(config "(callbacks ${callbacks})
    (slot source (number 1) (produces (CHAIN_FLOW EVENT_TEST)))
    (module app
      (slot fast (number 5) (option inline) (consumes (CHAIN_FLOW ANY_EVENT)))
      (slot normal (number 6) (consumes (CHAIN_FLOW ANY_EVENT))))")
  file(WRITE "${out}/system.dice" "${config}")
  run("${DICE}" "${out}/system.dice" -o "${out}")
  file(READ "${out}/dice.h" header)
  string(REGEX MATCH "DICE_CONFIG_HASH 0x[0-9a-f]+ULL" hash "${header}")
  string(REPLACE "(option inline)" "" plain "${config}")
  file(WRITE "${out}/plain.dice" "${plain}")
  run("${DICE}" "${out}/plain.dice" -o "${out}/plain")
  file(READ "${out}/plain/dice.h" plain_header)
  if(NOT plain_header MATCHES "${hash}")
    message(FATAL_ERROR "Inlining policy changed the runtime ABI fingerprint")
  endif()
  execute_process(COMMAND "${DICE}" --query "${out}/system.dice"
                  OUTPUT_VARIABLE query RESULT_VARIABLE result)
  if(NOT result EQUAL 0 OR NOT query MATCHES
     "slot app/fast 5 forced callbacks ${callbacks} option inline")
    message(FATAL_ERROR "Missing slot inline option in query: ${query}")
  endif()
  foreach(checked 0 1)
    foreach(lto IN LISTS lto_modes)
      set(build_flags ${flags} -DDICE_CHECK_ROUTES=${checked})
      if(lto STREQUAL "thin")
        list(APPEND build_flags -O3 -flto=thin)
      elseif(lto)
        list(APPEND build_flags -O3 -flto)
      endif()
      run("${CC}" ${build_flags} -c "${out}/dice.c" -o "${out}/dice.o")
      build_core(core_objects "${out}/dice.c" ${build_flags})
      foreach(name handlers publisher)
        run("${CC}" ${build_flags} -c "${FIXTURES}/inline/${name}.c"
            -o "${out}/${name}.o")
      endforeach()
      set(objects "${out}/dice.o" ${core_objects} "${out}/handlers.o"
                  "${out}/publisher.o")
      set(lib "${out}/runtime${suffix}")
      run("${CC}" ${build_flags} ${shared} ${objects} ${link} -o "${lib}")
      run("${CC}" ${build_flags} "${FIXTURES}/inline/main.c" "${lib}"
          ${link} -o "${out}/app")
      run("${out}/app")
      if(NOT callbacks)
        # Inline wrappers must retain strong external linkage, so duplicate
        # consumers still fail to link rather than being silently coalesced.
        run("${CC}" ${build_flags} -DDUPLICATE -c
            "${FIXTURES}/inline/handlers.c" -o "${out}/duplicate.o")
        expect_failure("dice_slot_" "${CC}" ${build_flags} ${shared}
            ${objects} "${out}/duplicate.o" ${link} -o "${lib}")
      endif()
    endforeach()
  endforeach()
endforeach()
file(MAKE_DIRECTORY "${out}/self")
file(WRITE "${out}/self.dice" "(callbacks false) (import (dice self))")
run("${DICE}" "${out}/self.dice" -o "${out}/self")
# The real catch-all self handlers republish events. They must also compile
# without optimization, where a forced-inline cycle cannot rely on LTO.
run("${CC}" ${flags} -DDICE_MODULE_SLOT=SLOT_DICE_SELF
    -include "${out}/self/dice.h" -c "${SOURCE}/src/mod/self.c"
    -o "${out}/self.o")
foreach(config "(slot a (option))" "(slot a (option inline extra))"
               "(slot a (option inline) (option inline))"
               "(slot a (option noinline))" "(option inline)"
               "(module a (option inline))")
  file(WRITE "${out}/bad.dice" "${config}")
  expect_failure("expects|duplicate|unknown" "${DICE}" --check "${out}/bad.dice")
endforeach()
