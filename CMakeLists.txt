cmake_minimum_required(VERSION 3.16)
project(tagcomposer LANGUAGES CXX)

include(qt.cmake)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(QT NAMES Qt6 REQUIRED COMPONENTS Core Widgets Network Concurrent WebSockets)

set(OpenCV_DIR "${CMAKE_SOURCE_DIR}/third_party/opencv/build")
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
    set(APP_ICON_RESOURCE_WINDOWS "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/icon.rc")
    target_sources(${PROJECT_NAME} PRIVATE ${APP_ICON_RESOURCE_WINDOWS})
    set_target_properties(${PROJECT_NAME}
        PROPERTIES
            WIN32_EXECUTABLE TRUE
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

