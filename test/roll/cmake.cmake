function(run)
  execute_process(
    COMMAND ${ARGV}
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
    TIMEOUT 120)
  if(NOT result STREQUAL "0")
    message(FATAL_ERROR "${ARGV}: ${result}\n${stdout}${stderr}")
  endif()
endfunction()

set(root "${WORK}/cmake-consumer")
set(build "${root}/build")
file(MAKE_DIRECTORY "${root}/import path/app" "${root}/import path/dice")
file(REMOVE_RECURSE "${root}/extra definitions")
file(MAKE_DIRECTORY "${root}/extra definitions")
file(WRITE "${root}/extra definitions/first.dice" "(slot first)\n")
file(COPY "${FIXTURES}/cmake/" DESTINATION "${root}")
file(COPY "${SOURCE}/examples/roll/interceptors.dice" DESTINATION "${root}")
file(COPY "${SOURCE}/src/mod/self.dice" DESTINATION "${root}/import path/dice")
file(
  WRITE "${root}/foo.dice"
  [=[
(callbacks true)
(include "interceptors.dice")
(import (app observer-routes))
(import (extra))
]=])
set(routes
    [=[
(dice (schema 1)
  (include "types-ids.dice")
  (slot observer (number 32) (after (dice self))
    (consumes (CHAIN_CAPTURE_BEFORE EVENT_MALLOC)
              (CHAIN_CAPTURE_EVENT EVENT_THREAD_START EVENT_THREAD_EXIT))))
]=])
file(WRITE "${root}/import path/app/observer-routes.dice" "${routes}")
file(WRITE "${root}/import path/app/types-ids.dice"
     "(dice (schema 1) (events (EVENT_USER 200)))\n")
file(
  WRITE "${root}/bar.dice"
  [=[
(dice (schema 1) (callbacks false)
  (include "interceptors.dice")
  (events (EVENT_USER 400))
  (module app (slot observer (number 64) (after (dice self))
    (consumes (CHAIN_CAPTURE_BEFORE EVENT_MALLOC)
              (CHAIN_CAPTURE_EVENT EVENT_THREAD_START EVENT_THREAD_EXIT)))))
]=])
file(
  WRITE "${root}/direct.dice"
  [=[
(dice (schema 1) (callbacks false)
  (slot source (produces (CHAIN_USER EVENT_USER)))
  (slot observer (consumes (CHAIN_USER EVENT_USER))))
]=])
set(configure
    "${CMAKE_COMMAND}"
    -S
    "${root}"
    -B
    "${build}"
    "-DDICE_SOURCE=${SOURCE}"
    "-DCMAKE_C_COMPILER=${CC}"
    "-DCMAKE_CXX_COMPILER=${CXX}")
if(GENERATOR)
  list(APPEND configure -G "${GENERATOR}")
endif()
if(MAKE)
  list(APPEND configure "-DCMAKE_MAKE_PROGRAM=${MAKE}")
endif()
run(${configure})
run("${CMAKE_COMMAND}" --build "${build}" -j 4)
run("${build}/foo-app" 32 200)
run("${build}/bar-app" 64 400)
run("${build}/direct")
run("${build}/objects-app")

file(TIMESTAMP "${build}/foo-dice/dice.h" foo_time "%s")
file(TIMESTAMP "${build}/bar-dice/dice.h" bar_time "%s")
run("${CMAKE_COMMAND}" -E sleep 1)
run("${CMAKE_COMMAND}" --build "${build}" -j 4)
file(TIMESTAMP "${build}/foo-dice/dice.h" now "%s")
if(NOT now STREQUAL foo_time)
  message(FATAL_ERROR "Unchanged configuration was regenerated")
endif()

# Directory membership is a dependency, including additions and removals.
file(WRITE "${root}/extra definitions/second.dice" "(slot second)\n")
run("${CMAKE_COMMAND}" --build "${build}" -j 4)
file(READ "${build}/foo-dice/dice.h" header)
if(NOT header MATCHES "#define SLOT_EXTRA_SECOND ")
  message(FATAL_ERROR "Adding a namespace member did not regenerate foo")
endif()
run("${CMAKE_COMMAND}" -E sleep 1)
file(REMOVE "${root}/extra definitions/second.dice")
run("${CMAKE_COMMAND}" --build "${build}" -j 4)
file(READ "${build}/foo-dice/dice.h" header)
if(header MATCHES "#define SLOT_EXTRA_SECOND ")
  message(FATAL_ERROR "Removing a namespace member did not regenerate foo")
endif()

# An include inside an imported library must regenerate foo.
file(WRITE "${root}/import path/app/types-ids.dice"
     "(dice (schema 1) (events (EVENT_USER 201)))\n")
run("${CMAKE_COMMAND}" --build "${build}" -j 4)
run("${build}/foo-app" 32 201)
file(TIMESTAMP "${build}/bar-dice/dice.h" now "%s")
if(NOT now STREQUAL bar_time)
  message(FATAL_ERROR "Changing foo regenerated unrelated bar configuration")
endif()

# Change the include graph itself, then edit the newly introduced dependency.
run("${CMAKE_COMMAND}" -E sleep 1)
string(REPLACE "types-ids.dice" "new-types.dice" routes "${routes}")
file(WRITE "${root}/import path/app/observer-routes.dice" "${routes}")
file(WRITE "${root}/import path/app/new-types.dice"
     "(dice (schema 1) (events (EVENT_USER 202)))\n")
run("${CMAKE_COMMAND}" --build "${build}" -j 4)
run("${build}/foo-app" 32 202)
run("${CMAKE_COMMAND}" -E sleep 1)
file(WRITE "${root}/import path/app/new-types.dice"
     "(dice (schema 1) (events (EVENT_USER 203)))\n")
string(REPLACE "(number 32)" "(number 33)" routes "${routes}")
file(WRITE "${root}/import path/app/observer-routes.dice" "${routes}")

# Moving a built-in slot must also rebuild the interceptor implementation.
file(READ "${root}/import path/dice/self.dice" self)
string(REPLACE "(slot self" "(slot self (number 7)" self "${self}")
file(WRITE "${root}/import path/dice/self.dice" "${self}")
run("${CMAKE_COMMAND}" --build "${build}" -j 4)
run("${build}/foo-app" 33 203)
run("${build}/bar-app" 64 400)
run("${build}/direct")

execute_process(
  COMMAND ${configure} -DBAD_MODULE=dice-missing
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr)
if(result STREQUAL "0" OR NOT "${stdout}${stderr}" MATCHES
                          "unknown or unsupported module")
  message(
    FATAL_ERROR "Missing diagnostic for unknown module: ${stdout}${stderr}")
endif()
# Restore a reusable successful build directory.
run(${configure} -DBAD_MODULE=)
