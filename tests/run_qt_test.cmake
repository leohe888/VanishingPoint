# Qt GUI builds on Windows can return an exit code without emitting console text.
# A file logger keeps failure details available both in CTest and on disk.
if(NOT DEFINED VP_TEST_EXECUTABLE OR NOT DEFINED VP_TEST_LOG)
    message(FATAL_ERROR "VP_TEST_EXECUTABLE and VP_TEST_LOG are required")
endif()
get_filename_component(log_dir "${VP_TEST_LOG}" DIRECTORY)
file(MAKE_DIRECTORY "${log_dir}")
file(WRITE "${VP_TEST_LOG}" "")
set(test_functions)
if(DEFINED VP_TEST_FUNCTION)
    list(APPEND test_functions "${VP_TEST_FUNCTION}")
endif()
execute_process(COMMAND "${VP_TEST_EXECUTABLE}" ${test_functions} -o "${VP_TEST_LOG},txt"
    RESULT_VARIABLE test_result
    OUTPUT_VARIABLE test_stdout
    ERROR_VARIABLE test_stderr)
file(READ "${VP_TEST_LOG}" test_report)
message("${test_report}${test_stdout}${test_stderr}")
if(NOT "${test_result}" STREQUAL "0")
    message(FATAL_ERROR "Qt test failed (${test_result}): ${VP_TEST_EXECUTABLE}")
endif()
