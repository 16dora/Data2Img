# GUI 单独开关，不要求仅构建 CLI 的使用者安装 Qt。
find_package(Qt6 6.7 REQUIRED COMPONENTS Widgets)

add_executable(screen_codec_gui WIN32
    source/main.cpp
    source/main_window.cpp
    source/drop_text_edit.cpp
    source/codec_worker.cpp
    include/main_window.h
    include/drop_text_edit.h
    include/codec_worker.h
    main_window.ui)
target_include_directories(screen_codec_gui PRIVATE include)
target_link_libraries(screen_codec_gui PRIVATE Qt6::Widgets data2img_core)
target_compile_options(screen_codec_gui PRIVATE /W4 /utf-8 /permissive-)
target_compile_definitions(screen_codec_gui PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
set_target_properties(screen_codec_gui PROPERTIES
    AUTOMOC ON
    AUTOUIC ON
    AUTOUIC_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}"
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")

# 构建后即具备 Qt 平台插件，支持从资源管理器直接双击运行。
get_target_property(D2I_QMAKE Qt6::qmake IMPORTED_LOCATION)
get_filename_component(D2I_QT_BIN "${D2I_QMAKE}" DIRECTORY)
find_program(D2I_WINDEPLOYQT NAMES windeployqt HINTS "${D2I_QT_BIN}" NO_DEFAULT_PATH REQUIRED)
add_custom_command(TARGET screen_codec_gui POST_BUILD
    COMMAND "${D2I_WINDEPLOYQT}" "$<IF:$<CONFIG:Debug>,--debug,--release>"
        --no-translations --no-compiler-runtime --no-system-d3d-compiler --no-opengl-sw
        "$<TARGET_FILE:screen_codec_gui>"
    VERBATIM)

# 与 CLI 共用平铺安装目录，仅部署本次 GUI 所需的 Qt 运行依赖。
install(TARGETS screen_codec_gui RUNTIME DESTINATION ".")
qt_generate_deploy_app_script(TARGET screen_codec_gui
    OUTPUT_SCRIPT D2I_GUI_DEPLOY
    NO_TRANSLATIONS
    NO_COMPILER_RUNTIME
    DEPLOY_TOOL_OPTIONS --no-system-d3d-compiler --no-opengl-sw)
# Qt 的部署支持在 find_package 时已固定默认目录，在安装脚本中覆盖为项目平铺布局。
install(CODE "set(QT_DEPLOY_BIN_DIR \".\")\nset(QT_DEPLOY_LIB_DIR \".\")")
install(SCRIPT "${D2I_GUI_DEPLOY}")
