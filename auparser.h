#pragma once

#include <stddef.h>
#include <stdbool.h>

typedef enum {
  End = 0,
  Empty,
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

/// Print internal representation of a single token
/// @param[in]  tok     token
void print_token(const token_t *tok);
/// Print array of tokens
/// @param[in]  toks    array of pointers to tokens
void print_tokens(const token_t **toks);

/// Tokenize pattern
/// @param[in]  pat   pattern to tokenize
/// @return     allocated array of tokens
token_t *tokenize(const char *pat);

/// Unroll pattern
/// @param[in]  toks   token array
/// @return     null terminated array of token_t* arrays
const token_t ***unroll(const token_t *toks);
/// Free array allocated by unroll
void free_tokens(const token_t ***toks);

/// Match autocommand name. in vim regex: "au%[utocmd]!?"
bool match_autocmd(const char *str);
/// Match event names. BufNewFile and BufRead/BufReadPost
bool match_events(const char *str);
