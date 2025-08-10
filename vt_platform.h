#pragma once

#include "vt_debug.h"

#define MIN(a, b)		((a) < (b) ? (a) : (b))
#define MAX(a, b)		((a) < (b) ? (b) : (a))
#define LEN(a)			(sizeof(a) / sizeof(a)[0])
#define BETWEEN(x, a, b)	((a) <= (x) && (x) <= (b))
#define DIVCEIL(n, d)		(((n) + ((d) - 1)) / (d))
#define DEFAULT(a, b)		(a) = (a) ? (a) : (b)
#define LIMIT(x, a, b)		(x) = (x) < (a) ? (a) : (x) > (b) ? (b) : (x)

/* SDL3 is that real platform layer for now 
 * But we define our own types in case we might
 * want to use another plaform layer or make our
 * own later. */
#include <SDL3/SDL.h>

#define malloc(s)     SDL_malloc(s)
#define calloc(n, s)  SDL_calloc(n, s)
#define realloc(p, s) SDL_realloc(p, s)
#define free(p)       SDL_free(p)

#define bool _Bool
#define true 1
#define false 0

typedef Uint64 u64; 
typedef Uint32 u32; 
typedef Uint16 u16;
typedef Uint8 u8;

typedef Sint64 i64; 
typedef Sint32 i32; 
typedef Sint16 i16;
typedef Sint8 i8;

typedef float f32;
typedef double f64;

typedef SDL_Window* Platform_Window;
typedef SDL_GLContext Context_OpenGL;

#if defined(__clang__) || defined(__gcc__)
#define STATIC_ASSERT _Static_assert
#else
#define STATIC_ASSERT static_assert
#endif

/* on windows wchar_t is not 32 bits so
 * to avoid confusion codepoint_t is defined */
#pragma GCC poison wchar_t 
typedef u32 codepoint_t;

STATIC_ASSERT(sizeof (u64) == 8, "u64 must be 8 bytes"); 
STATIC_ASSERT(sizeof (u32) == 4, "u32 must be 4 bytes"); 
STATIC_ASSERT(sizeof (u16) == 2, "u16 must be 2 bytes"); 
STATIC_ASSERT(sizeof (u8) == 1, "u8 must be 1 byte"); 

STATIC_ASSERT(sizeof (i64) == 8, "i64 must be 8 bytes"); 
STATIC_ASSERT(sizeof (i32) == 4, "i32 must be 4 bytes"); 
STATIC_ASSERT(sizeof (i16) == 2, "i16 must be 2 bytes"); 
STATIC_ASSERT(sizeof (i8) == 1, "i8 must be 1 byte"); 

STATIC_ASSERT(sizeof (f64) == 8, "f64 must be 8 bytes"); 
STATIC_ASSERT(sizeof (f32) == 4, "f32 must be 4 bytes"); 

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__)
#define PLATFORM_WINDOWS 1
#ifndef _WIN64
#error "64 bit platform is required on windows"
#endif
#elif defined(__linux__) || defined(__gnu_linux__)
#define PLATFORM_LINUX 1
#endif

typedef enum Renderer_Backend { 
  RENDERER_OPENGL3,
  RENDERER_VULKAN
} Renderer_Backend;

typedef struct {
  Platform_Window window;
  Context_OpenGL  context_gl;
  // Context_Vulkan context_vk;
  Renderer_Backend backend;
} Platform_Context;

static Platform_Context context;
static inline bool platform_init(Renderer_Backend type, u32 width, u32 height);
static inline void platform_get_window_size(u32 *width, u32 *height);
static inline void platform_clear_window(u32 color);
static inline void platform_swap_window(void);
static inline void platform_quit(void);
  

static inline bool
platform_init(Renderer_Backend backend, u32 width, u32 height) 
{
  assert(!context.window);

  if (!SDL_Init(SDL_INIT_VIDEO)) {
    VTERROR("SDL_Init");
    return false;
  }

  context.backend = backend;

  if (context.backend == RENDERER_OPENGL3) {
    context.window = SDL_CreateWindow("vt", width, height, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    context.context_gl = SDL_GL_CreateContext(context.window);

    gladLoadGL();

    int major = 3, minor = 3;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, major);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, minor);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

    VTINFO("OpenGL version %d.%d", major, minor);

  } else if (context.backend == RENDERER_VULKAN) {
    SDL_Quit();
    VTERROR("Vukan backend not implemented yet.");
    return false;
  } else {
    VTERROR("Invalid Backend");
    return false;
  }

  return true;
}

static inline void
platform_get_window_size(u32 *width, u32 *height) 
{
  i32 w, h = 0;
  SDL_GetWindowSizeInPixels(context.window, &w, &h);
  *width = MAX(0, w);
  *height = MAX(0, h);
}

static inline void
platform_clear_window(u32 color) 
{
  f32 r = (color >> 24) & 0xFF;
  f32 g = (color >> 16) & 0xFF;
  f32 b = (color >> 8)  & 0xFF;
  if (context.backend == RENDERER_OPENGL3) {
    glClearColor(r, g, b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
  }
}

static inline void
platform_swap_window(void) 
{
  SDL_GL_SwapWindow(context.window);
}

static inline void
platform_quit() 
{
  if (context.backend == RENDERER_OPENGL3) {
    SDL_GL_DestroyContext(context.context_gl);
  }
  if (context.window)
    SDL_DestroyWindow(context.window);
  SDL_Quit();
}
