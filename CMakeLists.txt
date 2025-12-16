# ==========================================
# 1. 初始化项目 V3
# ==========================================
cmake_minimum_required(VERSION 3.10)

# 定义项目名称
project(CMakePro_wave_V3 LANGUAGES CXX C)

# 定义最终生成的可执行文件名（统一变量，防止手误）
set(TARGET_NAME "CMakePro_wave_V3")

# ==========================================
# 2. 定义第三方库路径 (方便统一修改)
# ==========================================
set(LIB_ROOT "D:/CPP_lib")
set(OPENGL_ROOT "${LIB_ROOT}/OpenGL")
set(SDL2_ROOT   "${LIB_ROOT}/SDL2/SDL2-devel-2.30.9-VC/SDL2-2.30.9")

# ==========================================
# 3. 收集源文件
# ==========================================
# ImGui 源文件
file(GLOB IMGUI_SRCS
    "ImGui/imgui.cpp"
    "ImGui/imgui_demo.cpp"
    "ImGui/imgui_draw.cpp"
    "ImGui/imgui_tables.cpp"
    "ImGui/imgui_widgets.cpp"
    "ImGui/imgui_impl_glfw.cpp"
    "ImGui/imgui_impl_opengl3.cpp"
)

# STB Image 源文件
set(STB_SRCS "stb_image_cpp/stb_image.cpp")

# 项目源文件
set(PROJECT_SRCS
"stb_image/stb_image_write.h" 
    "CMakePro_game1.cpp"
    "CMakePro_fdm_v3_imgui.h"
    "CMakePro_fdm_V3_1.h"
    "CMakePro_fdm_V3_2.h"  
    "CMakePro_fdm_V3_3.h"
)

# ==========================================
# 4. 创建可执行文件目标
# ==========================================
add_executable(${TARGET_NAME}
    ${PROJECT_SRCS}
    ${IMGUI_SRCS}
    ${STB_SRCS}
 )

# 1. 定义资源源目录
set(RESOURCE_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/resources)

# 2. 自动递归地查找 "resources" 目录下的所有文件，用于建立依赖关系
file(GLOB_RECURSE RESOURCE_FILES "${RESOURCE_SRC_DIR}/*")

# 3. 定义资源要被复制到的目标目录
set(RESOURCE_DEST_DIR ${CMAKE_BINARY_DIR}/resources)

# 4. 创建一个自定义目标来执行目录复制
add_custom_target(copy_resources
    # 这个命令更简单，直接复制整个目录
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
            ${RESOURCE_SRC_DIR} # 源目录
            ${RESOURCE_DEST_DIR}  # 目标目录
    # 【关键】告诉CMake，这个操作依赖于我们找到的所有资源文件
    # 任何一个文件被修改，这个目标就会被认为是“过时的”，需要重新执行
    DEPENDS ${RESOURCE_FILES}
    COMMENT "Syncing resource files to output directory..."
)

# 5. 告诉CMake，你的主程序依赖于这个复制操作
add_dependencies(${TARGET_NAME} copy_resources)


# 设置 C++ 标准 (C++20)
if(CMAKE_VERSION VERSION_GREATER 3.12)
    set_property(TARGET ${TARGET_NAME} PROPERTY CXX_STANDARD 20)
    set_property(TARGET ${TARGET_NAME} PROPERTY CXX_STANDARD_REQUIRED ON)
endif()

# ==========================================
# 5. 配置 OpenMP
# ==========================================
if(MSVC)
    # Visual Studio 强制开启
    message(STATUS "Force enabling OpenMP for MSVC...")
    target_compile_options(${TARGET_NAME} PRIVATE /openmp)
else()
    # 其他编译器自动查找
    find_package(OpenMP)
    if(OpenMP_CXX_FOUND)
        target_link_libraries(${TARGET_NAME} PRIVATE OpenMP::OpenMP_CXX)
    endif()
endif()

# ==========================================
# 6. 配置头文件路径 (Include Directories)
# ==========================================
target_include_directories(${TARGET_NAME} PRIVATE
    # 本地目录
    "${CMAKE_CURRENT_SOURCE_DIR}/ImGui"
    "${CMAKE_CURRENT_SOURCE_DIR}/stb_image" # 假设你的stb_image.h在这里
    
    # 第三方库目录
    "${OPENGL_ROOT}/glm-master/glm-master"
    "${OPENGL_ROOT}/glew-2.1.0-win32/glew-2.1.0/include"
    "${OPENGL_ROOT}/glfw-3.4.bin.WIN64/glfw-3.4.bin.WIN64/include"
    "${SDL2_ROOT}/include"
)

# ==========================================
# 7. 配置库文件路径 (Link Directories)
# ==========================================
target_link_directories(${TARGET_NAME} PRIVATE
    "${OPENGL_ROOT}/glfw-3.4.bin.WIN64/glfw-3.4.bin.WIN64/lib-vc2022"
    "${OPENGL_ROOT}/glew-2.1.0-win32/glew-2.1.0/lib/Release/x64"
    "${SDL2_ROOT}/lib/x64"
)

# ==========================================
# 8. 链接库文件 & 宏定义
# ==========================================
# GLEW 静态链接宏
target_compile_definitions(${TARGET_NAME} PRIVATE GLEW_STATIC)

target_link_libraries(${TARGET_NAME} PRIVATE
    # OpenGL 基础库
    glfw3.lib
    glew32s.lib
    opengl32.lib
    user32.lib
    
    # SDL2 库
    SDL2.lib
    SDL2_image.lib
    SDL2_mixer.lib
)

# (可选) 在 VS 中将源文件分组显示，方便查看
source_group("Source Files\\ImGui" FILES ${IMGUI_SRCS})
source_group("Source Files\\Utils" FILES ${STB_SRCS})