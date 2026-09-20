cmake_minimum_required(VERSION 3.16)
project(TestMoveFill LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(test_movefill test_movefill.cpp
    ../src/document.cpp ../src/annotation.cpp ../src/settings.cpp)

target_include_directories(test_movefill PRIVATE ../src)
if(MSVC)
    target_compile_options(test_movefill PRIVATE /utf-8 /W3 /O2 /EHsc)
    target_compile_definitions(test_movefill PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
target_link_libraries(test_movefill PRIVATE user32 gdi32 gdiplus comctl32 comdlg32 shell32 ole32 shlwapi winmm)
