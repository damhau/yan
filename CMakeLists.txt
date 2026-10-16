cmake_minimum_required(VERSION 3.21)
# Version = latest git tag vX.Y.Z (CI tags each release from conventional commits).
set(YAN_VERSION 0.0.0)
find_package(Git QUIET)
if(GIT_FOUND)
  execute_process(COMMAND ${GIT_EXECUTABLE} describe --tags --abbrev=0 --match "v[0-9]*"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} OUTPUT_VARIABLE YAN_TAG
    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE YAN_TAG_RESULT)
  if(YAN_TAG_RESULT EQUAL 0 AND YAN_TAG MATCHES "^v([0-9]+)[.]([0-9]+)[.]([0-9]+)$")
    set(YAN_VERSION ${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3})
  endif()
endif()
project(yan VERSION ${YAN_VERSION} LANGUAGES CXX RC)
configure_file(src/version.h.in ${CMAKE_BINARY_DIR}/generated/version.h @ONLY)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static CRT: no Visual C++ redistributable needed, the exe is self-contained.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_compile_options(/utf-8 /EHsc /permissive- /MP /Gy /Gw)
add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
set(CMAKE_CXX_FLAGS_RELEASE "/O2 /Ob2 /GL /DNDEBUG")

# ---- Scintilla, linked statically ------------------------------------------
set(SCI ${CMAKE_SOURCE_DIR}/third_party/scintilla)
file(GLOB SCI_SOURCES ${SCI}/src/*.cxx ${SCI}/win32/*.cxx)
list(FILTER SCI_SOURCES EXCLUDE REGEX "ScintillaDLL\\.cxx$")
add_library(scintilla STATIC ${SCI_SOURCES})
target_include_directories(scintilla PUBLIC ${SCI}/include PRIVATE ${SCI}/src)
# We use Boost.Regex for searching, so drop Scintilla's std::regex support.
target_compile_definitions(scintilla PRIVATE NO_CXX11_REGEX STATIC_BUILD)
target_compile_options(scintilla PRIVATE /W1)

# ---- Lexilla, linked statically: only the lexers we use ---------------------
set(LEX ${CMAKE_SOURCE_DIR}/third_party/lexilla)
file(GLOB LEX_LIB_SOURCES ${LEX}/lexlib/*.cxx)
set(LEX_LEXERS Batch Bash CPP Cmake CSS Diff HTML JSON Lua Make Markdown PowerShell Props Python Rust SQL YAML)
list(TRANSFORM LEX_LEXERS PREPEND ${LEX}/lexers/Lex)
list(TRANSFORM LEX_LEXERS APPEND .cxx)
add_library(lexilla STATIC ${LEX_LIB_SOURCES} ${LEX_LEXERS})
target_include_directories(lexilla PUBLIC ${LEX}/include ${LEX}/lexlib)
target_link_libraries(lexilla PUBLIC scintilla) # ILexer.h
target_compile_options(lexilla PRIVATE /W1)

# ---- Yan --------------------------------------------------------------------
add_executable(yan WIN32
  src/main.cpp
  src/util.cpp
  src/theme.cpp
  src/fileio.cpp
  src/editor.cpp
  src/search.cpp
  src/tabbar.cpp
  src/panel.cpp
  src/statusbar.cpp
  src/language.cpp
  src/settings.cpp
  res/yan.rc
)
target_include_directories(yan PRIVATE src ${CMAKE_BINARY_DIR}/generated ${CMAKE_SOURCE_DIR}/third_party/boost-regex/include)
target_compile_definitions(yan PRIVATE BOOST_REGEX_STANDALONE BOOST_REGEX_NO_LIB)
target_compile_options(yan PRIVATE /W3)
target_link_libraries(yan PRIVATE scintilla lexilla
  user32 gdi32 imm32 ole32 oleaut32 uuid advapi32 dwmapi uxtheme shell32 comdlg32 comctl32 msimg32 delayimp)
# Tests: search engine, column editor, line operations, file encodings.
add_executable(yan_tests tests/tests.cpp src/util.cpp src/fileio.cpp src/editor.cpp src/search.cpp src/theme.cpp src/language.cpp)
target_include_directories(yan_tests PRIVATE src ${CMAKE_SOURCE_DIR}/third_party/boost-regex/include)
target_compile_definitions(yan_tests PRIVATE BOOST_REGEX_STANDALONE BOOST_REGEX_NO_LIB)
target_link_libraries(yan_tests PRIVATE scintilla lexilla user32 gdi32 imm32 ole32 oleaut32 advapi32 dwmapi uxtheme msimg32)

# Load rarely needed DLLs only when first used: keeps startup fast.
target_link_options(yan PRIVATE /MANIFEST:NO /OPT:REF /OPT:ICF
  /DELAYLOAD:shell32.dll /DELAYLOAD:comdlg32.dll /DELAYLOAD:oleaut32.dll /DELAYLOAD:comctl32.dll
  $<$<CONFIG:Release>:/LTCG>)
