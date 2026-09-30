cmake_minimum_required(VERSION 3.16)
project(tagcomposer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Concurrent WebSockets)
qt_standard_project_setup()

# ---- Bundled native dependencies, used only by the tagger target below.
#
# The tree under third_party/ is the Windows prebuilt. On another platform,
# drop a platform build at the same paths; nothing else here changes.
if (WIN32)
    set(OpenCV_DIR "${CMAKE_SOURCE_DIR}/third_party/opencv/build")
endif()
find_package(OpenCV REQUIRED)

set(ONNXRUNTIME_DIR "${CMAKE_SOURCE_DIR}/third_party/onnxruntime")
if (WIN32)
    set(ONNXRUNTIME_LIB "${ONNXRUNTIME_DIR}/lib/onnxruntime.lib")
elseif (APPLE)
    set(ONNXRUNTIME_LIB "${ONNXRUNTIME_DIR}/lib/libonnxruntime.dylib")
else()
    set(ONNXRUNTIME_LIB "${ONNXRUNTIME_DIR}/lib/libonnxruntime.so")
endif()

# qt_standard_project_setup() turns on AUTOMOC and AUTOUIC but not AUTORCC.
# Without this a .qrc listed in a target is silently ignored, every :/ path
# resolves to nothing, and the app runs unstyled with blank icons.
set(CMAKE_AUTORCC ON)

# Domain and storage. Qt6::Core and nothing else: with no Gui or Widgets on the
# include path, a #include <QWidget> under core/ fails to compile. That is the
# only reason this target exists as a separate one.
add_library(tc_core STATIC
    core/entry.h
    core/entry.cpp
    core/entry_io.h
    core/entry_io.cpp
    core/fct.h
    core/fct.cpp
    core/facet_schema.h
    core/facet_schema.cpp
    core/tag_facets.h
    core/tag_facets.cpp
    core/rule.h
    core/rule_io.h
    core/rule_io.cpp
    core/rule_eval.h
    core/rule_eval.cpp
    core/variables.h
    core/variables.cpp
    core/tag_groups.h
    core/tag_groups.cpp
    core/composer_doc.h
    core/composer_doc.cpp
    core/pipeline.h
    core/pipeline.cpp
    core/settings.h
    core/settings.cpp
    core/prompt.h
    core/prompt.cpp
    core/composer_store.h
    core/composer_store.cpp
    core/workflow.h
    core/workflow.cpp
    core/workflow_io.h
    core/workflow_io.cpp
    core/danbooru_index.h
    core/cluster_filter.h
    core/cluster_filter.cpp
    core/danbooru_index.cpp
    core/saved_state.h
    core/saved_state.cpp
    core/profiles.h
    core/profiles.cpp
    core/png_text.h
    core/png_text.cpp
    core/prompt_history.h
    core/prompt_history.cpp
    core/run.h
    core/run.cpp
    core/entry_store.h
    core/entry_store.cpp
    core/entry_search.h
    core/entry_search.cpp
    core/pipeline_types.h
    core/load_error.h
)
target_include_directories(tc_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tc_core PUBLIC Qt6::Core)

# Perceptual hashing and ONNX tag inference.
#
# Its own target rather than part of tc_core, for two reasons: it needs Gui
# (QImage) and Concurrent, which tc_core deliberately does not link so that a
# stray #include <QWidget> under core/ still fails to compile; and it drags in
# OpenCV and ONNX Runtime, which nothing else in the app touches.
add_library(tc_tagger STATIC
    tagger/phash.h
    tagger/phash.cpp
    tagger/phash_index.h
    tagger/phash_index.cpp
    tagger/download_watcher.h
    tagger/download_watcher.cpp
    tagger/tagger_model.h
    tagger/tagger_model.cpp
    tagger/tagger_library.h
    tagger/tagger_library.cpp
    tagger/batch_tagger.h
    tagger/batch_tagger.cpp
)
target_include_directories(tc_tagger PUBLIC ${CMAKE_SOURCE_DIR} ${ONNXRUNTIME_DIR}/include)
target_link_libraries(tc_tagger PUBLIC Qt6::Core Qt6::Gui Qt6::Concurrent
                                       ${OpenCV_LIBS} ${ONNXRUNTIME_LIB})

# The GUI. Widgets stop here: tc_core has no path to them.
qt_add_executable(tagcomposer
    app/main.cpp
    app/page.h
    app/paths.h
    app/widget_utils.h
    app/widget_utils.cpp
    app/logger.h
    app/logger.cpp
    app/app_scroll_bar.h
    app/app_scroll_bar.cpp
    app/flow_layout.h
    app/flow_layout.cpp
    app/chromed_dialog.h
    app/chromed_dialog.cpp
    app/comfy_client.h
    app/comfy_client.cpp
    app/app_data.h
    app/app_data.cpp
    app/app_window.h
    app/app_window.cpp
    app/nav_bar.h
    app/nav_bar.cpp
    app/status_bar.h
    app/status_bar.cpp
    app/home_page.h
    app/home_page.cpp
    app/shiny_logo.h
    app/shiny_logo.cpp
    app/entry_view.h
    app/entry_view.cpp
    app/entry_nav_panel.h
    app/entry_nav_panel.cpp
    app/icons.h
    app/image_dropper.h
    app/image_dropper.cpp
    app/entry_panel.h
    app/entry_panel.cpp
    app/lora_widgets.h
    app/lora_widgets.cpp
    app/entry_viewer_page.h
    app/entry_viewer_page.cpp
    app/composer_sidebar.h
    app/composer_sidebar.cpp
    app/composer_page.h
    app/composer_page.cpp
    app/composer_page_tags.cpp
    app/composer_page_sidebar.cpp
    app/composer_page_states.cpp
    app/tag_list_view.h
    app/tag_list_view.cpp
    app/settings_page.h
    app/settings_page.cpp
    app/workflow_input_cache.h
    app/workflow_input_cache.cpp
    app/workflow_edit_page.h
    app/workflow_edit_page.cpp
    app/clip_editor_dialog.h
    app/clip_editor_dialog.cpp
    app/dtext.h
    app/dtext.cpp
    app/tag_preview_fetcher.h
    app/tag_preview_fetcher.cpp
    app/facet_editor_page.h
    app/facet_editor_page.cpp
    app/tag_wiki_page.h
    app/tag_wiki_page.cpp
    app/prompt_history_page.h
    app/prompt_history_page.cpp
    app/output_viewer_page.h
    app/output_viewer_page.cpp
    app/tag_preview_popup.h
    app/tag_preview_popup.cpp
    app/dataset_helpers_page.h
    app/dataset_helpers_page.cpp
    app/tag_editor_page.h
    app/tag_editor_page.cpp
    app/batch_edit_page.h
    app/batch_edit_page.cpp
    app/tag_cluster_page.h
    app/tag_cluster_page.cpp
    app/tag_cluster_page_fetch.cpp
    app/collector_page.h
    app/collector_page.cpp
    app/auto_tag_page.h
    app/auto_tag_page.cpp
    app/composer_scroll_area.h
    app/composer_scroll_area.cpp
    app/category_nav_panel.h
    app/category_nav_panel.cpp
    app/composer_widgets.h
    app/composer_widgets.cpp
    app/movable_preview_label.h
    app/movable_preview_label.cpp
    app/preview_popout_window.h
    app/preview_popout_window.cpp
    app/tag_search_bar.h
    app/tag_search_bar.cpp
    app/states_grid_view.h
    app/states_grid_view.cpp
    app/frameless_chrome.h
    app/title_bar.h
    app/title_bar.cpp
    app/window_chrome.h
    app/window_chrome.cpp
    resources/resources.qrc
)
target_link_libraries(tagcomposer PRIVATE tc_core tc_tagger Qt6::Widgets Qt6::Network
                                          Qt6::Concurrent Qt6::WebSockets)

# Both DLLs have to sit beside the exe: Windows resolves them by search order,
# and ONNX Runtime fails to open a session without its provider DLL alongside.
if (WIN32)
    add_custom_command(TARGET tagcomposer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${OpenCV_DIR}/x64/vc16/bin/opencv_world4120$<$<CONFIG:Debug>:d>.dll"
                "${ONNXRUNTIME_DIR}/lib/onnxruntime.dll"
                "${ONNXRUNTIME_DIR}/lib/onnxruntime_providers_shared.dll"
                "$<TARGET_FILE_DIR:tagcomposer>"
        COMMENT "Copying OpenCV and ONNX Runtime"
    )
endif()

# opencv_world is ~64 MB and onnxruntime ~14 MB, and only the tagger, the
# collector and the clip editor ever reach them. Statically imported, every
# launch pays their load and static-init before main(); delay-loading moves
# that to the first actual call.
#
# Every import taken from both is a function, which is what makes this legal.
# A data import cannot be delay-loaded, so if one ever appears the link fails
# with LNK1194 and this is the thing to turn off.
if (MSVC)
    target_link_libraries(tagcomposer PRIVATE delayimp.lib)
    target_link_options(tagcomposer PRIVATE
        "/DELAYLOAD:opencv_world4120$<$<CONFIG:Debug>:d>.dll"
        "/DELAYLOAD:onnxruntime.dll"
    )
endif()

# Copy the Qt runtime next to the exe. Without this the GUI starts and dies
# with 0xC0000135 unless Qt's bin happens to be on PATH, which is not something
# to depend on.
if (WIN32)
    get_target_property(_qt_qmake Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin "${_qt_qmake}" DIRECTORY)
    find_program(WINDEPLOYQT windeployqt HINTS "${_qt_bin}")

    if (WINDEPLOYQT)
        add_custom_command(TARGET tagcomposer POST_BUILD
            COMMAND "${WINDEPLOYQT}"
                    --no-translations --no-system-d3d-compiler --no-opengl-sw
                    --no-compiler-runtime
                    "$<TARGET_FILE:tagcomposer>"
            COMMENT "Deploying Qt runtime"
        )
    else()
        message(WARNING "windeployqt not found; the GUI will need Qt's bin on PATH")
    endif()
endif()

# The check suite, console only, so the whole thing runs without a window.
add_executable(tc_checks
    checks/main.cpp
)
target_link_libraries(tc_checks PRIVATE tc_core)
