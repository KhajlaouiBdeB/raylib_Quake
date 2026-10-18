# GLQuake on raylib (`quake-gl`)

`quake-gl` is id's GLQuake renderer (`WinQuake/gl_*.c`) running in a raylib
window, next to the software `quake`. Input, sound, CD audio, networking and
the main loop are the same raylib drivers the software build uses; only the
video driver differs.

## Build and run

```sh
cmake -S . -B build -G Ninja      # quake-gl is on by default (-DQUAKE_GL=OFF skips it)
cmake --build build
./build/quake-gl -basedir "$PWD/data" -width 1280 -height 720
```

- `-width`/`-height`: the window. GLQuake renders at the window's real size,
  and the window can be resized afterwards.
- `-conwidth N` (default 640): width of the 2D layer (console, menus, status
  bar). Its height follows the window's shape, so 2D and 3D pixels stay
  square (1920x1080 gives 640x360). `-conheight` fixes it instead.
- `-gamma G` (default 0.7, as GLQuake used on every card but a 3Dfx):
  brightens the palette the textures are built from. `-gamma 1` keeps the
  software renderer's colours.
- Cvars: `vid_vsync` (saved), `vid_fullscreen` / Alt+Enter, plus GLQuake's
  own (`gl_texturemode`, `gl_flashblend`, `gl_polyblend`, `r_shadows`, ...).
- Needs OpenGL 3.3 (desktop) or OpenGL ES 3 / WebGL 2.

## How it works: GL 1.1 on rlgl

GLQuake draws in OpenGL 1.1 immediate mode with fixed-function state.
`gl_raylib.c` implements the subset it calls on top of raylib's **rlgl**
(`gl_raylib.h` replaces `<GL/gl.h>`; the functions are renamed `rgl*` so
they can never bind to a system libGL):

- `glBegin`/`glVertex`/`glEnd` collect vertices; `glEnd` turns polygons,
  quads, fans and strips into triangles and adds them to a batch.
- Any state change first draws the batch with the state it was built under.
  Batches are appended to one stream vertex buffer, orphaned when full and
  every frame.
- One shader (GLSL 330 or GLSL ES 300, chosen at run time) does what the
  fixed-function pipeline did: texture env modulate / replace / untextured,
  the alpha test (`GL_GREATER`), and `glDepthRange` (applied to clip-space z).
- The modelview and projection stacks are kept on the CPU.
- Textures keep GLQuake's own numbers and map to rlgl ids. All formats are
  converted to RGBA8 (GL 3 core and GLES 3 have no luminance, intensity or
  alpha textures); mip levels are generated on the GPU.
- `glDepthFunc`, `glClear` (depth only), `glGetString` and `glBufferData`
  (orphaning) have no rlgl call and come from `rlGetProcAddress`. All four
  are core GLES 3 / WebGL 2.

`gl_raylib.c` includes `rlgl.h` and no Quake header; `raylib.h` stays in
`bridge_raylib.c`, which gains `RL_BeginGLFrame`/`RL_EndGLFrame`.

Decision (QUAK-43): rlgl works for this, with one caveat. Its `rlBegin`
batch can't be used directly, because it has no alpha test, texture env or
depth range, and it applies matrices when the batch is drawn. So the shim
keeps its own batch and shader and uses rlgl for the context, buffers,
shaders, textures and state. Nothing in it needs desktop compatibility-profile
GL, so the same code should build for WebGL 2 (Stage 3, not tried yet).

## What doesn't match GLQuake yet

Tested on Mesa llvmpipe under Xvfb only.

- **Multitexture** (`GL_SGIS_multitexture`): off, so lightmaps are a second
  blended pass, as on cards without it. That's the same picture, but more
  draw calls.
- **8-bit paletted textures** (`GL_EXT_shared_texture_palette`, 3Dfx): not
  offered, so `VID_Is8bit` is false.
- **Fog** (`glFog*`): ignored. GLQuake's fog code is commented out anyway.
- **`gl_ztrick`** defaults to 0 (a depth clear every frame). With 1, the
  alternating depth ranges are emulated but not clipped at the far plane.
- **Points and lines** (`gl_test.c` debug drawing): dropped.
- **`glShadeModel(GL_FLAT)`**: drawn smooth. GLQuake only uses flat shading
  where all vertex colours are equal.
- **Gamma**: texture-only, as in GLQuake. There's no hardware gamma ramp, and
  the software build's `gamma` cvar doesn't apply.
- **Sky, water warp, particles, dynamic lights, 2D/HUD**: these render like
  GLQuake (see the PR screenshots). The status bar and console are scaled
  from the 640-wide 2D layer, not drawn 1:1.
- The FOV is GLQuake's: fixed horizontal, which crops widescreen vertically.
  The software build's Hor+ (QUAK-38) isn't ported.
- Not built for the web yet (Stage 3).
