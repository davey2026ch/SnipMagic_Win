cmake_minimum_required(VERSION 3.16)
project(TestWechat LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 微信式低纹理内容的拼接回归 + 原有合成回归
add_executable(test_wechat longstitch_test_wechat.cpp ../src/longstitch.cpp)
add_executable(test_regress ../src/longstitch_test.cpp ../src/longstitch.cpp)

foreach(t test_wechat test_regress)
    target_include_directories(${t} PRIVATE ../src)
    if(MSVC)
        target_compile_options(${t} PRIVATE /utf-8 /W3 /O2 /EHsc)
        target_compile_definitions(${t} PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    endif()
    target_link_libraries(${t} PRIVATE user32 gdi32 gdiplus comctl32 comdlg32 shell32 ole32 shlwapi)
endforeach()

# 调试版：打开拼接算法内部追踪日志
target_compile_definitions(test_wechat PRIVATE LC_DEBUG)
target_compile_definitions(test_regress PRIVATE LC_DEBUG)
