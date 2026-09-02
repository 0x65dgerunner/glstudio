cmake_minimum_required(VERSION 3.16)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(glstudio VERSION 0.1.0 LANGUAGES CXX C)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTORCC ON)
    if(NOT DEFINED CMAKE_PREFIX_PATH)
        if(EXISTS "C:/Qt/6.8.2/msvc2022_64/lib/cmake/Qt6")
            list(APPEND CMAKE_PREFIX_PATH "C:/Qt/6.8.2/msvc2022_64")
        endif()
    endif()
    find_package(Qt6 REQUIRED COMPONENTS Widgets Svg OpenGL OpenGLWidgets)
else()
    enable_language(C)
endif()

include(FetchContent)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(MESHOPT_BUILD_DEMO OFF CACHE BOOL "" FORCE)
FetchContent_Declare(meshoptimizer GIT_REPOSITORY https://github.com/zeux/meshoptimizer.git GIT_TAG v0.22)
FetchContent_MakeAvailable(meshoptimizer)

add_library(glstudio_ui STATIC
    ui/src/theme.cpp
    ui/src/fonts.cpp
    ui/src/icons.cpp
    ui/src/internal.cpp
    ui/src/internal.hpp
    ui/src/window.cpp
    ui/src/button.cpp
    ui/src/input.cpp
    ui/src/select.cpp
    ui/src/forms.cpp
    ui/src/floating.cpp
    ui/src/display.cpp
    ui/resources/resources.qrc
    ui/include/edgeqt/theme.hpp
    ui/include/edgeqt/fonts.hpp
    ui/include/edgeqt/icons.hpp
    ui/include/edgeqt/window.hpp
    ui/include/edgeqt/button.hpp
    ui/include/edgeqt/input.hpp
    ui/include/edgeqt/select.hpp
    ui/include/edgeqt/checkbox.hpp
    ui/include/edgeqt/slider.hpp
    ui/include/edgeqt/textarea.hpp
    ui/include/edgeqt/dropdown.hpp
    ui/include/edgeqt/combobox.hpp
    ui/include/edgeqt/empty.hpp
    ui/include/edgeqt/alert.hpp
    ui/include/edgeqt/avatar.hpp
    ui/include/edgeqt/badge.hpp
    ui/include/edgeqt/breadcrumb.hpp
    ui/include/edgeqt/calendar.hpp
)

target_include_directories(glstudio_ui
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/ui/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/ui/src
)

target_link_libraries(glstudio_ui PUBLIC Qt6::Widgets Qt6::Svg)

if(WIN32)
    target_compile_definitions(glstudio_ui PUBLIC UNICODE _UNICODE NOMINMAX)
endif()

add_executable(glstudio WIN32
    main.cpp
    window.cpp
    window.hpp
    viewport.cpp
    viewport.hpp
    glb_loader.cpp
    glb_loader.hpp
    hdri_loader.cpp
    hdri_loader.hpp
    tinyexr_impl.cpp
    third_party/tinyexr/miniz.c
)

target_include_directories(glstudio PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/tinyexr
)
target_compile_definitions(glstudio PRIVATE MINIZ_NO_TIME)
if(MSVC)
    set_source_files_properties(third_party/tinyexr/miniz.c PROPERTIES COMPILE_FLAGS "/W0")
endif()

target_link_libraries(glstudio PRIVATE glstudio_ui Qt6::OpenGL Qt6::OpenGLWidgets meshoptimizer)

set_target_properties(glstudio PROPERTIES
    OUTPUT_NAME "glstudio"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/RelWithDebInfo"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/MinSizeRel"
)

set(_glstudio_copy_cmds
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:glstudio>/models"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:glstudio>/hdri"
)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/resources/models")
    list(APPEND _glstudio_copy_cmds
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/models"
            "$<TARGET_FILE_DIR:glstudio>/models"
    )
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/resources/HDRI")
    list(APPEND _glstudio_copy_cmds
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/HDRI"
            "$<TARGET_FILE_DIR:glstudio>/hdri"
    )
endif()
add_custom_command(TARGET glstudio POST_BUILD
    ${_glstudio_copy_cmds}
    COMMENT "Copying glstudio models and HDRIs next to the executable"
)

if(WIN32)
    get_target_property(_qmake_executable Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin_dir "${_qmake_executable}" DIRECTORY)
    find_program(GLSTUDIO_WINDEPLOYQT windeployqt HINTS "${_qt_bin_dir}")
    if(GLSTUDIO_WINDEPLOYQT)
        add_custom_command(TARGET glstudio POST_BUILD
            COMMAND "${GLSTUDIO_WINDEPLOYQT}" --no-compiler-runtime "$<TARGET_FILE:glstudio>"
            COMMENT "Deploying Qt runtime for glstudio"
        )
    endif()
endif()
