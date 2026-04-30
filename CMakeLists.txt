cmake_minimum_required(VERSION 3.24)

project(DOLBUTO LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Vulkan REQUIRED)

if(NOT Vulkan_GLSLC_EXECUTABLE)
    find_program(Vulkan_GLSLC_EXECUTABLE glslc REQUIRED)
endif()

set(GLFW_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/external/glfw/glfw-3.4.bin.WIN64")
set(GLFW_INCLUDE_DIR "${GLFW_ROOT}/include")

if(MSVC_VERSION GREATER_EQUAL 1930)
    set(GLFW_LIB_DIR "${GLFW_ROOT}/lib-vc2022")
elseif(MSVC_VERSION GREATER_EQUAL 1920)
    set(GLFW_LIB_DIR "${GLFW_ROOT}/lib-vc2019")
elseif(MSVC_VERSION GREATER_EQUAL 1910)
    set(GLFW_LIB_DIR "${GLFW_ROOT}/lib-vc2017")
elseif(MSVC_VERSION GREATER_EQUAL 1900)
    set(GLFW_LIB_DIR "${GLFW_ROOT}/lib-vc2015")
else()
    message(FATAL_ERROR "Unsupported MSVC version for bundled GLFW binaries.")
endif()

set(GLFW_IMPORT_LIB "${GLFW_LIB_DIR}/glfw3dll.lib")
set(GLFW_DLL "${GLFW_LIB_DIR}/glfw3.dll")
set(SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")

if(NOT EXISTS "${GLFW_IMPORT_LIB}")
    message(FATAL_ERROR "GLFW import library not found: ${GLFW_IMPORT_LIB}")
endif()

if(NOT EXISTS "${GLFW_DLL}")
    message(FATAL_ERROR "GLFW DLL not found: ${GLFW_DLL}")
endif()

set(SHADER_SOURCES
    shaders/sprite.vert
    shaders/sprite.frag
    shaders/terrain.vert
    shaders/terrain.frag
)

set(SHADER_OUTPUTS)
foreach(SHADER_SOURCE ${SHADER_SOURCES})
    get_filename_component(SHADER_NAME "${SHADER_SOURCE}" NAME)
    set(SHADER_OUTPUT "${SHADER_OUTPUT_DIR}/${SHADER_NAME}.spv")
    add_custom_command(
        OUTPUT "${SHADER_OUTPUT}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_OUTPUT_DIR}"
        COMMAND "${Vulkan_GLSLC_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/${SHADER_SOURCE}" -o "${SHADER_OUTPUT}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${SHADER_SOURCE}"
        VERBATIM
    )
    list(APPEND SHADER_OUTPUTS "${SHADER_OUTPUT}")
endforeach()

add_custom_target(DOLBUTOShaders DEPENDS ${SHADER_OUTPUTS})

add_executable(DOLBUTO
    src/main.cpp
    src/app/Application.cpp
    src/app/Application.h
    src/camera/Camera.cpp
    src/camera/Camera.h
    src/renderer/Renderer.cpp
    src/renderer/Renderer.h
    src/third_party/stb_image.cpp
    src/third_party/stb_truetype.cpp
)

add_dependencies(DOLBUTO DOLBUTOShaders)

target_include_directories(DOLBUTO
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${GLFW_INCLUDE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/external/stb"
)

target_compile_definitions(DOLBUTO
    PRIVATE
        DOLBUTO_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
        DOLBUTO_SHADER_DIR="${SHADER_OUTPUT_DIR}"
)

target_link_libraries(DOLBUTO
    PRIVATE
        Vulkan::Vulkan
        "${GLFW_IMPORT_LIB}"
)

if(WIN32)
    target_link_libraries(DOLBUTO PRIVATE advapi32)
endif()

add_custom_command(TARGET DOLBUTO POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${GLFW_DLL}"
        "$<TARGET_FILE_DIR:DOLBUTO>"
)
