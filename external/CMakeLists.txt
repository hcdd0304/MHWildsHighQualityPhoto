set(imgui_SOURCES
		"imgui/imgui.cpp"
		"imgui/imgui_demo.cpp"
		"imgui/imgui_draw.cpp"
		"imgui/imgui_tables.cpp"
		"imgui/imgui_widgets.cpp"
	)

add_library(imgui STATIC)
target_sources(imgui PRIVATE ${imgui_SOURCES})

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${imgui_SOURCES})

target_include_directories(imgui PUBLIC
    "imgui"
)

add_library(reframework INTERFACE)
target_include_directories(reframework INTERFACE "reframework_lib")

add_library(cimgui SHARED
	"cimgui/cimgui.cpp"
	"cimgui/cimgui.h"
)

target_include_directories(cimgui PUBLIC "cimgui")
target_link_libraries(cimgui PUBLIC imgui)

add_library(reshade INTERFACE)
target_include_directories(reshade INTERFACE "reshade")

add_library(libwebp INTERFACE)
target_include_directories(libwebp INTERFACE "webp/include")
target_link_libraries(libwebp INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/webp/libwebp.lib")

add_library(sk_hdr_png INTERFACE)
target_include_directories(sk_hdr_png INTERFACE "sk_hdr_png/include")
target_link_libraries(sk_hdr_png INTERFACE reshade)

add_library(subprocess_h INTERFACE)
target_include_directories(subprocess_h INTERFACE "subprocess.h")

add_library(stb INTERFACE)
target_include_directories(stb INTERFACE "stb")
target_include_directories(stb INTERFACE "stb_image")

add_subdirectory(glaze)
add_subdirectory(nativefiledialog-extended)

add_library(avir INTERFACE)
target_include_directories(avir INTERFACE "avir")

add_library(thread-pool INTERFACE)
target_include_directories(thread-pool INTERFACE "thread-pool/include")