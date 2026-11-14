# Game Title (original Xbox), statically recompiled

cmake_minimum_required(VERSION 3.25)
project(game LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# the SDK: sdk/ here, two folders up inside the SDK's templates, a checkout beside this repository, else downloaded
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/sdk/cmake/XbeRecomp.cmake)
    set(SDK ${CMAKE_CURRENT_SOURCE_DIR}/sdk)
elseif(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/XbeRecomp.cmake)
    set(SDK ${CMAKE_CURRENT_SOURCE_DIR}/../..)
elseif(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../XboxXBE-Recomp/cmake/XbeRecomp.cmake)
    set(SDK ${CMAKE_CURRENT_SOURCE_DIR}/../XboxXBE-Recomp)
else()
    include(FetchContent)
    FetchContent_Declare(xbe_sdk GIT_REPOSITORY https://github.com/TGP482/XboxXBE-Recomp.git GIT_TAG main)
    FetchContent_MakeAvailable(xbe_sdk)
    set(SDK ${xbe_sdk_SOURCE_DIR})
endif()
include(${SDK}/cmake/XbeRecomp.cmake)

file(GLOB XBE_HOST_SOURCES CONFIGURE_DEPENDS src/*/*.cpp)
list(FILTER XBE_HOST_SOURCES EXCLUDE REGEX "/src/game/")

# partitions as xbe_symbols wrote them into config/game.toml
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS config/game.toml)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/config/game.toml)
    file(STRINGS config/game.toml GAME_PARTITIONS REGEX "^partitions *=")
    string(REGEX MATCH "[0-9]+" GAME_PARTITIONS "${GAME_PARTITIONS}")
endif()
# no config yet: build.bat writes it with xbe_symbols, the change reconfigures
if(NOT GAME_PARTITIONS)
    set(GAME_PARTITIONS 8)
endif()

# target name = GAME_EXE in build.bat, xbe = GAME_XBE
add_xbe_image(TS2 default.xbe game.toml ${GAME_PARTITIONS} game)

# the executable's icon: assets/icon.ico
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.ico)
    enable_language(RC)
    set(GAME_ICON_RC ${CMAKE_CURRENT_BINARY_DIR}/icon.rc)
    file(WRITE ${GAME_ICON_RC} "1 ICON \"${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.ico\"
")
    target_sources(TS2 PRIVATE ${GAME_ICON_RC})
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS assets/icon.ico)
endif()

# the launcher: status of the game files / ISO, options, profiles, MapMaker maps and updates (launcher/main.cpp)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/launcher/main.cpp)
    set(LAUNCHER_SOURCES
        launcher/main.cpp
        ${SDK}/runtime/kernel/disc.cpp
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/assets/default.xbe)
        file(SHA1 ${CMAKE_CURRENT_SOURCE_DIR}/assets/default.xbe TS2_XBE_SHA1)
    endif()
    add_executable(TS2Launcher WIN32 ${LAUNCHER_SOURCES} ${GAME_ICON_RC})
    target_include_directories(TS2Launcher SYSTEM PRIVATE ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
    target_include_directories(TS2Launcher PRIVATE ${SDK}/runtime)
    target_compile_definitions(TS2Launcher PRIVATE UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS TS2_XBE_SHA1="${TS2_XBE_SHA1}")
    target_link_libraries(TS2Launcher PRIVATE tomlplusplus::tomlplusplus d3d11 dxgi bcrypt winhttp comdlg32 shell32 user32 gdi32 ole32 advapi32 windowscodecs)
    add_custom_command(TARGET TS2Launcher POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/art $<TARGET_FILE_DIR:TS2Launcher>/art)
    target_link_options(TS2Launcher PRIVATE -fuse-ld=lld)
endif()
