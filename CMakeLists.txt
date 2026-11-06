cmake_minimum_required(VERSION 3.21)
project(Data2Img VERSION 2.0.0 LANGUAGES C CXX)

if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "Data2Img currently requires Windows x64 and MSVC")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Select an x64 toolchain")
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

# 允许显式配置已有 OpenCV，避免绑定开发机器的安装路径。
find_package(OpenCV 4 REQUIRED COMPONENTS core imgproc imgcodecs)
include(CTest)
add_subdirectory(core)
add_subdirectory(cli)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

# 默认安装到工程根目录的 d2i_install，仍允许调用方显式指定安装前缀。
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/d2i_install"
        CACHE PATH "Data2Img install directory" FORCE)
endif()

# 运行目录采用平铺结构，自动收集核心库及其传递依赖的动态库。
install(TARGETS screen_codec RUNTIME DESTINATION ".")
install(FILES $<TARGET_RUNTIME_DLLS:screen_codec> DESTINATION ".")

# 将编译器提供的可分发运行库放在程序旁，无需修改系统 PATH。
set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION ".")
include(InstallRequiredSystemLibraries)

# 使用说明和上游许可随 CLI 一同安装，不分发内部静态库或测试程序。
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/README.md" DESTINATION ".")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/subModule/libcorrect/LICENSE"
    DESTINATION "licenses" RENAME "libcorrect-LICENSE.txt")
if(D2I_OPENCV_LICENSE)
    install(FILES "${D2I_OPENCV_LICENSE}"
        DESTINATION "licenses" RENAME "OpenCV-LICENSE.txt")
endif()

# 旧版 OpenCV 的导入目标不一定参与运行库推导，显式补齐实际使用的 DLL。
set(D2I_INSTALL_CV ${OpenCV_LIBS})
if(TARGET opencv_world)
    set(D2I_INSTALL_CV opencv_world)
endif()
foreach(cvTarget IN LISTS D2I_INSTALL_CV)
    if(TARGET ${cvTarget})
        get_target_property(cvType ${cvTarget} TYPE)
        if(cvType STREQUAL "SHARED_LIBRARY")
            install(FILES "$<TARGET_FILE:${cvTarget}>" DESTINATION ".")
        endif()
    endif()
endforeach()

# 可选择关闭 GUI，保留原有 CLI 与核心库的独立构建能力。
option(D2I_BUILD_GUI "Build the Qt6 Widgets GUI" ON)
if(D2I_BUILD_GUI)
    add_subdirectory(gui)
endif()
