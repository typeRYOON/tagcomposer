cmake_minimum_required(VERSION 3.16)
project(tagcomposer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTORCC ON)

find_package(QT NAMES Qt6 REQUIRED COMPONENTS Core Widgets Network Concurrent WebSockets)

if (WIN32)
    set(OpenCV_DIR "${CMAKE_SOURCE_DIR}/third_party/opencv/build")
endif()
find_package(OpenCV REQUIRED)

# ONNX Runtime path-by-platform. The Windows tree ships a static-import .lib;
# Unix-likes link the shared object directly. The bundled third_party/ dir
# under this repo is Windows-only — Linux / macOS users either extract a
# platform-specific prebuilt into the same path or symlink a system install
# (Homebrew on macOS, manual download on Linux). See the README's Building
# section for setup.
set(ONNXRUNTIME_DIR "${CMAKE_SOURCE_DIR}/third_party/onnxruntime")
if (WIN32)
    set(ONNXRUNTIME_LIB "${ONNXRUNTIME_DIR}/lib/onnxruntime.lib")
elseif (APPLE)
    set(ONNXRUNTIME_LIB "${ONNXRUNTIME_DIR}/lib/libonnxruntime.dylib")
else()
    set(ONNXRUNTIME_LIB "${ONNXRUNTIME_DIR}/lib/libonnxruntime.so")
endif()


file(GLOB_RECURSE PROJECT_SOURCES CONFIGURE_DEPENDS
    src/*.cpp
    include/*.h
)

find_package(Qt${QT_VERSION_MAJOR}
    COMPONENTS
        Core
        Gui
        Widgets
        Concurrent
        Network
        WebSockets
)
qt_standard_project_setup()

qt_add_executable(${PROJECT_NAME}
    ${PROJECT_SOURCES}
    resources/resources.qrc
)

# Includes
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${ONNXRUNTIME_DIR}/include
)

if (WIN32)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${OpenCV_DIR}/x64/vc16/bin/opencv_world4120$<$<CONFIG:Debug>:d>.dll"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
    )
    # ONNX Runtime ships its own runtime + provider DLLs; both must sit next
    # to the executable (Windows DLL search order) or sessions fail to load.
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ONNXRUNTIME_DIR}/lib/onnxruntime.dll"
            #"${ONNXRUNTIME_DIR}/lib/onnxruntime_providers_shared.dll"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
    )
    set(APP_ICON_RESOURCE_WINDOWS "${CMAKE_CURRENT_SOURCE_DIR}/resources/tagcomposer.rc")
    target_sources(${PROJECT_NAME} PRIVATE ${APP_ICON_RESOURCE_WINDOWS})
    set_target_properties(${PROJECT_NAME}
        PROPERTIES
            WIN32_EXECUTABLE TRUE
    )
elseif (APPLE)
    # macOS apps are .app bundles — Qt deployment, the dock icon, and the
    # Info.plist lookups all expect this. Drop a real .icns into resources/
    # and add MACOSX_BUNDLE_ICON_FILE here when you have artwork.
    # rpath @executable_path/../Frameworks lets the bundled dylib resolve
    # without DYLD_LIBRARY_PATH; macdeployqt fixes install names downstream.
    set_target_properties(${PROJECT_NAME}
        PROPERTIES
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_GUI_IDENTIFIER  com.ryoon.tagcomposer
            MACOSX_BUNDLE_BUNDLE_NAME     ${PROJECT_NAME}
            MACOSX_BUNDLE_BUNDLE_VERSION  "0.1.0"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1"
            BUILD_WITH_INSTALL_RPATH      TRUE
            INSTALL_RPATH                 "@executable_path/../Frameworks"
    )
    # Drop the ONNX Runtime dylib (plus any sibling files the prebuilt ships)
    # into Contents/Frameworks/ so the bundle is self-contained.
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/../Frameworks"
    )
    file(GLOB _ort_runtime_libs "${ONNXRUNTIME_DIR}/lib/libonnxruntime*")
    foreach(_lib ${_ort_runtime_libs})
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_lib}"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/../Frameworks/"
        )
    endforeach()
else()
    # Linux: rpath = $ORIGIN so the executable finds the bundled .so beside
    # itself without an LD_LIBRARY_PATH dance. Copy every libonnxruntime*
    # entry from the prebuilt's lib/ so the SONAME-versioned files land
    # next to the unversioned symlink and the runtime linker can resolve
    # whichever name the binary was actually linked against.
    set_target_properties(${PROJECT_NAME}
        PROPERTIES
            BUILD_WITH_INSTALL_RPATH TRUE
            INSTALL_RPATH            "$ORIGIN"
    )
    file(GLOB _ort_runtime_libs "${ONNXRUNTIME_DIR}/lib/libonnxruntime*")
    foreach(_lib ${_ort_runtime_libs})
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_lib}"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
        )
    endforeach()
endif()


install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION ${CMAKE_BINARY_DIR}
)

target_link_libraries(${PROJECT_NAME}
    PUBLIC
        Qt::Core
        Qt::Gui
        Qt::Widgets
        Qt::Concurrent
        Qt::Network
        Qt::WebSockets
        ${OpenCV_LIBS}
        ${ONNXRUNTIME_LIB}
)
