set(MHWildsHQQuestResultPhoto_SOURCES
        "InjectClient/FileInjectClient.cpp"
        "InjectClient/FileInjectClient.hpp"
        "InjectClient/ReShadeAddOnInjectClient.cpp"
        "InjectClient/ReShadeAddOnInjectClient.hpp"
        "InjectClient/GameProducedMaxQualityInjectClient.cpp"
        "InjectClient/GameProducedMaxQualityInjectClient.hpp"
        "CaptureResolutionInject.cpp"
        "CaptureResolutionInject.hpp"
        "CImGuiRouteFix.cpp"
        "GameUIController.cpp"
        "GameUIController.hpp"
        "ModSettings.cpp"
        "ModSettings.hpp"
        "REFrameworkBorrowedAPI.cpp"
        "REFrameworkBorrowedAPI.hpp"
        "WebPCaptureInjector.cpp"
        "WebPCaptureInjector.hpp"
        "Plugin_QuestResult.cpp"
        "Plugin_QuestResult.hpp"
        "PluginBase.hpp"
        "PluginBase.cpp"
	)

set(MHWildsCustomAlbumPhoto_SOURCES
    "InjectClient/FileInjectClient.cpp"
    "InjectClient/FileInjectClient.hpp"
    "CaptureResolutionInject.cpp"
    "CaptureResolutionInject.hpp"
    "CImGuiRouteFix.cpp"
    "ModSettings.cpp"
    "ModSettings.hpp"
    "REFrameworkBorrowedAPI.cpp"
    "REFrameworkBorrowedAPI.hpp"
    "WebPCaptureInjector.cpp"
    "WebPCaptureInjector.hpp"
    "Plugin_AlbumPhoto.hpp"
    "Plugin_AlbumPhoto.cpp"
    "PluginBase.hpp"
    "PluginBase.cpp")

add_link_options("/DELAYLOAD:cimgui.dll")

# Custom album photo

add_library(MHWildsCustomAlbumPhoto SHARED)
target_sources(MHWildsCustomAlbumPhoto PRIVATE ${MHWildsCustomAlbumPhoto_SOURCES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${MHWildsCustomAlbumPhoto_SOURCES})
target_compile_features(MHWildsCustomAlbumPhoto PUBLIC
    cxx_std_23
)

target_link_libraries(MHWildsCustomAlbumPhoto PUBLIC
    cimgui
    reframework
    libwebp
    glaze::glaze
    nfd
)

set_target_properties(MHWildsCustomAlbumPhoto PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_RELEASE
        "${CMAKE_BINARY_DIR}/bin/CustomAlbumPhoto/reframework/plugins"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO
        "${CMAKE_BINARY_DIR}/bin/CustomAlbumPhoto/reframework/plugins"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG
        "${CMAKE_BINARY_DIR}/bin/CustomAlbumPhoto/reframework/plugins"
    LIBRARY_OUTPUT_DIRECTORY_RELEASE
        "${CMAKE_BINARY_DIR}/bin/CustomAlbumPhoto/__lib__/"
    LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO
        "${CMAKE_BINARY_DIR}/bin/CustomAlbumPhoto/__lib__/"
    LIBRARY_OUTPUT_DIRECTORY_DEBUG
        "${CMAKE_BINARY_DIR}/bin/CustomAlbumPhoto/__lib__/"
    ARCHIVE_OUTPUT_DIRECTORY_RELEASE
        "${CMAKE_BINARY_DIR}/bin/CustomAlbumPhoto/__lib__/"
    ARCHIVE_OUTPUT_DIRECTORY_RELWITHDEBINFO
        "${CMAKE_BINARY_DIR}/bin/CustomAlbumPhoto/__lib__/"
    ARCHIVE_OUTPUT_DIRECTORY_DEBUG
        "${CMAKE_BINARY_DIR}/bin/CustomAlbumPhoto/__lib__/"
)

# Quest result

add_library(MHWildsHQQuestResultPhoto SHARED)

target_sources(MHWildsHQQuestResultPhoto PRIVATE ${MHWildsHQQuestResultPhoto_SOURCES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${MHWildsHQQuestResultPhoto_SOURCES})

target_compile_features(MHWildsHQQuestResultPhoto PUBLIC
    cxx_std_23
)

target_link_libraries(MHWildsHQQuestResultPhoto PUBLIC
    cimgui
    reframework
    libwebp
    glaze::glaze
    nfd
    avir
    thread-pool
    stb
)

set_target_properties(MHWildsHQQuestResultPhoto PROPERTIES
    OUTPUT_NAME "MHWildsHighQualityPhoto"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE
        "${CMAKE_BINARY_DIR}/bin/HQQuestResult/reframework/plugins"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO
        "${CMAKE_BINARY_DIR}/bin/HQQuestResult/reframework/plugins"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG
        "${CMAKE_BINARY_DIR}/bin/HQQuestResult/reframework/plugins"
    LIBRARY_OUTPUT_DIRECTORY_RELEASE
        "${CMAKE_BINARY_DIR}/bin/HQQuestResult/__lib__/"
    LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO
        "${CMAKE_BINARY_DIR}/bin/HQQuestResult/__lib__/"
    LIBRARY_OUTPUT_DIRECTORY_DEBUG
        "${CMAKE_BINARY_DIR}/bin/HQQuestResult/__lib__/"
    ARCHIVE_OUTPUT_DIRECTORY_RELEASE
        "${CMAKE_BINARY_DIR}/bin/HQQuestResult/__lib__/"
    ARCHIVE_OUTPUT_DIRECTORY_RELWITHDEBINFO
        "${CMAKE_BINARY_DIR}/bin/HQQuestResult/__lib__/"
    ARCHIVE_OUTPUT_DIRECTORY_DEBUG
        "${CMAKE_BINARY_DIR}/bin/HQQuestResult/__lib__/"
)