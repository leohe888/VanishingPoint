function(add_vp_test test_name)
    add_executable(${test_name} ${ARGN})
    target_include_directories(${test_name} PRIVATE ${PROJECT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(${test_name} PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Test vp_engine)
    if(WIN32)
        get_target_property(qt_core_location Qt6::Core IMPORTED_LOCATION)
        get_filename_component(qt_bin_dir "${qt_core_location}" DIRECTORY)
        get_filename_component(compiler_bin_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
        add_test(NAME ${test_name} COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${qt_bin_dir};${compiler_bin_dir};$ENV{PATH}"
            $<TARGET_FILE:${test_name}>)
    else()
        add_test(NAME ${test_name} COMMAND ${test_name})
    endif()
endfunction()

foreach(name IN ITEMS canvasnavigation perspectiveplane perspectivetransform perspectivequad
        planecreatetool planeedittool marqueetool vpdocument brushtool clonetool
        vpcontroller floatingimageprojection auditinteractions auditboundaries)
    add_vp_test(tst_${name} tst_${name}.cpp)
    set_tests_properties(tst_${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endforeach()
