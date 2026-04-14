#include "base/base_inc.c"
#include "base/base_inc.h"

#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "fonts/font8x16.h"
#include "fonts/font8x8_basic.h"

#define COLOR_BG 0x1E1E1EFF
#define COLOR_TEXT 0xFFFFFFFF

static struct {
  SDL_Window *window;
  SDL_Renderer *renderer;
  SDL_Texture *canvas;
  int canvas_w;
  int canvas_h;
  int font;
  int scale;
  float dpr;
  u64 last_time;
  int fps;
  int frame_count;
  int show_fps;
} state = {.font = 0, .scale = 3};

static void update_canvas_size(void) {
  int pw, ph;
  SDL_GetWindowSizeInPixels(state.window, &pw, &ph);
  state.canvas_w = pw / state.scale;
  state.canvas_h = ph / state.scale;
  if (state.canvas_w < 1)
    state.canvas_w = 1;
  if (state.canvas_h < 1)
    state.canvas_h = 1;
}

static void draw_char_8x8(u32 *pixels, int pitch, int x, int y, char ch,
                          u32 color) {
  if (ch < 0 || ch > 127)
    return;
  for (int row = 0; row < 8; row++) {
    if (y + row < 0 || y + row >= state.canvas_h)
      continue;
    unsigned char bits = (unsigned char)font8x8_basic[(int)ch][row];
    for (int col = 0; col < 8; col++) {
      if (x + col < 0 || x + col >= state.canvas_w)
        continue;
      if (bits & (1 << col))
        pixels[(y + row) * pitch + (x + col)] = color;
    }
  }
}

static void draw_char_8x16(u32 *pixels, int pitch, int x, int y, char ch,
                           u32 color) {
  if (ch < 0 || ch > 127)
    return;
  for (int row = 0; row < 16; row++) {
    if (y + row < 0 || y + row >= state.canvas_h)
      continue;
    unsigned char bits = font8x16[(int)ch][row];
    for (int col = 0; col < 8; col++) {
      if (x + col < 0 || x + col >= state.canvas_w)
        continue;
      if (bits & (0x80 >> col))
        pixels[(y + row) * pitch + (x + col)] = color;
    }
  }
}

static void draw_string(u32 *pixels, int pitch, int x, int y, const char *str,
                        u32 color, int font) {
  for (int i = 0; str[i]; i++) {
    int cx = x + i * 8;
    if (font == 0)
      draw_char_8x8(pixels, pitch, cx, y, str[i], color);
    else
      draw_char_8x16(pixels, pitch, cx, y, str[i], color);
  }
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  (void)appstate;

  SDL_SetHint(SDL_HINT_EMSCRIPTEN_KEYBOARD_ELEMENT, "#canvas");

  if (!SDL_Init(SDL_INIT_VIDEO)) {
    SDL_Log("SDL_Init failed: %s", SDL_GetError());
    return SDL_APP_FAILURE;
  }

  state.window = SDL_CreateWindow(
      "ted", 400, 300, SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_RESIZABLE);
  if (!state.window) {
    SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
    return SDL_APP_FAILURE;
  }

  state.dpr = SDL_GetWindowPixelDensity(state.window);
  if (state.dpr < 1.0f)
    state.dpr = 1.0f;

  state.renderer = SDL_CreateRenderer(state.window, NULL);
  if (!state.renderer) {
    SDL_Log("SDL_CreateRenderer failed: %s", SDL_GetError());
    return SDL_APP_FAILURE;
  }

  SDL_DisplayID display = SDL_GetDisplayForWindow(state.window);
  const SDL_DisplayMode *mode = SDL_GetCurrentDisplayMode(display);
  int max_w = (int)(mode->w * state.dpr);
  int max_h = (int)(mode->h * state.dpr);

  state.canvas = SDL_CreateTexture(state.renderer, SDL_PIXELFORMAT_RGBA8888,
                                   SDL_TEXTUREACCESS_STREAMING, max_w, max_h);
  SDL_SetTextureScaleMode(state.canvas, SDL_SCALEMODE_NEAREST);

  update_canvas_size();

  return SDL_APP_CONTINUE;
}

static void render(void) {
  state.frame_count++;
  u64 now = SDL_GetTicksNS();
  if (now - state.last_time >= 1000000000ULL) {
    state.fps = state.frame_count;
    state.frame_count = 0;
    state.last_time = now;
  }

  void *pixel_ptr;
  int pitch_bytes;
  SDL_Rect canvas_rect = {0, 0, state.canvas_w, state.canvas_h};
  if (!SDL_LockTexture(state.canvas, &canvas_rect, &pixel_ptr, &pitch_bytes))
    return;

  u32 *pixels = (u32 *)pixel_ptr;
  int pitch = pitch_bytes / 4;

  for (int i = 0; i < pitch * state.canvas_h; i++)
    pixels[i] = COLOR_BG;

  draw_string(pixels, pitch, 10, 10, "Hello, world!", COLOR_TEXT, state.font);

  const char *font_label = (state.font == 0) ? "[1] 8x8" : "[2] 8x16";
  draw_string(pixels, pitch, 10, 40, font_label, COLOR_TEXT, state.font);

  char scale_label[32];
  SDL_snprintf(scale_label, sizeof(scale_label), "scale: %dx", state.scale);
  draw_string(pixels, pitch, 10, 60, scale_label, COLOR_TEXT, state.font);

  draw_string(pixels, pitch, 10, 90, "[ ] scale  1/2 font  ESC quit",
              COLOR_TEXT, state.font);

  if (state.show_fps) {
    char fps_buf[16];
    SDL_snprintf(fps_buf, sizeof(fps_buf), "%d fps", state.fps);
    int len = (int)SDL_strlen(fps_buf);
    draw_string(pixels, pitch, state.canvas_w - len * 8 - 4, 4, fps_buf,
                COLOR_TEXT, state.font);
  }

  SDL_UnlockTexture(state.canvas);

  SDL_FRect src = {0, 0, (float)state.canvas_w, (float)state.canvas_h};
  SDL_SetRenderDrawColor(state.renderer, 30, 30, 30, 255);
  SDL_RenderClear(state.renderer);
  SDL_RenderTexture(state.renderer, state.canvas, &src, NULL);
  SDL_RenderPresent(state.renderer);
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
  (void)appstate;
  if (event->type == SDL_EVENT_QUIT)
    return SDL_APP_SUCCESS;

  if (event->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
    update_canvas_size();

  if (event->type == SDL_EVENT_KEY_DOWN) {
    switch (event->key.key) {
    case SDLK_ESCAPE:
      return SDL_APP_SUCCESS;
    case SDLK_1:
      state.font = 0;
      break;
    case SDLK_2:
      state.font = 1;
      break;
    case SDLK_LEFTBRACKET:
      if (state.scale > 1) {
        state.scale--;
        update_canvas_size();
      }
      break;
    case SDLK_RIGHTBRACKET:
      if (state.scale < 8) {
        state.scale++;
        update_canvas_size();
      }
      break;
    case SDLK_SLASH:
      if (event->key.mod & SDL_KMOD_SHIFT)
        state.show_fps = !state.show_fps;
      break;
    }
  }

  return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate) {
  (void)appstate;
  render();

  return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {
  (void)appstate;
  (void)result;
  SDL_DestroyTexture(state.canvas);
  SDL_DestroyRenderer(state.renderer);
  SDL_DestroyWindow(state.window);
}
