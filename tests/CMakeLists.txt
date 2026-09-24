function(add_vp_test test_name)
    add_executable(${test_name} ${ARGN})
    target_include_directories(${test_name} PRIVATE ${PROJECT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(${test_name} PRIVATE
        Qt${QT_VERSION_MAJOR}::Core
        Qt${QT_VERSION_MAJOR}::Gui
        Qt${QT_VERSION_MAJOR}::Test)
    if(WIN32)
        get_target_property(qt_core_location Qt${QT_VERSION_MAJOR}::Core IMPORTED_LOCATION)
        get_filename_component(qt_bin_dir "${qt_core_location}" DIRECTORY)
        get_filename_component(compiler_bin_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
        add_test(NAME ${test_name} COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${qt_bin_dir};${compiler_bin_dir};$ENV{PATH}"
            $<TARGET_FILE:${test_name}>)
    else()
        add_test(NAME ${test_name} COMMAND ${test_name})
    endif()
endfunction()

set(VP_GEOMETRY_SOURCES
    ${PROJECT_SOURCE_DIR}/src/core/perspectivequad.cpp
    ${PROJECT_SOURCE_DIR}/src/core/perspectiveplane.cpp
    ${PROJECT_SOURCE_DIR}/src/core/perspectivetransform.cpp)

add_vp_test(tst_perspectiveplane
    tst_perspectiveplane.cpp
    ${VP_GEOMETRY_SOURCES})

add_vp_test(tst_perspectivetransform
    tst_perspectivetransform.cpp
    ${PROJECT_SOURCE_DIR}/src/core/perspectivetransform.cpp)

add_vp_test(tst_planecreatetool
    tst_planecreatetool.cpp
    ${PROJECT_SOURCE_DIR}/src/core/planecreatetool.cpp
    ${VP_GEOMETRY_SOURCES})

add_vp_test(tst_planeedittool
    tst_planeedittool.cpp
    ${PROJECT_SOURCE_DIR}/src/core/planeedittool.cpp
    ${VP_GEOMETRY_SOURCES})

add_vp_test(tst_canvasdocument
    tst_canvasdocument.cpp
    ${PROJECT_SOURCE_DIR}/src/core/canvasdocument.cpp
    ${VP_GEOMETRY_SOURCES})

add_vp_test(tst_brushtool
    tst_brushtool.cpp
    ${PROJECT_SOURCE_DIR}/src/core/canvasdocument.cpp
    ${PROJECT_SOURCE_DIR}/src/core/brushtool.cpp
    ${PROJECT_SOURCE_DIR}/src/core/paintengine.cpp
    ${VP_GEOMETRY_SOURCES})

add_vp_test(tst_clonetool
    tst_clonetool.cpp
    ${PROJECT_SOURCE_DIR}/src/core/canvasdocument.cpp
    ${PROJECT_SOURCE_DIR}/src/core/clonetool.cpp
    ${PROJECT_SOURCE_DIR}/src/core/clonestampengine.cpp
    ${VP_GEOMETRY_SOURCES})

add_vp_test(tst_floatingimageprojection
    tst_floatingimageprojection.cpp
    ${PROJECT_SOURCE_DIR}/src/core/floatingimage.cpp
    ${PROJECT_SOURCE_DIR}/src/core/floatingimageprojection.cpp
    ${PROJECT_SOURCE_DIR}/src/core/floatingimagetransformtool.cpp
    ${VP_GEOMETRY_SOURCES})
