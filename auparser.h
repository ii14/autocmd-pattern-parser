#pragma once

#include <stddef.h>
#include <stdbool.h>

typedef enum {
  Empty = 0,
  Literal,
  Count,
  Set,
  ZeroOrMore,
  ZeroOrOne,
  OneOrMore,
  AnyChar,
  AnyChars,
  Push,
  Branch,
  Pop,
  Cls,
  Opts,
} type_t;

const char *type_str(type_t type);

extern const char *error;

typedef struct token {
  type_t type;      /// token type
  const char *beg;  /// where it begins in string
  size_t len;       /// length of string
  int lvl;          /// nest level for branches
} token_t;

/// Print a single token
/// @param[in]  tok     token
void print_token(const token_t *tok);

/// Print string range that is represented by the token array
/// @param[out] buf     output buffer
/// @param[in]  bufsize output buffer size
/// @param[in]  toks    tokens
/// @param[in]  size    tokens size
/// @return     how many bytes were written
size_t print_range(char *buf, size_t bsize, const token_t *toks, size_t size);

/// Tokenize string
/// @param[in]  str         pattern to tokenize
/// @param[out] buf         tokens buffer
/// @param[in,out] bsize    initial buffer size, returns size
/// @return true on success
bool tokenize(const char *str, token_t *buf, size_t *bsize);
// TODO: return size

bool unroll(const token_t *toks, size_t size);
void print_stack();

/// Match autocommand name. in vim regex: "au%[utocmd]!?"
bool match_autocmd(const char *str);
/// Match event names. BufNewFile and BufRead/BufReadPost
bool match_events(const char *str);
