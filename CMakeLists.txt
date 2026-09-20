cmake_minimum_required(VERSION 3.16)
project(ScreenshotTool VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(ScreenshotTool WIN32
    src/main.cpp
    src/util.h
    src/version.h
    src/darkui.h
    src/scrollui.h
    src/settings.h
    src/settings.cpp
    src/annotation.h
    src/annotation.cpp
    src/document.h
    src/document.cpp
    src/overlay.h
    src/overlay.cpp
    src/canvas.h
    src/canvas.cpp
    src/compareview.h
    src/compareview.cpp
    src/colorpicker.h
    src/colorpicker.cpp
    src/textdlg.h
    src/textdlg.cpp
    src/settingsdlg.h
    src/settingsdlg.cpp
    src/netutil.h
    src/ziputil.h
    src/extract.h
    src/extract.cpp
    src/longstitch.h
    src/longstitch.cpp
    src/longcapture.h
    src/longcapture.cpp
    src/app.h
    src/app.cpp
    src/app.rc
)

if(MSVC)
    target_compile_options(ScreenshotTool PRIVATE /utf-8 /W3 /O2 /EHsc /MP)
    target_compile_definitions(ScreenshotTool PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    set_target_properties(ScreenshotTool PROPERTIES
        LINK_FLAGS "/MANIFEST:EMBED /MANIFESTINPUT:\"${CMAKE_SOURCE_DIR}/app.manifest\" /SUBSYSTEM:WINDOWS"
        OUTPUT_NAME "SnipMagic"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/dist"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/dist"
    )
    # dist 下固定保留两份 exe：英文本名 + 中文副本（内容相同）
    add_custom_command(TARGET ScreenshotTool POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:ScreenshotTool>"
                "${CMAKE_SOURCE_DIR}/dist/截图大师SnipMagic.exe"
        COMMENT "Copy 截图大师SnipMagic.exe to dist"
    )
endif()

target_link_libraries(ScreenshotTool PRIVATE
    user32 gdi32 gdiplus comctl32 comdlg32 shell32 ole32 oleaut32 shlwapi uxtheme msimg32 imm32 winhttp crypt32 uuid
)
