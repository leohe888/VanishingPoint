function(add_vp_test test_name)
    add_executable(${test_name} ${ARGN})
    target_include_directories(${test_name} PRIVATE ${PROJECT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(${test_name} PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Test vp_engine)
    set(test_command ${CMAKE_COMMAND}
        "-DVP_TEST_EXECUTABLE=$<TARGET_FILE:${test_name}>"
        "-DVP_TEST_LOG=${CMAKE_CURRENT_BINARY_DIR}/test-results/${test_name}.txt"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/run_qt_test.cmake")
    if(WIN32)
        get_target_property(qt_core_location Qt6::Core IMPORTED_LOCATION)
        get_filename_component(qt_bin_dir "${qt_core_location}" DIRECTORY)
        get_filename_component(compiler_bin_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
        add_test(NAME ${test_name} COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${qt_bin_dir};${compiler_bin_dir};$ENV{PATH}"
            ${test_command})
    else()
        add_test(NAME ${test_name} COMMAND ${test_command})
    endif()
    set_tests_properties(${test_name} PROPERTIES TIMEOUT 120)
endfunction()

foreach(name IN ITEMS canvasnavigation perspectiveplane perspectivetransform perspectivequad
        planecreatetool planeedittool marqueetool vpdocument brushtool clonetool
        vpcontroller floatingselectionprojection floatingselectiontransformtool eyedroppertool healingblend auditinteractions auditboundaries)
    add_vp_test(tst_${name} tst_${name}.cpp)
    set_tests_properties(tst_${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
endforeach()

add_vp_test(tst_qmlintegration tst_qmlintegration.cpp ${PROJECT_SOURCE_DIR}/res.qrc)
set_tests_properties(tst_qmlintegration PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")

add_vp_test(tst_qmlboundaries tst_qmlboundaries.cpp ${PROJECT_SOURCE_DIR}/res.qrc)
target_compile_definitions(tst_qmlboundaries PRIVATE VP_TEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
set_tests_properties(tst_qmlboundaries PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
foreach(target IN ITEMS tst_eyedroppertool tst_canvasnavigation tst_floatingselectiontransformtool)
    target_compile_definitions(${target} PRIVATE
        VP_EYEDROPPER_CURSOR="${PROJECT_SOURCE_DIR}/assets/icons/dropper.png"
        VP_CURSOR_DIR="${PROJECT_SOURCE_DIR}/assets/cursors")
endforeach()

option(VP_BUILD_BENCHMARKS "Build interactive performance checks" OFF)
if(VP_BUILD_BENCHMARKS)
    foreach(name IN ITEMS creation_preview plane_overlay selection brush clone)
        add_executable(vp_${name}_check benchmarks/${name}_check.cpp)
        target_link_libraries(vp_${name}_check PRIVATE vp_engine)
    endforeach()
    target_compile_definitions(vp_plane_overlay_check PRIVATE VP_WITH_AUXILIARY_OVERLAY)
    foreach(name IN ITEMS selection brush clone)
        string(TOUPPER "${name}" stage_name)
        set(VP_${stage_name}_STAGE 0 CACHE STRING "Optimization stage to verify")
        target_compile_definitions(vp_${name}_check PRIVATE VP_${stage_name}_STAGE=${VP_${stage_name}_STAGE})
    endforeach()
endif()

# The software backend does not exercise stencil geometry. Use Windows' default
# hardware backend explicitly to catch unsupported primitive topologies.
option(VP_ENABLE_GPU_TESTS "Register D3D11 grid clipping regression" OFF)
if(VP_ENABLE_GPU_TESTS AND WIN32)
    get_filename_component(vp_compiler_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    add_test(NAME vp_grid_d3d11 COMMAND ${CMAKE_COMMAND} -E env
        "PATH=$<TARGET_FILE_DIR:Qt6::Core>;${vp_compiler_bin};$ENV{PATH}"
        ${CMAKE_COMMAND}
        "-DVP_TEST_EXECUTABLE=$<TARGET_FILE:tst_floatingselectionprojection>"
        "-DVP_TEST_LOG=${CMAKE_CURRENT_BINARY_DIR}/test-results/grid-d3d11.txt"
        -DVP_TEST_FUNCTION=planeGridCoversBothDiagonalHalves
        -P "${CMAKE_CURRENT_SOURCE_DIR}/run_qt_test.cmake")
    set_tests_properties(vp_grid_d3d11 PROPERTIES TIMEOUT 120 LABELS gpu
        ENVIRONMENT "QT_QPA_PLATFORM=windows;QT_QUICK_BACKEND=;QSG_RHI_BACKEND=d3d11;VP_TEST_GPU=1")
endif()
