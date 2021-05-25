# CMake build for Cheetah.

cmake_minimum_required(VERSION 3.9)

if(POLICY CMP0068)
  cmake_policy(SET CMP0068 NEW)
  set(CMAKE_BUILD_WITH_INSTALL_NAME_DIR ON)
endif()

# Add path for custom cheetah modules.
set(CMAKE_MODULE_PATH
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake"
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Modules"
  ${CMAKE_MODULE_PATH}
  )

# Check if cheetah is built as a standalone project.
if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR OR CHEETAH_STANDALONE_BUILD)
  project(Cheetah CXX C)
  set_property(GLOBAL PROPERTY USE_FOLDERS ON)

  set(PACKAGE_NAME Cheetah)
  set(PACKAGE_VERSION 12.0.0)
  set(PACKAGE_STRING "${PACKAGE_NAME} ${PACKAGE_VERSION}")
  set(PACKAGE_BUGREPORT "bugs@opencilk.org")

  # Find the LLVM sources and simulate LLVM CMake options.
  include(HandleOutOfTreeLLVM)
endif()

# Require out of source build.
include(MacroEnsureOutOfSourceBuild)
MACRO_ENSURE_OUT_OF_SOURCE_BUILD(
  "${PROJECT_NAME} requires an out of source build. Please create a separate
 build directory and run 'cmake /path/to/${PROJECT_NAME} [options]' there."
  )

include(base-config-ix)
include(CheetahUtils)

#===============================================================================
# Setup CMake Options
#===============================================================================
include(CMakeDependentOption)
include(HandleCompilerRT)

# Basic options ---------------------------------------------------------------
option(CHEETAH_ENABLE_ASSERTIONS "Enable assertions independent of build mode." OFF)
option(CHEETAH_ENABLE_WERROR "Fail and stop if a warning is triggered." OFF)
option(CHEETAH_USE_COMPILER_RT "Use compiler-rt instead of libgcc" OFF)

option(CHEETAH_INCLUDE_TESTS "Generate build targets for the cheetah unit tests." ${LLVM_INCLUDE_TESTS})
option(CHEETAH_INSTALL_LIBRARY "Install the cheetah library." ON)

option(CHEETAH_ENABLE_SHARED "Build cheetah as a shared library." ON)
option(CHEETAH_ENABLE_STATIC "Build cheetah as a static library." ON)

set(CHEETAH_ABI_VERSION "1" CACHE STRING "ABI version of cheetah. Defaults to 1.")

if (NOT CHEETAH_ENABLE_SHARED AND NOT CHEETAH_ENABLE_STATIC)
  message(FATAL_ERROR "cheetah must be built as either a shared or static library.")
endif()

# Target options --------------------------------------------------------------

# Default minimum OSX version to support, if
# CMAKE_OSX_DEPLOYMENT_TARGET is not specified
set(DEFAULT_CHEETAH_MIN_OSX_VERSION 10.14)

#===============================================================================
# Configure System
#===============================================================================

construct_cheetah_default_triple()
if ("${CHEETAH_DEFAULT_TARGET_TRIPLE}" MATCHES ".*hf$")
  if (${CHEETAH_DEFAULT_TARGET_ARCH} MATCHES "^arm")
    set(CHEETAH_DEFAULT_TARGET_ARCH "armhf")
  endif()
endif()

set(CHEETAH_C_FLAGS "")
set(CHEETAH_CXX_FLAGS "")
set(CHEETAH_COMPILE_FLAGS "")
set(CHEETAH_COMPILE_DEFS "")
set(CHEETAH_LINK_FLAGS "")
set(CHEETAH_COMMON_LIBS "")

option(CHEETAH_EXTERNALIZE_DEBUGINFO
  "Generate dSYM files and strip executables and libraries (Darwin Only)" OFF)

# Include macros for adding and removing cheetah flags.
include(HandleCheetahFlags)

#===============================================================================
# Setup Compiler Flags
#===============================================================================

# Configure compiler.
include(config-ix)

if (CHEETAH_USE_COMPILER_RT)
  list(APPEND CHEETAH_LINK_FLAGS "--rtlib=compiler-rt")
endif()

# Get warning flags
add_compile_flags_if_supported(-Wall)

if (CHEETAH_ENABLE_WERROR)
  add_compile_flags_if_supported(-Werror)
else()
  add_compile_flags_if_supported(-Wno-error)
endif()

# The spawn_main symbol in cheetah is undefined.  This routine
# corresponds to the entry point of the compiled Cilk program.
set(CHEETAH_HAS_UNDEFINED_SYMBOLS ON)

if (CHEETAH_HAS_UNDEFINED_SYMBOLS)
  # Need to allow unresolved symbols if this is to work with shared library builds
  if (APPLE)
    list(APPEND CHEETAH_LINK_FLAGS "-undefined dynamic_lookup")
  else()
    # Relax this restriction from HandleLLVMOptions
    string(REPLACE "-Wl,-z,defs" "" CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS}")
  endif()
endif()

string(REPLACE ";" " " CHEETAH_CXX_FLAGS "${CHEETAH_CXX_FLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CHEETAH_CXX_FLAGS}")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${CHEETAH_C_FLAGS}")

include(AddCheetah)

#===============================================================================
# Setup Source Code
#===============================================================================

include_directories(runtime)

# Add source code. This also contains all of the logic for deciding linker flags
# soname, etc...
add_subdirectory(include)
add_subdirectory(runtime)

if (CHEETAH_INCLUDE_TESTS)
  # TODO: Set up CMake for Cheetah tests.
  # add_subdirectory(handcomp_test)
  # add_subdirectory(bench)
endif()
