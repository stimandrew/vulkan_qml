# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

cmake_minimum_required(VERSION 3.16)
project(vulkanunderqml LANGUAGES CXX)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick)

# Поиск компилятора шейдеров
find_program(GLSLC_EXECUTABLE NAMES glslc glslangValidator)
if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc or glslangValidator not found! Install Vulkan SDK or glslang-tools")
endif()

qt_standard_project_setup()

# Настройка путей для шейдеров
set(SHADER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(SHADER_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/compiled_shaders)
set(SHADER_OUTPUT_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# Создание директорий
file(MAKE_DIRECTORY ${SHADER_BINARY_DIR})

# Функция для компиляции шейдеров
function(compile_shader SHADER_NAME SHADER_TYPE)
    set(INPUT_FILE ${SHADER_SOURCE_DIR}/${SHADER_NAME}.${SHADER_TYPE})
    set(OUTPUT_FILE ${SHADER_BINARY_DIR}/${SHADER_NAME}.${SHADER_TYPE}.spv)
    set(FINAL_OUTPUT ${SHADER_OUTPUT_DIR}/${SHADER_NAME}.${SHADER_TYPE}.spv)

    if(EXISTS ${INPUT_FILE})
        add_custom_command(
            OUTPUT ${OUTPUT_FILE}
            COMMAND ${GLSLC_EXECUTABLE} -V -o ${OUTPUT_FILE} ${INPUT_FILE}
            DEPENDS ${INPUT_FILE}
            COMMENT "Compiling ${SHADER_NAME}.${SHADER_TYPE}"
            VERBATIM
        )

        # Копируем в исходную директорию для qrc
        add_custom_command(
            OUTPUT ${FINAL_OUTPUT}
            COMMAND ${CMAKE_COMMAND} -E copy ${OUTPUT_FILE} ${FINAL_OUTPUT}
            DEPENDS ${OUTPUT_FILE}
            COMMENT "Copying ${SHADER_NAME}.${SHADER_TYPE}.spv to source directory"
            VERBATIM
        )

        list(APPEND COMPILED_SHADERS ${FINAL_OUTPUT})
        set(COMPILED_SHADERS ${COMPILED_SHADERS} PARENT_SCOPE)
    else()
        message(WARNING "Shader file ${INPUT_FILE} not found")
    endif()
endfunction()

# Автоматическое обнаружение шейдеров
file(GLOB VERTEX_SHADERS "${SHADER_SOURCE_DIR}/*.vert")
file(GLOB FRAGMENT_SHADERS "${SHADER_SOURCE_DIR}/*.frag")

# Компилируем все найденные шейдеры
foreach(SHADER_PATH ${VERTEX_SHADERS})
    get_filename_component(SHADER_NAME ${SHADER_PATH} NAME_WE)
    compile_shader(${SHADER_NAME} vert)
endforeach()

foreach(SHADER_PATH ${FRAGMENT_SHADERS})
    get_filename_component(SHADER_NAME ${SHADER_PATH} NAME_WE)
    compile_shader(${SHADER_NAME} frag)
endforeach()

# Специально для шейдеров из примера
if(EXISTS ${SHADER_SOURCE_DIR}/squircle.vert)
    compile_shader(squircle vert)
endif()

if(EXISTS ${SHADER_SOURCE_DIR}/squircle.frag)
    compile_shader(squircle frag)
endif()

# Компилируем шейдеры для куба
if(EXISTS ${SHADER_SOURCE_DIR}/cube.vert)
    compile_shader(cube vert)
endif()

if(EXISTS ${SHADER_SOURCE_DIR}/cube.frag)
    compile_shader(cube frag)
endif()

# Создаем цель для шейдеров
if(COMPILED_SHADERS)
    add_custom_target(shaders ALL DEPENDS ${COMPILED_SHADERS})
endif()

qt_add_executable(vulkanunderqml WIN32 MACOSX_BUNDLE
    main.cpp
    vulkansquircle.cpp vulkansquircle.h
    vulkancube.cpp vulkancube.h
)

set_target_properties(vulkanunderqml PROPERTIES
    OUTPUT_NAME vulkanunderqmlapp
)

target_link_libraries(vulkanunderqml PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
)

# Добавляем зависимость от компиляции шейдеров
if(TARGET shaders)
    add_dependencies(vulkanunderqml shaders)
endif()

qt_add_qml_module(vulkanunderqml
    URI VulkanUnderQML
    QML_FILES
        main.qml
    RESOURCES
        squircle.frag.spv
        squircle.vert.spv
        cube.frag.spv
        cube.vert.spv
    RESOURCE_PREFIX /
    NO_RESOURCE_TARGET_PATH
    SOURCES vulkancube.h vulkancube.cpp
    RESOURCES textures/metalplate01_rgba.png
    SOURCES vulkanquickwindow.h vulkanquickwindow.cpp
)

install(TARGETS vulkanunderqml
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_qml_app_script(
    TARGET vulkanunderqml
    OUTPUT_SCRIPT deploy_script
    MACOS_BUNDLE_POST_BUILD
    NO_UNSUPPORTED_PLATFORM_ERROR
    DEPLOY_USER_QML_MODULES_ON_UNSUPPORTED_PLATFORM
)
install(SCRIPT ${deploy_script})
