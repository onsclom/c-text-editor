#include "base/base_inc.c"

#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "fonts/font8x16.h"
#include "fonts/font8x8_basic.h"

#define COLOR_BG 0x1E1E1EFF
#define COLOR_TEXT 0x00FF00FF

#define COLOR_CURSOR COLOR_TEXT
#define COLOR_STATUS_BG COLOR_TEXT
#define COLOR_STATUS_TEXT COLOR_BG

#define TEXT_BUF_SIZE (1024 * 1024)

static char text_buf[TEXT_BUF_SIZE];

static struct {
  SDL_Window *window;
  SDL_Renderer *renderer;
  SDL_Texture *canvas;
  i32 canvas_w;
  i32 canvas_h;
  i32 font;
  i32 scale;
  f32 dpr;
  u64 last_time;
  i32 fps;
  i32 frame_count;

  i32 buf_len;
  i32 cursor;
  i32 scroll_x;
  i32 scroll_y;
  u64 cursor_blink_time;
  i32 cursor_visible;
  const char *filepath;
  u64 save_flash_time;
} state = {.font = 1, .cursor_visible = 1};

static i32 font_height(void) { return state.font == 0 ? 8 : 16; }

static void update_canvas_size(void) {
  i32 pw, ph;
  SDL_GetWindowSizeInPixels(state.window, &pw, &ph);
  state.canvas_w = pw / state.scale;
  state.canvas_h = ph / state.scale;
  if (state.canvas_w < 1)
    state.canvas_w = 1;
  if (state.canvas_h < 1)
    state.canvas_h = 1;
}

static void draw_char_8x8(u32 *pixels, i32 pitch, i32 x, i32 y, char ch,
                           u32 color) {
  if (ch < 0 || ch > 127)
    return;
  for (i32 row = 0; row < 8; row++) {
    if (y + row < 0 || y + row >= state.canvas_h)
      continue;
    u8 bits = (u8)font8x8_basic[(i32)ch][row];
    for (i32 col = 0; col < 8; col++) {
      if (x + col < 0 || x + col >= state.canvas_w)
        continue;
      if (bits & (1 << col))
        pixels[(y + row) * pitch + (x + col)] = color;
    }
  }
}

static void draw_char_8x16(u32 *pixels, i32 pitch, i32 x, i32 y, char ch,
                            u32 color) {
  if (ch < 0 || ch > 127)
    return;
  for (i32 row = 0; row < 16; row++) {
    if (y + row < 0 || y + row >= state.canvas_h)
      continue;
    u8 bits = font8x16[(i32)ch][row];
    for (i32 col = 0; col < 8; col++) {
      if (x + col < 0 || x + col >= state.canvas_w)
        continue;
      if (bits & (0x80 >> col))
        pixels[(y + row) * pitch + (x + col)] = color;
    }
  }
}

static void draw_char(u32 *pixels, i32 pitch, i32 x, i32 y, char ch,
                       u32 color) {
  if (state.font == 0)
    draw_char_8x8(pixels, pitch, x, y, ch, color);
  else
    draw_char_8x16(pixels, pitch, x, y, ch, color);
}

static void draw_string(u32 *pixels, i32 pitch, i32 x, i32 y, const char *str,
                         u32 color) {
  for (i32 i = 0; str[i]; i++)
    draw_char(pixels, pitch, x + i * 8, y, str[i], color);
}

static void draw_rect(u32 *pixels, i32 pitch, i32 x, i32 y, i32 w, i32 h,
                       u32 color) {
  for (i32 row = y; row < y + h; row++) {
    if (row < 0 || row >= state.canvas_h)
      continue;
    for (i32 col = x; col < x + w; col++) {
      if (col < 0 || col >= state.canvas_w)
        continue;
      pixels[row * pitch + col] = color;
    }
  }
}

static i32 line_start(i32 pos) {
  while (pos > 0 && text_buf[pos - 1] != '\n')
    pos--;
  return pos;
}

static i32 line_end(i32 pos) {
  while (pos < state.buf_len && text_buf[pos] != '\n')
    pos++;
  return pos;
}

static void cursor_row_col(i32 *out_row, i32 *out_col) {
  i32 row = 0, col = 0;
  for (i32 i = 0; i < state.cursor; i++) {
    if (text_buf[i] == '\n') {
      row++;
      col = 0;
    } else {
      col++;
    }
  }
  *out_row = row;
  *out_col = col;
}

static i32 pos_from_row_col(i32 target_row, i32 target_col) {
  i32 row = 0, i = 0;
  while (i < state.buf_len && row < target_row) {
    if (text_buf[i] == '\n')
      row++;
    i++;
  }
  i32 ls = i;
  i32 le = line_end(ls);
  i32 line_len = le - ls;
  if (target_col > line_len)
    target_col = line_len;
  return ls + target_col;
}

static void buf_insert(i32 pos, char ch) {
  if (state.buf_len >= TEXT_BUF_SIZE - 1)
    return;
  SDL_memmove(text_buf + pos + 1, text_buf + pos, state.buf_len - pos);
  text_buf[pos] = ch;
  state.buf_len++;
}

static void buf_delete(i32 pos) {
  if (pos < 0 || pos >= state.buf_len)
    return;
  SDL_memmove(text_buf + pos, text_buf + pos + 1, state.buf_len - pos - 1);
  state.buf_len--;
}

static void save_file(void) {
  if (!state.filepath)
    return;
  SDL_IOStream *f = SDL_IOFromFile(state.filepath, "wb");
  if (!f)
    return;
  SDL_WriteIO(f, text_buf, (size_t)state.buf_len);
  SDL_CloseIO(f);
  state.save_flash_time = SDL_GetTicksNS();
}

static void reset_blink(void) {
  state.cursor_blink_time = SDL_GetTicksNS();
  state.cursor_visible = 1;
}

static void ensure_cursor_visible(void) {
  i32 row, col;
  cursor_row_col(&row, &col);
  i32 fh = font_height();
  i32 text_area_h = state.canvas_h - fh - 2;
  i32 visible_lines = text_area_h / fh;
  if (visible_lines < 1)
    visible_lines = 1;
  i32 visible_cols = state.canvas_w / 8;
  if (visible_cols < 1)
    visible_cols = 1;

  if (row < state.scroll_y)
    state.scroll_y = row;
  else if (row >= state.scroll_y + visible_lines)
    state.scroll_y = row - visible_lines + 1;

  if (col < state.scroll_x)
    state.scroll_x = col;
  else if (col >= state.scroll_x + visible_cols)
    state.scroll_x = col - visible_cols + 1;
}

SDL_AppResult SDL_AppInit(void **appstate, i32 argc, char *argv[]) {
  (void)argc;
  (void)argv;
  (void)appstate;

  SDL_SetHint(SDL_HINT_EMSCRIPTEN_KEYBOARD_ELEMENT, "#canvas");

  if (!SDL_Init(SDL_INIT_VIDEO)) {
    SDL_Log("SDL_Init failed: %s", SDL_GetError());
    return SDL_APP_FAILURE;
  }

  state.window = SDL_CreateWindow(
      "ted", 800, 600, SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_RESIZABLE);
  if (!state.window) {
    SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
    return SDL_APP_FAILURE;
  }

  state.dpr = SDL_GetWindowPixelDensity(state.window);
  if (state.dpr < 1.0f)
    state.dpr = 1.0f;
  state.scale = (i32)(state.dpr * 2.0f);

  state.renderer = SDL_CreateRenderer(state.window, NULL);
  if (!state.renderer) {
    SDL_Log("SDL_CreateRenderer failed: %s", SDL_GetError());
    return SDL_APP_FAILURE;
  }

  SDL_DisplayID display = SDL_GetDisplayForWindow(state.window);
  const SDL_DisplayMode *mode = SDL_GetCurrentDisplayMode(display);
  i32 max_w = (i32)(mode->w * state.dpr);
  i32 max_h = (i32)(mode->h * state.dpr);

  state.canvas = SDL_CreateTexture(state.renderer, SDL_PIXELFORMAT_RGBA8888,
                                   SDL_TEXTUREACCESS_STREAMING, max_w, max_h);
  SDL_SetTextureScaleMode(state.canvas, SDL_SCALEMODE_NEAREST);

  state.buf_len = 0;
  state.cursor = 0;

  if (argc > 1) {
    state.filepath = argv[1];
    SDL_IOStream *f = SDL_IOFromFile(state.filepath, "rb");
    if (f) {
      Sint64 size = SDL_GetIOSize(f);
      if (size > 0 && size < TEXT_BUF_SIZE) {
        i32 raw_len = (i32)SDL_ReadIO(f, text_buf, (size_t)size);
        if (raw_len < 0)
          raw_len = 0;
        i32 j = 0;
        for (i32 i = 0; i < raw_len; i++) {
          if (text_buf[i] != '\r')
            text_buf[j++] = text_buf[i];
        }
        state.buf_len = j;
      }
      SDL_CloseIO(f);
    }
  }

  if (state.filepath) {
    char title[256];
    SDL_snprintf(title, sizeof(title), "ted - %s", state.filepath);
    SDL_SetWindowTitle(state.window, title);
  }

  SDL_StartTextInput(state.window);

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

  if ((now - state.cursor_blink_time) / 1000000ULL > 530) {
    state.cursor_visible = !state.cursor_visible;
    state.cursor_blink_time = now;
  }

  void *pixel_ptr;
  i32 pitch_bytes;
  SDL_Rect canvas_rect = {0, 0, state.canvas_w, state.canvas_h};
  if (!SDL_LockTexture(state.canvas, &canvas_rect, &pixel_ptr, &pitch_bytes))
    return;

  u32 *pixels = (u32 *)pixel_ptr;
  i32 pitch = pitch_bytes / 4;

  for (i32 i = 0; i < pitch * state.canvas_h; i++)
    pixels[i] = COLOR_BG;

  i32 fh = font_height();
  i32 text_area_h = state.canvas_h - fh - 2;

  i32 cur_row, cur_col;
  cursor_row_col(&cur_row, &cur_col);

  i32 row = 0, col = 0;
  for (i32 i = 0; i <= state.buf_len; i++) {
    i32 screen_row = row - state.scroll_y;
    i32 screen_col = col - state.scroll_x;
    i32 py = screen_row * fh;
    i32 px = screen_col * 8;
    i32 visible = (py >= 0 && py + fh <= text_area_h && px >= 0 &&
                   px + 8 <= state.canvas_w);

    if (i == state.cursor && state.cursor_visible && visible) {
      draw_rect(pixels, pitch, px, py, 8, fh, COLOR_CURSOR);
      char under =
          (i < state.buf_len && text_buf[i] != '\n') ? text_buf[i] : ' ';
      if (under != ' ')
        draw_char(pixels, pitch, px, py, under, COLOR_BG);
    }

    if (i < state.buf_len) {
      char ch = text_buf[i];
      if (ch == '\n') {
        row++;
        col = 0;
      } else {
        if (i != state.cursor || !state.cursor_visible) {
          if (visible)
            draw_char(pixels, pitch, px, py, ch, COLOR_TEXT);
        }
        col++;
      }
    }
  }

  i32 status_y = state.canvas_h - fh - 1;
  draw_rect(pixels, pitch, 0, status_y, state.canvas_w, fh + 1,
            COLOR_STATUS_BG);

  i32 show_saved = state.save_flash_time &&
                   (now - state.save_flash_time) / 1000000ULL < 2000;

  char status[256];
  if (show_saved) {
    SDL_snprintf(status, sizeof(status), "%d:%d  saved %s", cur_row + 1,
                 cur_col + 1, state.filepath ? state.filepath : "");
  } else {
    SDL_snprintf(status, sizeof(status), "%d:%d  (F1) %dx  (F2) %s%s%s",
                 cur_row + 1, cur_col + 1, state.scale,
                 state.font == 0 ? "8x8" : "8x16", state.filepath ? "  " : "",
                 state.filepath ? state.filepath : "");
  }
  draw_string(pixels, pitch, 0, status_y + 1, status, COLOR_STATUS_TEXT);

  {
    char fps_buf[16];
    SDL_snprintf(fps_buf, sizeof(fps_buf), "%d fps", state.fps);
    i32 len = (i32)SDL_strlen(fps_buf);
    draw_string(pixels, pitch, state.canvas_w - len * 8, status_y + 1, fps_buf,
                COLOR_STATUS_TEXT);
  }

  SDL_UnlockTexture(state.canvas);

  SDL_FRect src = {0, 0, (f32)state.canvas_w, (f32)state.canvas_h};
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

  if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
      event->button.button == SDL_BUTTON_LEFT) {
    i32 fh = font_height();
    i32 px = (i32)(event->button.x * state.dpr) / state.scale;
    i32 py = (i32)(event->button.y * state.dpr) / state.scale;
    i32 click_row = py / fh + state.scroll_y;
    i32 click_col = px / 8 + state.scroll_x;
    if (click_col < 0)
      click_col = 0;
    state.cursor = pos_from_row_col(click_row, click_col);
    reset_blink();
  }

  if (event->type == SDL_EVENT_TEXT_INPUT) {
    const char *text = event->text.text;
    for (i32 i = 0; text[i]; i++) {
      char ch = text[i];
      if (ch >= 32 && ch < 127) {
        buf_insert(state.cursor, ch);
        state.cursor++;
      }
    }
    reset_blink();
    ensure_cursor_visible();
  }

  if (event->type == SDL_EVENT_KEY_DOWN) {
    i32 row, col;
    cursor_row_col(&row, &col);

    switch (event->key.key) {
    case SDLK_ESCAPE:
      return SDL_APP_SUCCESS;

    case SDLK_S:
      if (event->key.mod & (SDL_KMOD_GUI | SDL_KMOD_CTRL)) {
        save_file();
      }
      break;

    case SDLK_F1:
      state.scale = (state.scale % 8) + 1;
      update_canvas_size();
      break;

    case SDLK_F2:
      state.font = 1 - state.font;
      break;

    case SDLK_RETURN:
      buf_insert(state.cursor, '\n');
      state.cursor++;
      reset_blink();
      ensure_cursor_visible();
      break;

    case SDLK_BACKSPACE:
      if (state.cursor > 0) {
        state.cursor--;
        buf_delete(state.cursor);
        reset_blink();
        ensure_cursor_visible();
      }
      break;

    case SDLK_DELETE:
      if (state.cursor < state.buf_len) {
        buf_delete(state.cursor);
        reset_blink();
      }
      break;

    case SDLK_LEFT:
      if (state.cursor > 0) {
        state.cursor--;
        reset_blink();
        ensure_cursor_visible();
      }
      break;

    case SDLK_RIGHT:
      if (state.cursor < state.buf_len) {
        state.cursor++;
        reset_blink();
        ensure_cursor_visible();
      }
      break;

    case SDLK_UP:
      if (row > 0) {
        state.cursor = pos_from_row_col(row - 1, col);
        reset_blink();
        ensure_cursor_visible();
      }
      break;

    case SDLK_DOWN: {
      i32 ls = line_end(state.cursor);
      if (ls < state.buf_len) {
        state.cursor = pos_from_row_col(row + 1, col);
        reset_blink();
        ensure_cursor_visible();
      }
      break;
    }

    case SDLK_HOME:
      state.cursor = line_start(state.cursor);
      reset_blink();
      ensure_cursor_visible();
      break;

    case SDLK_END:
      state.cursor = line_end(state.cursor);
      reset_blink();
      ensure_cursor_visible();
      break;

    default:
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
