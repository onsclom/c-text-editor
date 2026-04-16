#include "base/base_inc.c"
#include "tokenize.c"

#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "fonts/font8x16.h"
#include "fonts/font8x8_basic.h"
#include "fonts/font_ibmvga8.h"
#include "fonts/font_pixelcode.h"

#define COLOR_BG 0x1C1A2EFF

#define SYN_TEXT 0xE8D5C5FF
#define SYN_KEYWORD 0xE86A9DFF
#define SYN_TYPE 0xF0A868FF
#define SYN_STRING 0xF5B091FF
#define SYN_NUMBER 0xEBC27AFF
#define SYN_COMMENT 0x6B5C7FFF
#define SYN_PREPROC 0x9E8F8FFF
#define SYN_CONST 0xE87662FF
#define SYN_PUNCT 0x8A7682FF
#define SYN_OPERATOR 0xE89050FF

#define COLOR_CURSOR SYN_TEXT
#define COLOR_STATUS_BG SYN_OPERATOR
#define COLOR_STATUS_TEXT COLOR_BG

static u32 color_for_token_type(TokenType t) {
  switch (t) {
  case TOKEN_COMMENT_LINE:
  case TOKEN_COMMENT_BLOCK:
    return SYN_COMMENT;
  case TOKEN_PREPROCESSOR:
    return SYN_PREPROC;
  case TOKEN_STRING_LITERAL:
  case TOKEN_CHAR_LITERAL:
    return SYN_STRING;
  case TOKEN_INT_LITERAL:
  case TOKEN_FLOAT_LITERAL:
    return SYN_NUMBER;
  case TOKEN_CONST_TRUE:
  case TOKEN_CONST_FALSE:
  case TOKEN_CONST_NULL:
    return SYN_CONST;
  case TOKEN_TYPE_INT:
  case TOKEN_TYPE_FLOAT:
  case TOKEN_TYPE_CHAR:
  case TOKEN_TYPE_VOID:
  case TOKEN_TYPE_SHORT:
  case TOKEN_TYPE_LONG:
  case TOKEN_TYPE_SIGNED:
  case TOKEN_TYPE_UNSIGNED:
  case TOKEN_TYPE_DOUBLE:
  case TOKEN_TYPE_BOOL:
  case TOKEN_TYPE_BUILTIN:
    return SYN_TYPE;
  case TOKEN_KEYWORD_RETURN:
  case TOKEN_KEYWORD_IF:
  case TOKEN_KEYWORD_ELSE:
  case TOKEN_KEYWORD_WHILE:
  case TOKEN_KEYWORD_FOR:
  case TOKEN_KEYWORD_DO:
  case TOKEN_KEYWORD_SWITCH:
  case TOKEN_KEYWORD_CASE:
  case TOKEN_KEYWORD_DEFAULT:
  case TOKEN_KEYWORD_BREAK:
  case TOKEN_KEYWORD_CONTINUE:
  case TOKEN_KEYWORD_GOTO:
  case TOKEN_KEYWORD_SIZEOF:
  case TOKEN_KEYWORD_STATIC:
  case TOKEN_KEYWORD_CONST:
  case TOKEN_KEYWORD_EXTERN:
  case TOKEN_KEYWORD_TYPEDEF:
  case TOKEN_KEYWORD_STRUCT:
  case TOKEN_KEYWORD_ENUM:
  case TOKEN_KEYWORD_UNION:
  case TOKEN_KEYWORD_VOLATILE:
  case TOKEN_KEYWORD_INLINE:
  case TOKEN_KEYWORD_REGISTER:
    return SYN_KEYWORD;
  case TOKEN_PLUS:
  case TOKEN_PLUS_PLUS:
  case TOKEN_MINUS:
  case TOKEN_MINUS_MINUS:
  case TOKEN_STAR:
  case TOKEN_DIVIDE:
  case TOKEN_PERCENT:
  case TOKEN_EQ:
  case TOKEN_EQ_EQ:
  case TOKEN_BANG:
  case TOKEN_BANG_EQ:
  case TOKEN_LT:
  case TOKEN_LT_EQ:
  case TOKEN_GT:
  case TOKEN_GT_EQ:
  case TOKEN_AND:
  case TOKEN_AND_AND:
  case TOKEN_OR:
  case TOKEN_OR_OR:
  case TOKEN_CARET:
  case TOKEN_TILDE:
  case TOKEN_QUESTION:
  case TOKEN_ARROW:
    return SYN_OPERATOR;
  case TOKEN_LPAREN:
  case TOKEN_RPAREN:
  case TOKEN_LBRACE:
  case TOKEN_RBRACE:
  case TOKEN_LBRACKET:
  case TOKEN_RBRACKET:
  case TOKEN_COMMA:
  case TOKEN_SEMICOLON:
  case TOKEN_COLON:
  case TOKEN_DOT:
    return SYN_PUNCT;
  default:
    return SYN_TEXT;
  }
}

#define TEXT_BUFFER_SIZE (1024 * 1024)
#define CURSOR_BLINK_MS 500
#define CURSOR_ANIM_DECAY 40.0f
#define SCROLL_SPEED 16

static char text_buffer[TEXT_BUFFER_SIZE];

typedef enum { MODE_NORMAL, MODE_INSERT } EditorMode;

typedef struct {
  const u8 *data;
  i32 glyph_stride;
  i32 width;
  i32 height;
  bool msb_first;
  const char *name;
} Font;

static const Font fonts[] = {
    {(const u8 *)font8x8_basic, 8, 8, 8, false, "8x8"},
    {(const u8 *)font8x16, 16, 8, 16, true, "8x16"},
    {(const u8 *)font_pixelcode, PIXELCODE_FONT_HEIGHT, PIXELCODE_FONT_WIDTH,
     PIXELCODE_FONT_HEIGHT, false, "6x9"},
    {(const u8 *)font_ibmvga8, IBMVGA8_FONT_HEIGHT, IBMVGA8_FONT_WIDTH,
     IBMVGA8_FONT_HEIGHT, false, "VGA8"},
};

#define FONT_COUNT ((i32)(sizeof(fonts) / sizeof(fonts[0])))

static struct {
  SDL_Window *window;
  SDL_Renderer *renderer;
  SDL_Texture *canvas;
  i32 canvas_width;
  i32 canvas_height;
  i32 font_index;
  i32 scale;
  f32 pixel_density;
  u64 last_time;
  i32 fps;
  i32 frame_count;

  i32 buffer_length;
  i32 cursor;
  f32 scroll_x;
  f32 scroll_y;
  f32 cursor_display_x;
  f32 cursor_display_y;
  u64 last_frame_time;
  u64 cursor_blink_time;
  bool cursor_visible;
  const char *filepath;
  u64 save_flash_time;

  struct {
    EditorMode mode;
    char pending;
    char cmd_buf[64];
    i32 cmd_len;
    bool in_command;
  } vim;
} state = {.font_index = 2, .cursor_visible = true};

#define TOKEN_ARENA_SIZE (8 * 1024 * 1024)
static u8 token_arena_buffer[TOKEN_ARENA_SIZE];
static Arena token_arena;
static Token *syntax_tokens;
static size_t syntax_token_count;
static bool tokens_dirty = true;

static void retokenize_if_dirty(void) {
  if (!tokens_dirty)
    return;
  tokens_dirty = false;
  token_arena.base = token_arena_buffer;
  token_arena.cap = TOKEN_ARENA_SIZE;
  arena_reset(&token_arena);
  if ((size_t)state.buffer_length * sizeof(Token) + 64 > TOKEN_ARENA_SIZE) {
    syntax_tokens = NULL;
    syntax_token_count = 0;
    return;
  }
  s8 src = {.data = (u8 *)text_buffer, .length = (size_t)state.buffer_length};
  TokenizeResult r = tokenize(&token_arena, src);
  syntax_tokens = r.tokens;
  syntax_token_count = r.count;
}

static const Font *current_font(void) { return &fonts[state.font_index]; }

static void update_canvas_size(void) {
  i32 pixel_width, pixel_height;
  SDL_GetWindowSizeInPixels(state.window, &pixel_width, &pixel_height);
  state.canvas_width = pixel_width / state.scale;
  state.canvas_height = pixel_height / state.scale;
  if (state.canvas_width < 1)
    state.canvas_width = 1;
  if (state.canvas_height < 1)
    state.canvas_height = 1;
}

static void draw_char(u32 *pixels, i32 pitch, i32 x, i32 y, u8 character,
                      u32 color) {
  if (character > 127)
    return;
  const Font *font = current_font();
  const u8 *glyph = font->data + character * font->glyph_stride;
  for (i32 row = 0; row < font->height; row++) {
    if (y + row < 0 || y + row >= state.canvas_height)
      continue;
    u8 bits = glyph[row];
    for (i32 col = 0; col < font->width; col++) {
      if (x + col < 0 || x + col >= state.canvas_width)
        continue;
      bool lit = font->msb_first ? (bits & (0x80 >> col)) : (bits & (1 << col));
      if (lit)
        pixels[(y + row) * pitch + (x + col)] = color;
    }
  }
}

static void draw_string(u32 *pixels, i32 pitch, i32 x, i32 y, const char *str,
                        u32 color) {
  i32 char_width = current_font()->width;
  for (i32 i = 0; str[i]; i++)
    draw_char(pixels, pitch, x + i * char_width, y, (u8)str[i], color);
}

static void draw_rect(u32 *pixels, i32 pitch, i32 x, i32 y, i32 width,
                      i32 height, u32 color) {
  for (i32 row = y; row < y + height; row++) {
    if (row < 0 || row >= state.canvas_height)
      continue;
    for (i32 col = x; col < x + width; col++) {
      if (col < 0 || col >= state.canvas_width)
        continue;
      pixels[row * pitch + col] = color;
    }
  }
}

static i32 line_start(i32 position) {
  while (position > 0 && text_buffer[position - 1] != '\n')
    position--;
  return position;
}

static i32 line_end(i32 position) {
  while (position < state.buffer_length && text_buffer[position] != '\n')
    position++;
  return position;
}

static void cursor_row_col(i32 *out_row, i32 *out_col) {
  i32 row = 0, col = 0;
  for (i32 i = 0; i < state.cursor; i++) {
    if (text_buffer[i] == '\n') {
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
  while (i < state.buffer_length && row < target_row) {
    if (text_buffer[i] == '\n')
      row++;
    i++;
  }
  i32 start = i;
  i32 end = line_end(start);
  i32 length = end - start;
  if (target_col > length)
    target_col = length;
  return start + target_col;
}

static void buffer_insert(i32 position, char character) {
  if (state.buffer_length >= TEXT_BUFFER_SIZE - 1)
    return;
  SDL_memmove(text_buffer + position + 1, text_buffer + position,
              state.buffer_length - position);
  text_buffer[position] = character;
  state.buffer_length++;
  tokens_dirty = true;
}

static void buffer_delete(i32 position) {
  if (position < 0 || position >= state.buffer_length)
    return;
  SDL_memmove(text_buffer + position, text_buffer + position + 1,
              state.buffer_length - position - 1);
  state.buffer_length--;
  tokens_dirty = true;
}

static void ensure_cursor_visible(void);

static void reset_blink(void) {
  state.cursor_blink_time = SDL_GetTicksNS();
  state.cursor_visible = true;
  ensure_cursor_visible();
}

static void save_file(void) {
  if (!state.filepath)
    return;
  SDL_IOStream *file = SDL_IOFromFile(state.filepath, "wb");
  if (!file)
    return;
  SDL_WriteIO(file, text_buffer, (size_t)state.buffer_length);
  SDL_CloseIO(file);
  state.save_flash_time = SDL_GetTicksNS();
}

static bool is_word_char(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == '_';
}

static bool is_whitespace(char c) { return c == ' ' || c == '\t' || c == '\n'; }

static i32 word_forward(i32 pos) {
  if (pos >= state.buffer_length)
    return pos;
  char c = text_buffer[pos];
  if (is_word_char(c)) {
    while (pos < state.buffer_length && is_word_char(text_buffer[pos]))
      pos++;
  } else if (!is_whitespace(c)) {
    while (pos < state.buffer_length && !is_word_char(text_buffer[pos]) &&
           !is_whitespace(text_buffer[pos]))
      pos++;
  }
  while (pos < state.buffer_length && is_whitespace(text_buffer[pos]))
    pos++;
  return pos;
}

static i32 word_backward(i32 pos) {
  if (pos <= 0)
    return 0;
  pos--;
  while (pos > 0 && text_buffer[pos] == ' ')
    pos--;
  if (is_word_char(text_buffer[pos])) {
    while (pos > 0 && is_word_char(text_buffer[pos - 1]))
      pos--;
  } else {
    while (pos > 0 && !is_word_char(text_buffer[pos - 1]) &&
           text_buffer[pos - 1] != '\n')
      pos--;
  }
  return pos;
}

static i32 word_end(i32 pos) {
  if (pos >= state.buffer_length - 1)
    return state.buffer_length > 0 ? state.buffer_length - 1 : 0;
  pos++;
  while (pos < state.buffer_length && text_buffer[pos] == ' ')
    pos++;
  if (pos < state.buffer_length && is_word_char(text_buffer[pos])) {
    while (pos < state.buffer_length - 1 && is_word_char(text_buffer[pos + 1]))
      pos++;
  } else {
    while (pos < state.buffer_length - 1 &&
           !is_word_char(text_buffer[pos + 1]) && text_buffer[pos + 1] != '\n')
      pos++;
  }
  return pos;
}

static i32 total_lines(void) {
  i32 count = 1;
  for (i32 i = 0; i < state.buffer_length; i++)
    if (text_buffer[i] == '\n')
      count++;
  return count;
}

static void delete_line(void) {
  i32 start = line_start(state.cursor);
  i32 end = line_end(state.cursor);
  if (end < state.buffer_length)
    end++;
  i32 count = end - start;
  SDL_memmove(text_buffer + start, text_buffer + end,
              state.buffer_length - end);
  state.buffer_length -= count;
  state.cursor = start;
  tokens_dirty = true;
  if (state.cursor > 0 && state.cursor >= state.buffer_length &&
      state.buffer_length > 0)
    state.cursor = state.buffer_length - 1;
  i32 le = line_end(state.cursor);
  if (state.cursor < le) { /* already on a char */
  } else
    state.cursor = line_start(state.cursor);
}

static void clamp_cursor_to_line(void) {
  i32 end = line_end(state.cursor);
  i32 start = line_start(state.cursor);
  if (end > start && state.cursor >= end)
    state.cursor = end - 1;
}

static void handle_normal_char(char c) {
  i32 row, col;
  cursor_row_col(&row, &col);

  if (state.vim.pending == 'd') {
    state.vim.pending = 0;
    if (c == 'd')
      delete_line();
    return;
  }
  if (state.vim.pending == 'g') {
    state.vim.pending = 0;
    if (c == 'g')
      state.cursor = 0;
    return;
  }

  state.vim.pending = 0;

  switch (c) {
  case 'h':
    if (state.cursor > 0) {
      i32 start = line_start(state.cursor);
      if (state.cursor > start)
        state.cursor--;
    }
    break;
  case 'l': {
    i32 end = line_end(state.cursor);
    if (state.cursor < end - 1)
      state.cursor++;
    break;
  }
  case 'j':
    if (line_end(state.cursor) < state.buffer_length) {
      state.cursor = pos_from_row_col(row + 1, col);
      clamp_cursor_to_line();
    }
    break;
  case 'k':
    if (row > 0) {
      state.cursor = pos_from_row_col(row - 1, col);
      clamp_cursor_to_line();
    }
    break;
  case 'w':
    state.cursor = word_forward(state.cursor);
    break;
  case 'b':
    state.cursor = word_backward(state.cursor);
    break;
  case 'e':
    state.cursor = word_end(state.cursor);
    break;
  case '0':
    state.cursor = line_start(state.cursor);
    break;
  case '$': {
    i32 end = line_end(state.cursor);
    if (end > line_start(state.cursor))
      state.cursor = end - 1;
    break;
  }
  case 'x':
    if (state.cursor < state.buffer_length &&
        text_buffer[state.cursor] != '\n') {
      buffer_delete(state.cursor);
      clamp_cursor_to_line();
    }
    break;
  case 'd':
    state.vim.pending = 'd';
    return;
  case 'g':
    state.vim.pending = 'g';
    return;
  case 'G':
    state.cursor = pos_from_row_col(total_lines() - 1, 0);
    break;
  case 'J': {
    i32 end = line_end(state.cursor);
    if (end < state.buffer_length) {
      text_buffer[end] = ' ';
      tokens_dirty = true;
    }
    break;
  }
  case 'i':
    state.vim.mode = MODE_INSERT;
    break;
  case 'a':
    if (state.cursor < line_end(state.cursor))
      state.cursor++;
    state.vim.mode = MODE_INSERT;
    break;
  case 'I':
    state.cursor = line_start(state.cursor);
    state.vim.mode = MODE_INSERT;
    break;
  case 'A':
    state.cursor = line_end(state.cursor);
    state.vim.mode = MODE_INSERT;
    break;
  case 'o':
    state.cursor = line_end(state.cursor);
    buffer_insert(state.cursor, '\n');
    state.cursor++;
    state.vim.mode = MODE_INSERT;
    break;
  case 'O':
    state.cursor = line_start(state.cursor);
    buffer_insert(state.cursor, '\n');
    state.vim.mode = MODE_INSERT;
    break;
  case ':':
    state.vim.in_command = true;
    state.vim.cmd_len = 0;
    break;
  default:
    break;
  }

  reset_blink();
}

static SDL_AppResult handle_command(void) {
  state.vim.cmd_buf[state.vim.cmd_len] = '\0';
  state.vim.in_command = false;

  if (SDL_strcmp(state.vim.cmd_buf, "w") == 0) {
    save_file();
  } else if (SDL_strcmp(state.vim.cmd_buf, "q") == 0) {
    return SDL_APP_SUCCESS;
  } else if (SDL_strcmp(state.vim.cmd_buf, "wq") == 0) {
    save_file();
    return SDL_APP_SUCCESS;
  }
  return SDL_APP_CONTINUE;
}

static void ensure_cursor_visible(void) {
  i32 row, col;
  cursor_row_col(&row, &col);
  const Font *font = current_font();
  i32 text_area_height = state.canvas_height - font->height - 2;
  f32 cursor_py = (f32)(row * font->height);
  f32 cursor_px = (f32)(col * font->width);

  if (cursor_py < state.scroll_y)
    state.scroll_y = cursor_py;
  else if (cursor_py + font->height > state.scroll_y + text_area_height)
    state.scroll_y = cursor_py + font->height - text_area_height;

  if (cursor_px < state.scroll_x)
    state.scroll_x = cursor_px;
  else if (cursor_px + font->width > state.scroll_x + state.canvas_width)
    state.scroll_x = cursor_px + font->width - state.canvas_width;
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

  state.pixel_density = SDL_GetWindowPixelDensity(state.window);
  if (state.pixel_density < 1.0f)
    state.pixel_density = 1.0f;
  state.scale = (i32)(state.pixel_density * 2.0f);

  state.renderer = SDL_CreateRenderer(state.window, NULL);
  if (!state.renderer) {
    SDL_Log("SDL_CreateRenderer failed: %s", SDL_GetError());
    return SDL_APP_FAILURE;
  }

  SDL_DisplayID display = SDL_GetDisplayForWindow(state.window);
  const SDL_DisplayMode *mode = SDL_GetCurrentDisplayMode(display);
  i32 max_width = (i32)(mode->w * state.pixel_density);
  i32 max_height = (i32)(mode->h * state.pixel_density);

  state.canvas =
      SDL_CreateTexture(state.renderer, SDL_PIXELFORMAT_RGBA8888,
                        SDL_TEXTUREACCESS_STREAMING, max_width, max_height);
  SDL_SetTextureScaleMode(state.canvas, SDL_SCALEMODE_NEAREST);

  state.buffer_length = 0;
  state.cursor = 0;

  if (argc > 1) {
    state.filepath = argv[1];
    SDL_IOStream *file = SDL_IOFromFile(state.filepath, "rb");
    if (file) {
      Sint64 size = SDL_GetIOSize(file);
      if (size > 0 && size < TEXT_BUFFER_SIZE) {
        i32 raw_length = (i32)SDL_ReadIO(file, text_buffer, (size_t)size);
        if (raw_length < 0)
          raw_length = 0;
        i32 write_pos = 0;
        for (i32 i = 0; i < raw_length; i++) {
          if (text_buffer[i] != '\r')
            text_buffer[write_pos++] = text_buffer[i];
        }
        state.buffer_length = write_pos;
      }
      SDL_CloseIO(file);
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
  retokenize_if_dirty();

  state.frame_count++;
  u64 now = SDL_GetTicksNS();

  f32 dt = 0.0f;
  if (state.last_frame_time != 0) {
    dt = (f32)(now - state.last_frame_time) / 1.0e9f;
    if (dt > 0.1f)
      dt = 0.1f;
  }
  state.last_frame_time = now;

  if (now - state.last_time >= 1000000000ULL) {
    state.fps = state.frame_count;
    state.frame_count = 0;
    state.last_time = now;
  }

  if ((now - state.cursor_blink_time) / 1000000ULL > CURSOR_BLINK_MS) {
    state.cursor_visible = !state.cursor_visible;
    state.cursor_blink_time = now;
  }

  void *pixel_ptr;
  i32 pitch_bytes;
  SDL_Rect canvas_rect = {0, 0, state.canvas_width, state.canvas_height};
  if (!SDL_LockTexture(state.canvas, &canvas_rect, &pixel_ptr, &pitch_bytes))
    return;

  u32 *pixels = (u32 *)pixel_ptr;
  i32 pitch = pitch_bytes / 4;

  for (i32 i = 0; i < pitch * state.canvas_height; i++)
    pixels[i] = COLOR_BG;

  const Font *font = current_font();
  i32 text_area_height = state.canvas_height - font->height - 2;

  i32 cursor_row, cursor_col;
  cursor_row_col(&cursor_row, &cursor_col);

  f32 target_x = (f32)(cursor_col * font->width);
  f32 target_y = (f32)(cursor_row * font->height);
  f32 factor = 1.0f - SDL_expf(-CURSOR_ANIM_DECAY * dt);
  state.cursor_display_x += (target_x - state.cursor_display_x) * factor;
  state.cursor_display_y += (target_y - state.cursor_display_y) * factor;

  i32 row = 0, col = 0;
  size_t tok_idx = 0;
  for (i32 i = 0; i < state.buffer_length; i++) {
    i32 pixel_y = row * font->height - (i32)state.scroll_y;
    i32 pixel_x = col * font->width - (i32)state.scroll_x;
    bool visible = (pixel_y + font->height > 0 && pixel_y < text_area_height &&
                    pixel_x + font->width > 0 && pixel_x < state.canvas_width);

    u32 char_color = SYN_TEXT;
    if (syntax_tokens) {
      while (tok_idx < syntax_token_count &&
             syntax_tokens[tok_idx].span.end <= (size_t)i)
        tok_idx++;
      if (tok_idx < syntax_token_count &&
          syntax_tokens[tok_idx].span.start <= (size_t)i &&
          (size_t)i < syntax_tokens[tok_idx].span.end) {
        char_color = color_for_token_type(syntax_tokens[tok_idx].type);
      }
    }

    char character = text_buffer[i];
    if (character == '\n') {
      row++;
      col = 0;
    } else {
      if (visible)
        draw_char(pixels, pitch, pixel_x, pixel_y, (u8)character, char_color);
      col++;
    }
  }

  if (state.cursor_visible) {
    i32 grid_px = cursor_col * font->width - (i32)state.scroll_x;
    i32 grid_py = cursor_row * font->height - (i32)state.scroll_y;

    f32 anim_dx = state.cursor_display_x - target_x;
    f32 anim_dy = state.cursor_display_y - target_y;
    i32 rect_px = grid_px + (i32)SDL_roundf(anim_dx);
    i32 rect_py = grid_py + (i32)SDL_roundf(anim_dy);

    if (rect_py + font->height > 0 && rect_py < text_area_height &&
        rect_px + font->width > 0 && rect_px < state.canvas_width) {
      i32 rect_height = font->height;
      if (rect_py + rect_height > text_area_height)
        rect_height = text_area_height - rect_py;
      if (rect_height > 0)
        draw_rect(pixels, pitch, rect_px, rect_py, font->width, rect_height,
                  COLOR_CURSOR);
    }

    bool settled = (anim_dx * anim_dx + anim_dy * anim_dy) < 0.25f;
    if (settled && state.cursor < state.buffer_length && grid_py >= 0 &&
        grid_py + font->height <= text_area_height) {
      u8 under = text_buffer[state.cursor] != '\n'
                     ? (u8)text_buffer[state.cursor]
                     : ' ';
      if (under != ' ')
        draw_char(pixels, pitch, grid_px, grid_py, under, COLOR_BG);
    }
  }

  i32 status_y = state.canvas_height - font->height - 1;
  draw_rect(pixels, pitch, 0, status_y, state.canvas_width, font->height + 1,
            COLOR_STATUS_BG);

  bool show_saved = state.save_flash_time &&
                    (now - state.save_flash_time) / 1000000ULL < 2000;

  const char *mode_str = state.vim.mode == MODE_INSERT ? "INSERT" : "NORMAL";

  char status[256];
  if (state.vim.in_command) {
    char cmd_display[65];
    SDL_memcpy(cmd_display, state.vim.cmd_buf, state.vim.cmd_len);
    cmd_display[state.vim.cmd_len] = '\0';
    SDL_snprintf(status, sizeof(status), ":%s", cmd_display);
  } else if (show_saved) {
    SDL_snprintf(status, sizeof(status), "%d:%d  saved %s", cursor_row + 1,
                 cursor_col + 1, state.filepath ? state.filepath : "");
  } else {
    SDL_snprintf(status, sizeof(status), "%d:%d  %s  (F1) %dx  (F2) %s%s%s",
                 cursor_row + 1, cursor_col + 1, mode_str, state.scale,
                 font->name, state.filepath ? "  " : "",
                 state.filepath ? state.filepath : "");
  }
  draw_string(pixels, pitch, 0, status_y + 1, status, COLOR_STATUS_TEXT);

  {
    char fps_text[16];
    SDL_snprintf(fps_text, sizeof(fps_text), "%d fps", state.fps);
    i32 length = (i32)SDL_strlen(fps_text);
    draw_string(pixels, pitch, state.canvas_width - length * font->width,
                status_y + 1, fps_text, COLOR_STATUS_TEXT);
  }

  SDL_UnlockTexture(state.canvas);

  SDL_FRect source = {0, 0, (f32)state.canvas_width, (f32)state.canvas_height};
  SDL_SetRenderDrawColor(state.renderer, 30, 30, 30, 255);
  SDL_RenderClear(state.renderer);
  SDL_RenderTexture(state.renderer, state.canvas, &source, NULL);
  SDL_RenderPresent(state.renderer);
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
  (void)appstate;
  if (event->type == SDL_EVENT_QUIT)
    return SDL_APP_SUCCESS;

  if (event->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
    update_canvas_size();

  if (event->type == SDL_EVENT_MOUSE_WHEEL) {
    state.scroll_y -= event->wheel.y * SCROLL_SPEED;
    state.scroll_x += event->wheel.x * SCROLL_SPEED;
    if (state.scroll_y < 0)
      state.scroll_y = 0;
    if (state.scroll_x < 0)
      state.scroll_x = 0;
  }

  if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
      event->button.button == SDL_BUTTON_LEFT) {
    const Font *font = current_font();
    i32 pixel_x = (i32)(event->button.x * state.pixel_density) / state.scale;
    i32 pixel_y = (i32)(event->button.y * state.pixel_density) / state.scale;
    i32 click_row = (pixel_y + (i32)state.scroll_y) / font->height;
    i32 click_col = (pixel_x + (i32)state.scroll_x) / font->width;
    if (click_col < 0)
      click_col = 0;
    state.cursor = pos_from_row_col(click_row, click_col);
    reset_blink();
  }

  if (event->type == SDL_EVENT_TEXT_INPUT) {
    const char *text = event->text.text;
    if (state.vim.in_command) {
      for (i32 i = 0; text[i]; i++) {
        char c = text[i];
        if (c >= 32 && c < 127 && state.vim.cmd_len < 63)
          state.vim.cmd_buf[state.vim.cmd_len++] = c;
      }
    } else if (state.vim.mode == MODE_INSERT) {
      for (i32 i = 0; text[i]; i++) {
        char c = text[i];
        if (c >= 32 && c < 127) {
          buffer_insert(state.cursor, c);
          state.cursor++;
        }
      }
      reset_blink();
    } else {
      for (i32 i = 0; text[i]; i++) {
        char c = text[i];
        if (c >= 32 && c < 127)
          handle_normal_char(c);
      }
    }
  }

  if (event->type == SDL_EVENT_KEY_DOWN) {
    i32 row, col;
    cursor_row_col(&row, &col);

    if (state.vim.in_command) {
      switch (event->key.key) {
      case SDLK_RETURN: {
        SDL_AppResult result = handle_command();
        if (result != SDL_APP_CONTINUE)
          return result;
        break;
      }
      case SDLK_ESCAPE:
        state.vim.in_command = false;
        break;
      case SDLK_BACKSPACE:
        if (state.vim.cmd_len > 0)
          state.vim.cmd_len--;
        else
          state.vim.in_command = false;
        break;
      default:
        break;
      }
      return SDL_APP_CONTINUE;
    }

    switch (event->key.key) {
    case SDLK_ESCAPE:
      if (state.vim.mode == MODE_INSERT) {
        state.vim.mode = MODE_NORMAL;
        state.vim.pending = 0;
        if (state.cursor > 0 && state.cursor > line_start(state.cursor))
          state.cursor--;
        reset_blink();
      }
      break;

    case SDLK_S:
      if (event->key.mod & (SDL_KMOD_GUI | SDL_KMOD_CTRL))
        save_file();
      break;

    case SDLK_F1:
      state.scale = (state.scale % 8) + 1;
      update_canvas_size();
      break;

    case SDLK_F2:
      state.font_index = (state.font_index + 1) % FONT_COUNT;
      break;

    case SDLK_RETURN:
      if (state.vim.mode == MODE_INSERT) {
        buffer_insert(state.cursor, '\n');
        state.cursor++;
        reset_blink();
      }
      break;

    case SDLK_BACKSPACE:
      if (state.vim.mode == MODE_INSERT && state.cursor > 0) {
        state.cursor--;
        buffer_delete(state.cursor);
        reset_blink();
      }
      break;

    case SDLK_DELETE:
      if (state.vim.mode == MODE_INSERT && state.cursor < state.buffer_length) {
        buffer_delete(state.cursor);
        reset_blink();
      }
      break;

    case SDLK_LEFT:
      if (state.cursor > 0) {
        state.cursor--;
        reset_blink();
      }
      break;

    case SDLK_RIGHT:
      if (state.cursor < state.buffer_length) {
        state.cursor++;
        reset_blink();
      }
      break;

    case SDLK_UP:
      if (row > 0) {
        state.cursor = pos_from_row_col(row - 1, col);
        reset_blink();
      }
      break;

    case SDLK_DOWN: {
      i32 end = line_end(state.cursor);
      if (end < state.buffer_length) {
        state.cursor = pos_from_row_col(row + 1, col);
        reset_blink();
      }
      break;
    }

    case SDLK_HOME:
      state.cursor = line_start(state.cursor);
      reset_blink();
      break;

    case SDLK_END:
      state.cursor = line_end(state.cursor);
      reset_blink();
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
