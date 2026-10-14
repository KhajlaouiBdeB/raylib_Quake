cmake_minimum_required(VERSION 3.16)
project(raylib_Quake C)

# The WinQuake software renderer. With QUAKE_RAYLIB, `quake` runs with the
# raylib drivers; `quake-null` (always built) uses the null drivers, has no
# raylib in it, and is the dedicated server / headless build.
# Without QUAKE_RAYLIB, `quake` is the null build.
option(QUAKE_RAYLIB "Build with the raylib video/sound/input drivers" ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# gnu11 keeps `false`/`true` usable as qboolean enumerators: they are
# keywords in C23, which GCC 15 uses by default.
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

set(WQ ${CMAKE_CURRENT_SOURCE_DIR}/WinQuake)

set(QUAKE_CORE_SOURCES
  cl_demo.c cl_input.c cl_main.c cl_parse.c cl_tent.c chase.c
  cmd.c common.c console.c crc.c cvar.c
  host.c host_cmd.c keys.c menu.c mathlib.c model.c
  net_main.c net_loop.c net_dgrm.c net_vcr.c
  pr_cmds.c pr_edict.c pr_exec.c
  sbar.c screen.c view.c wad.c world.c zone.c
  sv_main.c sv_move.c sv_phys.c sv_user.c
  # software renderer (C versions of the x86 asm, id386 == 0)
  draw.c nonintel.c
  d_edge.c d_fill.c d_init.c d_modech.c d_part.c d_polyse.c d_scan.c
  d_sky.c d_sprite.c d_surf.c d_vars.c d_zpoint.c
  r_aclip.c r_alias.c r_bsp.c r_draw.c r_edge.c r_efrag.c r_light.c
  r_main.c r_misc.c r_part.c r_sky.c r_sprite.c r_surf.c r_vars.c
)

# Platform drivers
set(QUAKE_NULL_DRIVER_SOURCES
  sys_linux.c
  net_udp.c net_bsd.c
  vid_null.c snd_null.c in_null.c cd_null.c
)

# Video/input drivers: if raylib video driver exists (QUAK-5), use it;
# otherwise fall back to null video/input so audio can be verified independently.
if(EXISTS ${WQ}/vid_raylib.c)
  set(QUAKE_VIDEO_DRIVER_SOURCES
    sys_raylib.c vid_raylib.c in_raylib.c bridge_raylib.c
  )
else()
  set(QUAKE_VIDEO_DRIVER_SOURCES
    sys_linux.c vid_null.c in_null.c
  )
endif()

# Raylib sound drivers: DMA sound engine (snd_dma, snd_mem, snd_mix, snd_raylib)
# and CD audio emulation (cd_raylib).
set(QUAKE_RAYLIB_DRIVER_SOURCES
  ${QUAKE_VIDEO_DRIVER_SOURCES}
  net_udp.c net_bsd.c
  snd_dma.c snd_mem.c snd_mix.c snd_raylib.c cd_raylib.c
)

list(TRANSFORM QUAKE_CORE_SOURCES PREPEND ${WQ}/)
list(TRANSFORM QUAKE_NULL_DRIVER_SOURCES PREPEND ${WQ}/)
list(TRANSFORM QUAKE_RAYLIB_DRIVER_SOURCES PREPEND ${WQ}/)

function(quake_compile_settings target)
  target_include_directories(${target} PRIVATE ${WQ})

  # Never use the x86 assembly, even on 32-bit x86.
  target_compile_definitions(${target} PRIVATE id386=0)

  if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    # The code relies on signed overflow wrapping and on type punning, and
    # assumes signed plain char (arm has unsigned char).
    target_compile_options(${target} PRIVATE
      -Wall
      -fwrapv -fno-strict-aliasing -fsigned-char
      # Errors by default from GCC 14 on; keep older compilers just as strict.
      -Werror=implicit-function-declaration
      -Werror=implicit-int
      -Werror=int-conversion
      -Werror=incompatible-pointer-types
      -Werror=return-type
    )
  endif()
endfunction()

# The engine core is compiled once and linked into both executables.
add_library(quake_core OBJECT ${QUAKE_CORE_SOURCES})
quake_compile_settings(quake_core)

if(QUAKE_RAYLIB)
  # Use an installed raylib 6 if there is one, otherwise download raylib 6.0
  # and link it statically. -DCMAKE_DISABLE_FIND_PACKAGE_raylib=ON forces the
  # download; FETCHCONTENT_SOURCE_DIR_RAYLIB=DIR builds the source in DIR.
  if(NOT FETCHCONTENT_SOURCE_DIR_RAYLIB)
    find_package(raylib 6.0 QUIET)
  endif()
  if(raylib_FOUND)
    message(STATUS "Using installed raylib ${raylib_VERSION}")
  else()
    include(FetchContent)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
    # The default desktop backend (GLFW), not the experimental native ones.
    set(PLATFORM Desktop CACHE STRING "raylib platform")
    FetchContent_Declare(
      raylib
      GIT_REPOSITORY https://github.com/raysan5/raylib.git
      GIT_TAG 6.0
      GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(raylib)
    message(STATUS "Using raylib 6.0 from ${raylib_SOURCE_DIR}")
  endif()

  add_executable(quake $<TARGET_OBJECTS:quake_core> ${QUAKE_RAYLIB_DRIVER_SOURCES})
  quake_compile_settings(quake)
  target_link_libraries(quake PRIVATE raylib m)

  set(QUAKE_NULL_TARGET quake-null)
else()
  set(QUAKE_NULL_TARGET quake)
endif()

add_executable(${QUAKE_NULL_TARGET} $<TARGET_OBJECTS:quake_core> ${QUAKE_NULL_DRIVER_SOURCES})
quake_compile_settings(${QUAKE_NULL_TARGET})
target_link_libraries(${QUAKE_NULL_TARGET} PRIVATE m)
