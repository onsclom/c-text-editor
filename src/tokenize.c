#include "tokenize.h"

static void emit(Token *tokens, TokenizeResult *result, TokenType type,
                 size_t start, size_t end) {
  tokens[result->count++] = (Token){type, {start, end}};
}

static size_t skip_char_or_escape(const u8 *src, size_t pos, size_t len) {
  if (pos < len && src[pos] == '\\')
    pos++;
  if (pos < len)
    pos++;
  return pos;
}

static TokenType classify_identifier(s8 ident) {
  if (s8_eq(ident, s8_lit("return")))   return TOKEN_KEYWORD_RETURN;
  if (s8_eq(ident, s8_lit("if")))       return TOKEN_KEYWORD_IF;
  if (s8_eq(ident, s8_lit("else")))     return TOKEN_KEYWORD_ELSE;
  if (s8_eq(ident, s8_lit("while")))    return TOKEN_KEYWORD_WHILE;
  if (s8_eq(ident, s8_lit("for")))      return TOKEN_KEYWORD_FOR;
  if (s8_eq(ident, s8_lit("do")))       return TOKEN_KEYWORD_DO;
  if (s8_eq(ident, s8_lit("switch")))   return TOKEN_KEYWORD_SWITCH;
  if (s8_eq(ident, s8_lit("case")))     return TOKEN_KEYWORD_CASE;
  if (s8_eq(ident, s8_lit("default")))  return TOKEN_KEYWORD_DEFAULT;
  if (s8_eq(ident, s8_lit("break")))    return TOKEN_KEYWORD_BREAK;
  if (s8_eq(ident, s8_lit("continue"))) return TOKEN_KEYWORD_CONTINUE;
  if (s8_eq(ident, s8_lit("goto")))     return TOKEN_KEYWORD_GOTO;
  if (s8_eq(ident, s8_lit("sizeof")))   return TOKEN_KEYWORD_SIZEOF;
  if (s8_eq(ident, s8_lit("static")))   return TOKEN_KEYWORD_STATIC;
  if (s8_eq(ident, s8_lit("const")))    return TOKEN_KEYWORD_CONST;
  if (s8_eq(ident, s8_lit("extern")))   return TOKEN_KEYWORD_EXTERN;
  if (s8_eq(ident, s8_lit("typedef")))  return TOKEN_KEYWORD_TYPEDEF;
  if (s8_eq(ident, s8_lit("struct")))   return TOKEN_KEYWORD_STRUCT;
  if (s8_eq(ident, s8_lit("enum")))     return TOKEN_KEYWORD_ENUM;
  if (s8_eq(ident, s8_lit("union")))    return TOKEN_KEYWORD_UNION;
  if (s8_eq(ident, s8_lit("volatile"))) return TOKEN_KEYWORD_VOLATILE;
  if (s8_eq(ident, s8_lit("inline")))   return TOKEN_KEYWORD_INLINE;
  if (s8_eq(ident, s8_lit("register"))) return TOKEN_KEYWORD_REGISTER;

  if (s8_eq(ident, s8_lit("int")))      return TOKEN_TYPE_INT;
  if (s8_eq(ident, s8_lit("float")))    return TOKEN_TYPE_FLOAT;
  if (s8_eq(ident, s8_lit("char")))     return TOKEN_TYPE_CHAR;
  if (s8_eq(ident, s8_lit("void")))     return TOKEN_TYPE_VOID;
  if (s8_eq(ident, s8_lit("short")))    return TOKEN_TYPE_SHORT;
  if (s8_eq(ident, s8_lit("long")))     return TOKEN_TYPE_LONG;
  if (s8_eq(ident, s8_lit("signed")))   return TOKEN_TYPE_SIGNED;
  if (s8_eq(ident, s8_lit("unsigned"))) return TOKEN_TYPE_UNSIGNED;
  if (s8_eq(ident, s8_lit("double")))   return TOKEN_TYPE_DOUBLE;
  if (s8_eq(ident, s8_lit("bool")))     return TOKEN_TYPE_BOOL;
  if (s8_eq(ident, s8_lit("_Bool")))    return TOKEN_TYPE_BOOL;

  if (s8_eq(ident, s8_lit("i8")))       return TOKEN_TYPE_BUILTIN;
  if (s8_eq(ident, s8_lit("i16")))      return TOKEN_TYPE_BUILTIN;
  if (s8_eq(ident, s8_lit("i32")))      return TOKEN_TYPE_BUILTIN;
  if (s8_eq(ident, s8_lit("i64")))      return TOKEN_TYPE_BUILTIN;
  if (s8_eq(ident, s8_lit("u8")))       return TOKEN_TYPE_BUILTIN;
  if (s8_eq(ident, s8_lit("u16")))      return TOKEN_TYPE_BUILTIN;
  if (s8_eq(ident, s8_lit("u32")))      return TOKEN_TYPE_BUILTIN;
  if (s8_eq(ident, s8_lit("u64")))      return TOKEN_TYPE_BUILTIN;
  if (s8_eq(ident, s8_lit("f32")))      return TOKEN_TYPE_BUILTIN;
  if (s8_eq(ident, s8_lit("f64")))      return TOKEN_TYPE_BUILTIN;
  if (s8_eq(ident, s8_lit("size_t")))   return TOKEN_TYPE_BUILTIN;
  if (s8_eq(ident, s8_lit("ssize_t")))  return TOKEN_TYPE_BUILTIN;
  if (s8_eq(ident, s8_lit("ptrdiff_t")))return TOKEN_TYPE_BUILTIN;
  if (s8_eq(ident, s8_lit("uintptr_t")))return TOKEN_TYPE_BUILTIN;
  if (s8_eq(ident, s8_lit("intptr_t"))) return TOKEN_TYPE_BUILTIN;
  if (s8_eq(ident, s8_lit("int8_t")))   return TOKEN_TYPE_BUILTIN;
  if (s8_eq(ident, s8_lit("int16_t")))  return TOKEN_TYPE_BUILTIN;
  if (s8_eq(ident, s8_lit("int32_t")))  return TOKEN_TYPE_BUILTIN;
  if (s8_eq(ident, s8_lit("int64_t")))  return TOKEN_TYPE_BUILTIN;
  if (s8_eq(ident, s8_lit("uint8_t")))  return TOKEN_TYPE_BUILTIN;
  if (s8_eq(ident, s8_lit("uint16_t"))) return TOKEN_TYPE_BUILTIN;
  if (s8_eq(ident, s8_lit("uint32_t"))) return TOKEN_TYPE_BUILTIN;
  if (s8_eq(ident, s8_lit("uint64_t"))) return TOKEN_TYPE_BUILTIN;

  if (s8_eq(ident, s8_lit("true")))     return TOKEN_CONST_TRUE;
  if (s8_eq(ident, s8_lit("false")))    return TOKEN_CONST_FALSE;
  if (s8_eq(ident, s8_lit("NULL")))     return TOKEN_CONST_NULL;

  return TOKEN_IDENTIFIER;
}

TokenizeResult tokenize(Arena *arena, s8 source) {
  size_t pos = 0;
  size_t len = source.length;
  const u8 *src = source.data;

  Token *tokens = arena_alloc(arena, len * sizeof(Token));
  TokenizeResult result = {.tokens = tokens, .count = 0};

  while (pos < len) {
    u8 cur = src[pos];
    u8 next = (pos + 1 < len) ? src[pos + 1] : '\0';

    if (cur == ' ' || cur == '\t' || cur == '\n' || cur == '\r') {
      pos++;
      continue;
    }

    if (cur == '/' && next == '/') {
      size_t start = pos;
      pos += 2;
      while (pos < len && src[pos] != '\n') pos++;
      emit(tokens, &result, TOKEN_COMMENT_LINE, start, pos);
      continue;
    }
    if (cur == '/' && next == '*') {
      size_t start = pos;
      pos += 2;
      while (pos + 1 < len && !(src[pos] == '*' && src[pos + 1] == '/')) pos++;
      if (pos + 1 < len) pos += 2;
      else               pos = len;
      emit(tokens, &result, TOKEN_COMMENT_BLOCK, start, pos);
      continue;
    }

    if (cur == '#') {
      size_t scan = pos;
      bool at_line_start = true;
      while (scan > 0) {
        u8 pc = src[scan - 1];
        if (pc == '\n') break;
        if (pc != ' ' && pc != '\t') { at_line_start = false; break; }
        scan--;
      }
      if (at_line_start) {
        size_t start = pos;
        while (pos < len && src[pos] != '\n') {
          if (src[pos] == '\\' && pos + 1 < len && src[pos + 1] == '\n') {
            pos += 2;
            continue;
          }
          pos++;
        }
        emit(tokens, &result, TOKEN_PREPROCESSOR, start, pos);
        continue;
      }
    }

    switch (cur) {
    case ';': emit(tokens, &result, TOKEN_SEMICOLON, pos, pos + 1); pos++; continue;
    case '(': emit(tokens, &result, TOKEN_LPAREN,    pos, pos + 1); pos++; continue;
    case ')': emit(tokens, &result, TOKEN_RPAREN,    pos, pos + 1); pos++; continue;
    case '{': emit(tokens, &result, TOKEN_LBRACE,    pos, pos + 1); pos++; continue;
    case '}': emit(tokens, &result, TOKEN_RBRACE,    pos, pos + 1); pos++; continue;
    case '[': emit(tokens, &result, TOKEN_LBRACKET,  pos, pos + 1); pos++; continue;
    case ']': emit(tokens, &result, TOKEN_RBRACKET,  pos, pos + 1); pos++; continue;
    case ',': emit(tokens, &result, TOKEN_COMMA,     pos, pos + 1); pos++; continue;
    case ':': emit(tokens, &result, TOKEN_COLON,     pos, pos + 1); pos++; continue;
    case '.': emit(tokens, &result, TOKEN_DOT,       pos, pos + 1); pos++; continue;
    case '?': emit(tokens, &result, TOKEN_QUESTION,  pos, pos + 1); pos++; continue;
    case '~': emit(tokens, &result, TOKEN_TILDE,     pos, pos + 1); pos++; continue;
    case '^': emit(tokens, &result, TOKEN_CARET,     pos, pos + 1); pos++; continue;
    case '*': emit(tokens, &result, TOKEN_STAR,      pos, pos + 1); pos++; continue;
    case '%': emit(tokens, &result, TOKEN_PERCENT,   pos, pos + 1); pos++; continue;
    case '/': emit(tokens, &result, TOKEN_DIVIDE,    pos, pos + 1); pos++; continue;

    case '+':
      if (next == '+') { emit(tokens, &result, TOKEN_PLUS_PLUS,   pos, pos + 2); pos += 2; }
      else             { emit(tokens, &result, TOKEN_PLUS,        pos, pos + 1); pos++; }
      continue;
    case '-':
      if (next == '-')  { emit(tokens, &result, TOKEN_MINUS_MINUS, pos, pos + 2); pos += 2; }
      else if (next == '>') { emit(tokens, &result, TOKEN_ARROW,   pos, pos + 2); pos += 2; }
      else              { emit(tokens, &result, TOKEN_MINUS,       pos, pos + 1); pos++; }
      continue;
    case '=':
      if (next == '=') { emit(tokens, &result, TOKEN_EQ_EQ,   pos, pos + 2); pos += 2; }
      else             { emit(tokens, &result, TOKEN_EQ,      pos, pos + 1); pos++; }
      continue;
    case '!':
      if (next == '=') { emit(tokens, &result, TOKEN_BANG_EQ, pos, pos + 2); pos += 2; }
      else             { emit(tokens, &result, TOKEN_BANG,    pos, pos + 1); pos++; }
      continue;
    case '<':
      if (next == '=') { emit(tokens, &result, TOKEN_LT_EQ,   pos, pos + 2); pos += 2; }
      else             { emit(tokens, &result, TOKEN_LT,      pos, pos + 1); pos++; }
      continue;
    case '>':
      if (next == '=') { emit(tokens, &result, TOKEN_GT_EQ,   pos, pos + 2); pos += 2; }
      else             { emit(tokens, &result, TOKEN_GT,      pos, pos + 1); pos++; }
      continue;
    case '&':
      if (next == '&') { emit(tokens, &result, TOKEN_AND_AND, pos, pos + 2); pos += 2; }
      else             { emit(tokens, &result, TOKEN_AND,     pos, pos + 1); pos++; }
      continue;
    case '|':
      if (next == '|') { emit(tokens, &result, TOKEN_OR_OR,   pos, pos + 2); pos += 2; }
      else             { emit(tokens, &result, TOKEN_OR,      pos, pos + 1); pos++; }
      continue;

    default:
      break;
    }

    if (is_digit(cur)) {
      size_t start = pos;
      while (pos < len && (is_alnum(src[pos]) || src[pos] == '.'))
        pos++;
      TokenType t = TOKEN_INT_LITERAL;
      for (size_t i = start; i < pos; i++)
        if (src[i] == '.') { t = TOKEN_FLOAT_LITERAL; break; }
      emit(tokens, &result, t, start, pos);
      continue;
    }

    if (cur == '"') {
      size_t start = pos;
      pos++;
      while (pos < len && src[pos] != '"' && src[pos] != '\n')
        pos = skip_char_or_escape(src, pos, len);
      if (pos < len && src[pos] == '"') pos++;
      emit(tokens, &result, TOKEN_STRING_LITERAL, start, pos);
      continue;
    }

    if (cur == '\'') {
      size_t start = pos;
      pos++;
      while (pos < len && src[pos] != '\'' && src[pos] != '\n')
        pos = skip_char_or_escape(src, pos, len);
      if (pos < len && src[pos] == '\'') pos++;
      emit(tokens, &result, TOKEN_CHAR_LITERAL, start, pos);
      continue;
    }

    if (is_alpha(cur) || cur == '_') {
      size_t start = pos;
      while (pos < len && (is_alnum(src[pos]) || src[pos] == '_'))
        pos++;
      s8 ident = {.data = (u8 *)(src + start), .length = pos - start};
      emit(tokens, &result, classify_identifier(ident), start, pos);
      continue;
    }

    emit(tokens, &result, TOKEN_UNKNOWN, pos, pos + 1);
    pos++;
  }
  return result;
}
