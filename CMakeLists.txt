cmake_minimum_required(VERSION 3.16)
project(raylib_Quake C)

# Stage 1 builds the WinQuake software renderer with the null video, sound,
# input and CD drivers. The raylib drivers arrive in stage 2.
option(QUAKE_RAYLIB "Build with the raylib video/sound/input drivers" OFF)

if(QUAKE_RAYLIB)
  message(FATAL_ERROR "QUAKE_RAYLIB is not implemented yet (stage 2)")
endif()

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
set(QUAKE_DRIVER_SOURCES
  sys_linux.c
  net_udp.c net_bsd.c
  vid_null.c snd_null.c in_null.c cd_null.c
)

list(TRANSFORM QUAKE_CORE_SOURCES PREPEND ${WQ}/)
list(TRANSFORM QUAKE_DRIVER_SOURCES PREPEND ${WQ}/)

add_executable(quake ${QUAKE_CORE_SOURCES} ${QUAKE_DRIVER_SOURCES})
target_include_directories(quake PRIVATE ${WQ})

# Never use the x86 assembly, even on 32-bit x86.
target_compile_definitions(quake PRIVATE id386=0)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  # The code relies on signed overflow wrapping and on type punning, and
  # assumes signed plain char (arm has unsigned char).
  target_compile_options(quake PRIVATE
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

target_link_libraries(quake PRIVATE m)
