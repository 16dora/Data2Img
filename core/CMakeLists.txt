# 只编译子模块的 RS 源码，隔离上游工具、卷积码和旧版构建选项。
set(RS_ROOT "${PROJECT_SOURCE_DIR}/subModule/libcorrect")
if(NOT EXISTS "${RS_ROOT}/include/correct.h")
    message(FATAL_ERROR "Missing subModule/libcorrect; initialize the recorded Git submodule")
endif()
add_library(d2i_rs STATIC
    "${RS_ROOT}/src/reed-solomon/polynomial.c"
    "${RS_ROOT}/src/reed-solomon/reed-solomon.c"
    "${RS_ROOT}/src/reed-solomon/encode.c"
    "${RS_ROOT}/src/reed-solomon/decode.c")
target_include_directories(d2i_rs SYSTEM PUBLIC "${RS_ROOT}/include")
target_compile_options(d2i_rs PRIVATE /W0)

add_library(d2i_impl STATIC
    source/support.cpp
    source/format.cpp
    source/image_codec.cpp
    source/file_codec.cpp)
target_include_directories(d2i_impl PUBLIC include PRIVATE source)
target_include_directories(d2i_impl SYSTEM PUBLIC ${OpenCV_INCLUDE_DIRS})
target_link_libraries(d2i_impl PUBLIC d2i_rs ${OpenCV_LIBS} bcrypt)
target_compile_definitions(d2i_impl PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(d2i_impl PRIVATE /W4 /utf-8 /permissive-)

add_library(data2img_core SHARED source/api.cpp)
target_link_libraries(data2img_core PRIVATE d2i_impl)
target_include_directories(data2img_core PUBLIC include PRIVATE source)
target_compile_definitions(data2img_core PRIVATE D2I_BUILD_DLL NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(data2img_core PRIVATE /W4 /utf-8 /permissive-)
set_target_properties(data2img_core PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin"
    ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/lib")

# SDK 按配置分开，避免 Debug 与 Release 产物互相覆盖。
add_custom_command(TARGET data2img_core POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_SOURCE_DIR}/api/include"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_SOURCE_DIR}/api/bin/$<CONFIG>"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_SOURCE_DIR}/api/lib/$<CONFIG>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/include/data2img.h" "${CMAKE_CURRENT_SOURCE_DIR}/api/include/data2img.h"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:data2img_core>" "${CMAKE_CURRENT_SOURCE_DIR}/api/bin/$<CONFIG>/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_LINKER_FILE:data2img_core>" "${CMAKE_CURRENT_SOURCE_DIR}/api/lib/$<CONFIG>/"
    VERBATIM)

# 兼容合并的 world 包和独立模块包，只复制实际使用的动态库。
set(D2I_CV_TARGETS ${OpenCV_LIBS})
if(TARGET opencv_world)
    set(D2I_CV_TARGETS opencv_world)
endif()
foreach(cvTarget IN LISTS D2I_CV_TARGETS)
    if(TARGET ${cvTarget})
        get_target_property(cvType ${cvTarget} TYPE)
        if(cvType STREQUAL "SHARED_LIBRARY")
            add_custom_command(TARGET data2img_core POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${cvTarget}>" "$<TARGET_FILE_DIR:data2img_core>/"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${cvTarget}>" "${CMAKE_CURRENT_SOURCE_DIR}/api/bin/$<CONFIG>/"
                VERBATIM)
        endif()
    endif()
endforeach()

# SDK 同步携带直接依赖的许可文本，源码和上游子模块保持原样。
add_custom_command(TARGET data2img_core POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_SOURCE_DIR}/api/licenses"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${RS_ROOT}/LICENSE" "${CMAKE_CURRENT_SOURCE_DIR}/api/licenses/libcorrect-LICENSE.txt"
    VERBATIM)
find_file(D2I_OPENCV_LICENSE NAMES LICENSE LICENSE.txt
    PATHS "${OpenCV_INSTALL_PATH}" "${OpenCV_DIR}/../../.." "${OpenCV_DIR}" NO_DEFAULT_PATH)
if(D2I_OPENCV_LICENSE)
    add_custom_command(TARGET data2img_core POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${D2I_OPENCV_LICENSE}" "${CMAKE_CURRENT_SOURCE_DIR}/api/licenses/OpenCV-LICENSE.txt"
        VERBATIM)
endif()
