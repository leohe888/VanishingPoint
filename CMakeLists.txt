cmake_minimum_required(VERSION 3.16)

project(VanishingPoint VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.11 REQUIRED COMPONENTS Core Quick)
include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
endif()

set(PROJECT_SOURCES
        src/main.cpp
        src/canvas/vpcanvas.cpp
        src/canvas/vpcanvas.h
        src/canvas/vpcontroller.cpp
        src/canvas/vpcontroller.h
        src/core/vpdocument.cpp
        src/core/vpdocument.h
        src/tools/brushtool.cpp
        src/tools/brushtool.h
        src/tools/clonetool.cpp
        src/tools/clonetool.h
        src/core/floatingimage.cpp
        src/core/floatingimage.h
        src/core/floatingimageprojection.cpp
        src/core/floatingimageprojection.h
        src/tools/floatingimagetransformtool.cpp
        src/tools/floatingimagetransformtool.h
        src/tools/planeedittool.cpp
        src/tools/planeedittool.h
        src/tools/marqueetool.cpp
        src/tools/marqueetool.h
        src/tools/planecreatetool.cpp
        src/tools/planecreatetool.h
        src/core/perspectivequad.cpp
        src/core/perspectivequad.h
        src/core/perspectiveplane.cpp
        src/core/perspectiveplane.h
        src/core/perspectivetransform.cpp
        src/core/perspectivetransform.h
        src/core/scenerenderer.cpp
        src/core/scenerenderer.h
        res.qrc
)


set(VP_LIBRARY_SOURCES ${PROJECT_SOURCES})
list(FILTER VP_LIBRARY_SOURCES EXCLUDE REGEX "(src/main.cpp|res.qrc)$")
add_library(vp_engine STATIC ${VP_LIBRARY_SOURCES})
target_include_directories(vp_engine PUBLIC src)
target_link_libraries(vp_engine PUBLIC Qt6::Core Qt6::Quick)

qt_add_executable(VanishingPoint MANUAL_FINALIZATION src/main.cpp res.qrc)
target_link_libraries(VanishingPoint PRIVATE vp_engine)
set_target_properties(VanishingPoint PROPERTIES WIN32_EXECUTABLE TRUE)
qt_import_qml_plugins(VanishingPoint)
qt_finalize_executable(VanishingPoint)

include(GNUInstallDirs)
install(TARGETS VanishingPoint RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
