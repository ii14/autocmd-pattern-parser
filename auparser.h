#pragma once

#include <stddef.h>
#include <stdbool.h>

typedef enum {
  End = 0,      /// internal, end of tokens
  Literal,      /// matches string literals
  AnyChar,      /// matches ? (translates to . in vim/lua regex)
  AnyChars,     /// matches * (translates to .* in vim/lua regex)
  Set,          /// matches character sets, eg. [^2-3abc]
  Cls,          /// matches character class, eg. \d \s \X
  Opts,         /// matches vim regex settings, eg. \c for turning on ignorecase
  ZeroOrMore,   /// matches \*
  ZeroOrOne,    /// matches \= (\? is a literal ?, so idk if there is another way to have this)
  OneOrMore,    /// matches \+
  Count,        /// previous atom repeated N times, eg. \\\{6\}
  Push,         /// internal, matches { \(
  Branch,       /// internal, matches , \|
  Pop,          /// internal, matches } \)
  Empty,        /// internal, inserted in empty branches
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


/// Writes escaped string
/// @param[out] out   output buffer
/// @param[in]  max   output buffer max size
/// @param[in]  str   string to escape
/// @param[in]  len   string length
/// @return     number of bytes written
int write_escaped(char *out, size_t max, const char *str, size_t len);
