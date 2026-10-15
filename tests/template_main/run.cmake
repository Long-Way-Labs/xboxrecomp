# One template_main test (MODE): run the template's main.c, built with stub
# guest code, where there is no XBE, and check how it failed.
#   no_xbe:    exit 1, "[FATAL] load XBE" in its output.
#   stdio_log: RECOMP_STDIO_LOG=<file>: exit 1, the line in the file, nothing
#              on the console.
# RECOMP_HEADLESS=1 so a Windows build shows no dialog and a POSIX one opens
# no window. The run directory has no game/ beside it; under Proton the
# emulator runs the exe from the build directory, which has none either.
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
set(ENV{RECOMP_HEADLESS} 1)
unset(ENV{RECOMP_STDIO_LOG})
set(log "${WORK}/stdio.log")
if(MODE STREQUAL "stdio_log")
    set(ENV{RECOMP_STDIO_LOG} "${log}")
endif()

execute_process(COMMAND ${EMU} "${EXE}"
    WORKING_DIRECTORY "${WORK}"
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
set(console "${out}${err}")

if(NOT rc STREQUAL "1")
    message(FATAL_ERROR "exit status ${rc}, want 1; output:\n${console}")
endif()
if(MODE STREQUAL "no_xbe")
    if(NOT console MATCHES "\\[FATAL\\] load XBE")
        message(FATAL_ERROR "no '[FATAL] load XBE' in the output:\n${console}")
    endif()
else()
    if(NOT EXISTS "${log}")
        message(FATAL_ERROR "RECOMP_STDIO_LOG file not written; output:\n${console}")
    endif()
    file(READ "${log}" text)
    if(NOT text MATCHES "\\[FATAL\\] load XBE")
        message(FATAL_ERROR "no '[FATAL] load XBE' in ${log}:\n${text}")
    endif()
    # The emulator's own lines are not the program's; anything else is.
    string(REGEX REPLACE "proton_run[^\n]*\n?" "" rest "${console}")
    if(rest MATCHES "[^ \t\r\n]")
        message(FATAL_ERROR "output reached the console with RECOMP_STDIO_LOG set:\n${console}")
    endif()
endif()
message(STATUS "template_main ${MODE}: ok")
