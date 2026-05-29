cmake_minimum_required(VERSION 3.24)

project(DOLBUTO LANGUAGES C CXX)

if(WIN32)
    enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "" FORCE)
endif()

find_package(Vulkan REQUIRED)

set(DOLBUTO_PREBUILT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/prebuilt")

function(dolbuto_require_file path description)
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "${description} not found: ${path}")
    endif()
endfunction()

set(FASTNOISE2_TOOLS OFF CACHE BOOL "" FORCE)
set(FASTNOISE2_TESTS OFF CACHE BOOL "" FORCE)
set(FASTNOISE2_UTILITY OFF CACHE BOOL "" FORCE)
set(FASTSIMD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FASTSIMD_TESTS OFF CACHE BOOL "" FORCE)
get_filename_component(DOLBUTO_FASTSIMD_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/FastSIMD" REALPATH)
set(FastSIMD_SOURCE_DIR "${DOLBUTO_FASTSIMD_SOURCE_DIR}" CACHE PATH "" FORCE)
add_subdirectory(external/FastNoise2)

set(FREETYPE_ROOT "${DOLBUTO_PREBUILT_DIR}/freetype")
set(FREETYPE_INCLUDE_DIR "${FREETYPE_ROOT}/include/freetype2")
set(FREETYPE_IMPORT_LIB "${FREETYPE_ROOT}/lib/freetype.lib")
set(FREETYPE_DLL "${FREETYPE_ROOT}/bin/freetype.dll")
dolbuto_require_file("${FREETYPE_IMPORT_LIB}" "FreeType import library")
dolbuto_require_file("${FREETYPE_DLL}" "FreeType DLL")
add_library(Freetype::Freetype SHARED IMPORTED GLOBAL)
set_target_properties(Freetype::Freetype PROPERTIES
    IMPORTED_IMPLIB "${FREETYPE_IMPORT_LIB}"
    IMPORTED_LOCATION "${FREETYPE_DLL}"
    INTERFACE_INCLUDE_DIRECTORIES "${FREETYPE_INCLUDE_DIR}"
)

set(RMLUI_ROOT "${DOLBUTO_PREBUILT_DIR}/rmlui")
set(RMLUI_INCLUDE_DIR "${RMLUI_ROOT}/include")
set(RMLUI_IMPORT_LIB "${RMLUI_ROOT}/lib/rmlui.lib")
set(RMLUI_DLL "${RMLUI_ROOT}/bin/rmlui.dll")
set(RMLUI_DEBUG_IMPORT_LIB "${RMLUI_ROOT}/debug/lib/rmlui.lib")
set(RMLUI_DEBUG_DLL "${RMLUI_ROOT}/debug/bin/rmlui.dll")
dolbuto_require_file("${RMLUI_IMPORT_LIB}" "RmlUi import library")
dolbuto_require_file("${RMLUI_DLL}" "RmlUi DLL")
add_library(RmlUi::Core SHARED IMPORTED GLOBAL)
set_target_properties(RmlUi::Core PROPERTIES
    IMPORTED_IMPLIB "${RMLUI_IMPORT_LIB}"
    IMPORTED_LOCATION "${RMLUI_DLL}"
    INTERFACE_INCLUDE_DIRECTORIES "${RMLUI_INCLUDE_DIR}"
)
if(EXISTS "${RMLUI_DEBUG_IMPORT_LIB}" AND EXISTS "${RMLUI_DEBUG_DLL}")
    set_property(TARGET RmlUi::Core APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
    set_target_properties(RmlUi::Core PROPERTIES
        IMPORTED_IMPLIB_DEBUG "${RMLUI_DEBUG_IMPORT_LIB}"
        IMPORTED_LOCATION_DEBUG "${RMLUI_DEBUG_DLL}"
    )
else()
    message(WARNING "RmlUi Debug prebuilt not found. Debug builds may not run correctly with the Release RmlUi DLL: ${RMLUI_DEBUG_DLL}")
endif()

set(OPENAL_ROOT "${DOLBUTO_PREBUILT_DIR}/openal-soft")
set(OPENAL_INCLUDE_DIR "${OPENAL_ROOT}/include")
set(OPENAL_IMPORT_LIB "${OPENAL_ROOT}/lib/OpenAL32.lib")
set(OPENAL_DLL "${OPENAL_ROOT}/bin/OpenAL32.dll")
dolbuto_require_file("${OPENAL_IMPORT_LIB}" "OpenAL Soft import library")
dolbuto_require_file("${OPENAL_DLL}" "OpenAL Soft DLL")
add_library(OpenAL::OpenAL SHARED IMPORTED GLOBAL)
set_target_properties(OpenAL::OpenAL PROPERTIES
    IMPORTED_IMPLIB "${OPENAL_IMPORT_LIB}"
    IMPORTED_LOCATION "${OPENAL_DLL}"
    INTERFACE_INCLUDE_DIRECTORIES "${OPENAL_INCLUDE_DIR}"
)

if(NOT Vulkan_GLSLC_EXECUTABLE)
    find_program(Vulkan_GLSLC_EXECUTABLE glslc REQUIRED)
endif()

set(GLFW_ROOT "${DOLBUTO_PREBUILT_DIR}/glfw")
set(GLFW_INCLUDE_DIR "${GLFW_ROOT}/include")

set(GLFW_IMPORT_LIB "${GLFW_ROOT}/lib/glfw3dll.lib")
set(GLFW_DLL "${GLFW_ROOT}/bin/glfw3.dll")
set(SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")

set(DOLBUTO_WINDOWS_RESOURCE)
if(WIN32)
    set(DOLBUTO_ICON_FILE "${CMAKE_CURRENT_SOURCE_DIR}/assets/textures/icon/icon.ico")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/windows/DOLBUTO.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/DOLBUTO.rc"
        @ONLY
    )
    set(DOLBUTO_WINDOWS_RESOURCE "${CMAKE_CURRENT_BINARY_DIR}/DOLBUTO.rc")
endif()

if(NOT EXISTS "${GLFW_IMPORT_LIB}")
    message(FATAL_ERROR "GLFW import library not found: ${GLFW_IMPORT_LIB}")
endif()

if(NOT EXISTS "${GLFW_DLL}")
    message(FATAL_ERROR "GLFW DLL not found: ${GLFW_DLL}")
endif()

set(DOLBUTO_RUNTIME_DLLS
    "${GLFW_DLL}"
    "${FREETYPE_DLL}"
    "$<TARGET_FILE:RmlUi::Core>"
    "${OPENAL_DLL}"
)

set(SHADER_SOURCES
    shaders/sky.vert
    shaders/sky.frag
    shaders/cloud.vert
    shaders/cloud.frag
    shaders/sprite.vert
    shaders/sprite.frag
    shaders/kawase_blur.frag
    shaders/rmlui.vert
    shaders/rmlui.frag
    shaders/terrain.vert
    shaders/terrain.frag
    shaders/fluid.frag
    shaders/player.vert
    shaders/player_model.vert
    shaders/item.vert
    shaders/item_viewmodel.vert
    shaders/selection.vert
    shaders/selection.frag
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
    ${DOLBUTO_WINDOWS_RESOURCE}
    src/main.cpp
    src/game/ClientContent.cpp
    src/game/ClientContent.h
    src/game/ClientFrame.h
    src/game/ClientRenderRuntime.cpp
    src/game/ClientRenderRuntime.h
    src/game/ClientRuntime.cpp
    src/game/ClientRuntime.h
    src/game/ClientRuntimeState.h
    src/game/ClientSceneLifecycle.cpp
    src/game/ClientSceneLifecycle.h
    src/game/ClientTerrainCompletionHandler.cpp
    src/game/ClientTerrainCompletionHandler.h
    src/game/ClientTerrainCoordinator.cpp
    src/game/ClientTerrainCoordinator.h
    src/game/ClientTerrainJobProcessor.cpp
    src/game/ClientTerrainJobProcessor.h
    src/game/ClientTerrainSceneRuntime.cpp
    src/game/ClientTerrainSceneRuntime.h
    src/game/ClientUiTypes.h
    src/game/ClientWorldRuntime.cpp
    src/game/ClientWorldRuntime.h
    src/game/CommandSystem.cpp
    src/game/CommandSystem.h
    src/game/GameClient.cpp
    src/game/GameClient.h
    src/game/GameMode.h
    src/game/PlayerMovementSystem.cpp
    src/game/PlayerMovementSystem.h
    src/game/PlayerStats.h
    src/game/RadialMenuRenderFrame.h
    src/gameplay/BlockInteractionSystem.cpp
    src/gameplay/BlockInteractionSystem.h
    src/gameplay/ClientGameplayRuntime.cpp
    src/gameplay/ClientGameplayRuntime.h
    src/gameplay/PlayerInventory.cpp
    src/gameplay/PlayerInventory.h
    src/assets/BlockItemIconBuilder.cpp
    src/assets/BlockItemIconBuilder.h
    src/assets/PropModelLoader.cpp
    src/assets/PropModelLoader.h
    src/audio/AudioSystem.cpp
    src/audio/AudioSystem.h
    src/camera/Camera.cpp
    src/camera/Camera.h
    src/camera/CameraViewBob.cpp
    src/camera/CameraViewBob.h
    src/config/ConfigLoaders.cpp
    src/config/ConfigLoaders.h
    src/config/ViewmodelConfig.h
    src/data/DataLoaders.cpp
    src/data/DataLoaders.h
    src/items/ItemData.h
    src/platform/Log.cpp
    src/platform/Log.h
    src/platform/RuntimePaths.cpp
    src/platform/RuntimePaths.h
    src/renderer/RendererDroppedItems.cpp
    src/renderer/RendererFrameLoop.cpp
    src/renderer/RendererGameplayBridge.cpp
    src/renderer/RendererLifecycle.cpp
    src/renderer/RendererLocalResources.cpp
    src/renderer/RendererPipelines.cpp
    src/renderer/RendererRmlUiBackend.cpp
    src/renderer/RendererAudio.cpp
    src/renderer/RendererClimateOverlay.cpp
    src/renderer/RendererConfig.cpp
    src/renderer/RendererDiagnostics.cpp
    src/renderer/RendererSceneLifecycleBridge.cpp
    src/renderer/RendererSceneDraw.cpp
    src/renderer/RendererSwapchain.cpp
    src/renderer/RendererTerrainRuntimeBridge.cpp
    src/renderer/RendererVulkanContext.cpp
    src/renderer/CloudRenderPath.cpp
    src/renderer/CloudRenderPath.h
    src/renderer/DroppedItemRenderCollector.cpp
    src/renderer/DroppedItemRenderCollector.h
    src/renderer/DroppedItemRenderPath.cpp
    src/renderer/DroppedItemRenderPath.h
    src/renderer/ItemSpriteMeshBuilder.cpp
    src/renderer/ItemSpriteMeshBuilder.h
    src/renderer/RendererFrame.h
    src/renderer/RendererAssetStore.cpp
    src/renderer/RendererAssetStore.h
    src/renderer/RendererAudioBridge.h
    src/renderer/RendererConfigBridge.h
    src/renderer/RendererDiagnosticsBridge.h
    src/renderer/RendererGameplayBridge.h
    src/renderer/RendererGpuResources.cpp
    src/renderer/RendererGpuResources.h
    src/renderer/Renderer.h
    src/renderer/RadialMenuRenderPath.cpp
    src/renderer/RadialMenuRenderPath.h
    src/renderer/RendererRenderMethods.inc
    src/renderer/RendererRmlUiBackend.h
    src/renderer/RendererSceneLifecycleBridge.h
    src/renderer/RendererTerrainRuntimeBridge.h
    src/renderer/RendererTypes.h
    src/renderer/RendererUiRuntimeBridge.h
    src/renderer/RendererVulkanMethods.inc
    src/renderer/RendererVulkanState.h
    src/renderer/RendererTerrainMeshBridge.cpp
    src/renderer/RendererTerrainMeshBridge.h
    src/renderer/ClimateOverlayTextureBuilder.cpp
    src/renderer/ClimateOverlayTextureBuilder.h
    src/renderer/DebugOverlayText.cpp
    src/renderer/DebugOverlayText.h
    src/renderer/ScreenPresentation.cpp
    src/renderer/ScreenPresentation.h
    src/renderer/SkyRenderPath.cpp
    src/renderer/SkyRenderPath.h
    src/renderer/SpriteRenderPath.cpp
    src/renderer/SpriteRenderPath.h
    src/renderer/TerrainGeometryBuilder.cpp
    src/renderer/TerrainGeometryBuilder.h
    src/renderer/TextRenderPath.cpp
    src/renderer/TextRenderPath.h
    src/renderer/ParticleRenderPath.cpp
    src/renderer/ParticleRenderPath.h
    src/renderer/PlayerModelLoader.cpp
    src/renderer/PlayerModelLoader.h
    src/renderer/PlayerMeshRenderPath.cpp
    src/renderer/PlayerMeshRenderPath.h
    src/renderer/TerrainRenderPath.cpp
    src/renderer/TerrainRenderPath.h
    src/renderer/TerrainTypes.h
    src/renderer/RendererUi.cpp
    src/save/SaveFormat.cpp
    src/save/SaveFormat.h
    src/save/SaveSystem.cpp
    src/save/SaveSystem.h
    src/third_party/stb_image.cpp
    src/third_party/stb_image_write.cpp
    src/third_party/stb_vorbis.cpp
    src/ui/ClientUiBridge.cpp
    src/ui/ClientUiBridge.h
    src/ui/InventoryUi.cpp
    src/ui/InventoryUi.h
    src/ui/RmlInput.cpp
    src/ui/RmlInput.h
    src/ui/UiSystem.cpp
    src/ui/UiSystem.h
    src/world/BlockData.h
    src/world/BlockVisualShape.h
    src/world/Biome.cpp
    src/world/Biome.h
    src/world/ChunkLoadSystem.cpp
    src/world/ChunkLoadSystem.h
    src/world/ChunkPrepareSystem.cpp
    src/world/ChunkPrepareSystem.h
    src/world/ClimateSystem.cpp
    src/world/ClimateSystem.h
    src/world/DroppedItemRuntime.cpp
    src/world/DroppedItemRuntime.h
    src/world/DroppedItemSystem.cpp
    src/world/DroppedItemSystem.h
    src/world/SkyLightSystem.cpp
    src/world/SkyLightSystem.h
    src/world/TerrainBuilder.cpp
    src/world/TerrainBuilder.h
    src/world/TerrainJobSystem.cpp
    src/world/TerrainJobSystem.h
    src/world/TerrainMesher.cpp
    src/world/TerrainMesher.h
    src/world/WorldRuntime.cpp
    src/world/WorldRuntime.h
    src/world/WorldTypes.h
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
        DOLBUTO_CONFIG_DIR="${CMAKE_CURRENT_SOURCE_DIR}/config"
        DOLBUTO_SAVE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/saves"
        DOLBUTO_SHADER_DIR="${SHADER_OUTPUT_DIR}"
        DOLBUTO_LOG_DIR="${CMAKE_CURRENT_SOURCE_DIR}/logs"
)

target_compile_options(DOLBUTO
    PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
)

target_link_libraries(DOLBUTO
    PRIVATE
        Vulkan::Vulkan
        FastNoise
        Freetype::Freetype
        RmlUi::Core
        OpenAL::OpenAL
        "${GLFW_IMPORT_LIB}"
)

if(WIN32)
    target_link_libraries(DOLBUTO PRIVATE advapi32)
endif()

if(WIN32 AND MSVC)
    target_link_options(DOLBUTO
        PRIVATE
            "$<$<CONFIG:Release>:/SUBSYSTEM:WINDOWS>"
            "$<$<CONFIG:Release>:/ENTRY:mainCRTStartup>"
    )
endif()

add_custom_command(TARGET DOLBUTO POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${DOLBUTO_RUNTIME_DLLS} "$<TARGET_FILE_DIR:DOLBUTO>"
)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_custom_command(TARGET DOLBUTO POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:DOLBUTO>/assets"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/config" "$<TARGET_FILE_DIR:DOLBUTO>/config"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${SHADER_OUTPUT_DIR}" "$<TARGET_FILE_DIR:DOLBUTO>/shaders"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:DOLBUTO>/saves"
    )
endif()
