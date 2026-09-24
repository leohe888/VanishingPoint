cmake_minimum_required(VERSION 3.16)

project(VanishingPoint VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Quick)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Quick)

include(CTest)
if(BUILD_TESTING)
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test)
endif()

set(PROJECT_SOURCES
        src/main.cpp
        src/canvas/vpcanvas.cpp
        src/canvas/vpcanvas.h
        src/canvas/vpcontroller.cpp
        src/canvas/vpcontroller.h
        src/core/vpdocument.cpp
        src/core/vpdocument.h
        src/core/brushtool.cpp
        src/core/brushtool.h
        src/core/clonetool.cpp
        src/core/clonetool.h
        src/core/floatingimage.cpp
        src/core/floatingimage.h
        src/core/floatingimageprojection.cpp
        src/core/floatingimageprojection.h
        src/core/floatingimagetransformtool.cpp
        src/core/floatingimagetransformtool.h
        src/core/planeedittool.cpp
        src/core/planeedittool.h
        src/core/planecreatetool.cpp
        src/core/planecreatetool.h
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

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(VanishingPoint
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
# Define target properties for Android with Qt 6 as:
#    set_property(TARGET VanishingPoint APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
#                 ${CMAKE_CURRENT_SOURCE_DIR}/android)
# For more information, see https://doc.qt.io/qt-6/qt-add-executable.html#target-creation
else()
    if(ANDROID)
        add_library(VanishingPoint SHARED
            ${PROJECT_SOURCES}
        )
# Define properties for Android with Qt 5 after find_package() calls as:
#    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
    else()
        add_executable(VanishingPoint
          ${PROJECT_SOURCES}
        )
    endif()
endif()

target_include_directories(VanishingPoint PRIVATE src)

target_link_libraries(VanishingPoint
  PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Quick)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
if(${QT_VERSION} VERSION_LESS 6.1.0)
  set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.VanishingPoint)
endif()
set_target_properties(VanishingPoint PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS VanishingPoint
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})

if(QT_VERSION_MAJOR EQUAL 6)
    qt_import_qml_plugins(VanishingPoint)
    qt_finalize_executable(VanishingPoint)
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
