cmake_minimum_required(VERSION 3.16)
# must precede project() to take effect
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Oldest macOS supported")
project(curl-ui VERSION 0.2.1 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(CURLUI_BUILD_GUI "Build the SDL2 application" ON)
option(CURLUI_SANITIZE "Build with ASan and UBSan" OFF)
if(MSVC)
  # static CRT, so the installer needs no redistributable
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
  add_compile_options(/utf-8 /W3)
else()
  add_compile_options(-Wall -Wextra)
endif()
if(CURLUI_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g -fno-sanitize-recover=undefined)
  add_link_options(-fsanitize=address,undefined)
endif()

find_package(Threads REQUIRED)

# ---- core: no SDL, unit tested ----
add_library(curlui_core STATIC
  src/core/options.c src/core/options_data.c src/core/builder.c
  src/core/procrun.c src/core/outbuf.c src/core/json_fmt.c
  src/core/curlinfo.c src/core/docs.c src/core/settings.c)
target_include_directories(curlui_core PUBLIC src/core)
target_link_libraries(curlui_core PUBLIC Threads::Threads)
if(NOT WIN32)
  target_compile_definitions(curlui_core PRIVATE _DEFAULT_SOURCE)
endif()

# Regenerate from curl's docs: cmake --build build --target regen-options
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
  add_custom_target(regen-options
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_options.py
    COMMENT "Regenerating src/core/options_data.c")
endif()

enable_testing()
add_executable(echoargs tests/echoargs.c)
if(WIN32)
  target_link_libraries(echoargs PRIVATE shell32)
endif()
add_executable(test_core tests/test_core.c)
target_link_libraries(test_core PRIVATE curlui_core)
set(CURLUI_TEST_CURL "curl" CACHE STRING "curl used by the end-to-end test")
add_test(NAME core COMMAND test_core $<TARGET_FILE:echoargs> ${CURLUI_TEST_CURL})

# ---- the application ----
if(CURLUI_BUILD_GUI)
  if(WIN32 OR APPLE)
    # static SDL: the installers carry everything they need
    include(FetchContent)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    set(SDL_FORCE_STATIC_VCRT ON CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL2
      URL https://github.com/libsdl-org/SDL/releases/download/release-2.30.12/SDL2-2.30.12.tar.gz
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(SDL2)
    set(CURLUI_SDL SDL2::SDL2-static)
    if(TARGET SDL2::SDL2main)
      list(PREPEND CURLUI_SDL SDL2::SDL2main)
    endif()
  else()
    find_package(SDL2 REQUIRED)
    if(TARGET SDL2::SDL2)
      set(CURLUI_SDL SDL2::SDL2)
    else()
      set(CURLUI_SDL ${SDL2_LIBRARIES})
      include_directories(${SDL2_INCLUDE_DIRS})
    endif()
  endif()

  set(CURLUI_SOURCES
    src/ui/main.c src/ui/app.c src/ui/theme.c src/ui/draw.c src/ui/fonts_data.c
    src/ui/view_frame.c src/ui/view_library.c src/ui/view_center.c
    src/ui/view_output.c src/ui/view_docs.c src/ui/view_settings.c)
  if(WIN32)
    list(APPEND CURLUI_SOURCES packaging/curl-ui.rc)
    if(MINGW)
      enable_language(RC)
    endif()
  endif()
  add_executable(curl-ui WIN32 MACOSX_BUNDLE ${CURLUI_SOURCES})
  target_include_directories(curl-ui SYSTEM PRIVATE third_party)
  target_include_directories(curl-ui PRIVATE src/ui)
  target_link_libraries(curl-ui PRIVATE curlui_core ${CURLUI_SDL})
  if(NOT WIN32)
    target_link_libraries(curl-ui PRIVATE m)
  endif()
  if(MINGW)
    target_link_options(curl-ui PRIVATE -static)
  endif()
  if(APPLE)
    set_target_properties(curl-ui PROPERTIES
      OUTPUT_NAME "curl UI"
      MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/packaging/Info.plist.in
      MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
      MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})
    target_sources(curl-ui PRIVATE packaging/curl-ui.icns)
    set_source_files_properties(packaging/curl-ui.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
  endif()

  # ---- installers ----
  include(GNUInstallDirs)
  if(APPLE)
    install(TARGETS curl-ui BUNDLE DESTINATION .)
  elseif(WIN32)
    install(TARGETS curl-ui RUNTIME DESTINATION .)
    install(FILES LICENSE README.md DESTINATION .)
  else()
    install(TARGETS curl-ui RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES packaging/curl-ui.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES packaging/curl-ui.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps)
    install(FILES LICENSE README.md DESTINATION ${CMAKE_INSTALL_DOCDIR})
  endif()

  set(CPACK_PACKAGE_NAME "curl-ui")
  set(CPACK_PACKAGE_VENDOR "curl UI contributors")
  set(CPACK_PACKAGE_CONTACT "curl UI contributors <noreply@example.com>")
  set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "A desktop front end for curl: all of its options, explained")
  set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
  set(CPACK_PACKAGE_INSTALL_DIRECTORY "curl UI")
  set(CPACK_RESOURCE_FILE_LICENSE ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE)
  set(CPACK_STRIP_FILES ON)
  if(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
    set(CPACK_DMG_VOLUME_NAME "curl UI")
    set(CPACK_PACKAGE_FILE_NAME "curl-ui-${PROJECT_VERSION}-macos-universal")
  elseif(WIN32)
    set(CPACK_GENERATOR "NSIS;ZIP")
    set(CPACK_PACKAGE_FILE_NAME "curl-ui-${PROJECT_VERSION}-windows-x64")
    set(CPACK_PACKAGE_EXECUTABLES "curl-ui" "curl UI")
    set(CPACK_NSIS_EXECUTABLES_DIRECTORY ".")
    set(CPACK_NSIS_DISPLAY_NAME "curl UI")
    set(CPACK_NSIS_PACKAGE_NAME "curl UI")
    set(CPACK_NSIS_INSTALLED_ICON_NAME "curl-ui.exe")
    file(TO_NATIVE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/packaging/curl-ui.ico" CURLUI_ICO)
    string(REPLACE "\\" "\\\\" CURLUI_ICO "${CURLUI_ICO}")
    set(CPACK_NSIS_MUI_ICON "${CURLUI_ICO}")
    set(CPACK_NSIS_MUI_UNIICON "${CURLUI_ICO}")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
  else()
    set(CPACK_GENERATOR "DEB;RPM")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_DEBIAN_PACKAGE_SECTION "net")
    # curl is what this drives: required at run time, though --version detection copes
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "curl")
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    # declared, not left to the scanner: the soname works on every RPM distro,
    # whichever package provides it (SDL2 or sdl2-compat)
    set(CPACK_RPM_PACKAGE_REQUIRES "curl, libSDL2-2.0.so.0()(64bit)")
    set(CPACK_RPM_PACKAGE_LICENSE "curl AND MIT AND OFL-1.1")
    set(CPACK_RPM_PACKAGE_GROUP "Applications/Internet")
    set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
    set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
        /usr/share/applications /usr/share/icons /usr/share/icons/hicolor
        /usr/share/icons/hicolor/256x256 /usr/share/icons/hicolor/256x256/apps)
  endif()
  include(CPack)
endif()
