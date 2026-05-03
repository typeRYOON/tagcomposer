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
)

if (WIN32)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${OpenCV_DIR}/x64/vc16/bin/opencv_world4120$<$<CONFIG:Debug>:d>.dll"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
    )
    set(APP_ICON_RESOURCE_WINDOWS "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/icon.rc")
    target_sources(${PROJECT_NAME} PRIVATE ${APP_ICON_RESOURCE_WINDOWS})
    set_target_properties(${PROJECT_NAME}
        PROPERTIES
            WIN32_EXECUTABLE TRUE
    )
elseif (APPLE)
    # macOS apps are .app bundles — Qt deployment, the dock icon, and the
    # Info.plist lookups all expect this. Drop a real .icns into resources/
    # and add MACOSX_BUNDLE_ICON_FILE here when you have artwork.
    set_target_properties(${PROJECT_NAME}
        PROPERTIES
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_GUI_IDENTIFIER  com.ryoon.tagcomposer
            MACOSX_BUNDLE_BUNDLE_NAME     ${PROJECT_NAME}
            MACOSX_BUNDLE_BUNDLE_VERSION  "0.1.0"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1"
    )
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
)
