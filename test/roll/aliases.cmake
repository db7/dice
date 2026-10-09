cmake_minimum_required(VERSION 3.20)

function(run)
  execute_process(COMMAND ${ARGV} RESULT_VARIABLE result OUTPUT_VARIABLE stdout
                  ERROR_VARIABLE stderr TIMEOUT 30)
  if(NOT result STREQUAL "0")
    message(FATAL_ERROR "${ARGV}: ${result}\n${stdout}${stderr}")
  endif()
endfunction()

set(root "${WORK}/aliases")
file(MAKE_DIRECTORY "${root}")
set(config [=[(callbacks false)
  (module observer
    (subscribes (CAPTURE_EVENT EVENT_MA_READ))
    (publishes (INTERCEPT_EVENT EVENT_MA_READ)))
]=])
set(old "${config}")
string(REPLACE "subscribes" "consumes" old "${old}")
string(REPLACE "publishes" "produces" old "${old}")
string(REPLACE "publishes" "produces" mixed "${config}")
foreach(mode config old mixed)
  file(WRITE "${root}/${mode}.dice" "${${mode}}")
  run("${DICE}" "${root}/${mode}.dice" -o "${root}/${mode}")
  foreach(extension c h)
    file(SHA256 "${root}/${mode}/dice.${extension}" digest)
    if(mode STREQUAL "config")
      set(expected_${extension} "${digest}")
    elseif(NOT digest STREQUAL expected_${extension})
      message(FATAL_ERROR "${mode} aliases changed generated ${extension}")
    endif()
  endforeach()
endforeach()

# Synonyms describe one field and one route, rather than independent entries.
foreach(invalid
    "(module a (subscribes (CAPTURE_EVENT EVENT_MA_READ)) (consumes (CAPTURE_EVENT EVENT_MA_WRITE)))"
    "(module a (produces (INTERCEPT_EVENT EVENT_MA_READ)) (publishes (INTERCEPT_EVENT EVENT_MA_WRITE)))"
)
  file(WRITE "${root}/invalid.dice" "${invalid}")
  execute_process(COMMAND "${DICE}" --check "${root}/invalid.dice"
                  RESULT_VARIABLE result ERROR_VARIABLE stderr)
  if(result STREQUAL "0" OR NOT stderr MATCHES "duplicate")
    message(FATAL_ERROR "Accepted conflicting aliases: ${invalid}\n${stderr}")
  endif()
endforeach()
