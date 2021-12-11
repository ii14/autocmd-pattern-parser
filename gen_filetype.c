#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>

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
  Class,
  Opts,
} type_t;

static const char *type_str(type_t type)
{
  switch (type) {
#define CASE(TYPE) case TYPE: return #TYPE
    CASE(Empty);
    CASE(Literal);
    CASE(Count);
    CASE(Set);
    CASE(ZeroOrMore);
    CASE(ZeroOrOne);
    CASE(OneOrMore);
    CASE(AnyChar);
    CASE(AnyChars);
    CASE(Push);
    CASE(Branch);
    CASE(Pop);
    CASE(Class);
    CASE(Opts);
#undef CASE
  }
  return "???";
}

typedef struct token {
  /// token type
  type_t type;
  /// where it begins in string
  const char *begin;
  /// length of string
  size_t len;
  /// nest level for branches
  size_t level;
} token_t;

/// Error message
static const char *error = NULL;

#define CHARACTER_CLASSES "iIkKfFpPsSdDxXoOwWhHaAlLuU"

static bool lex(const char *str);
static void print_token(const token_t *tok);
static void print_tokens(const token_t *toks, size_t size);
static size_t print_range(char *buf, size_t bufsize, const token_t *toks, size_t size);
static void unroll_branch(const token_t *toks, size_t size);
static void unroll_subbranch(const token_t *toks, size_t size);
static bool match_autocmd(const char *str);
static bool match_events(const char *str);

#define ERROR(msg) \
  do { \
    error = msg; \
    return false; \
  } while (0)

/// Tokenize string
/// @return true on success
static bool lex(const char *str)
{
  const char *it;
  int level = 0;

  token_t toks[512];
  size_t size = 0;
  size_t pushed = 0;

  const char *literal = NULL;
  const char *begin = str;
  bool isliteral = false;

#define PUSH(TYPE, BEGIN, LEN) \
  do { \
    if (size >= sizeof(toks) / sizeof(*toks)) \
      ERROR("overflow"); \
    toks[size++] = (token_t){ \
      .type=(TYPE), .begin=(BEGIN), .len=(LEN), .level=0 \
    }; \
    ++pushed; \
  } while (0)

  for (it = str; *it != '\0'; ++it) {
    begin = it;
    pushed = 0;
    isliteral = false;

    if (*it == '{') {
      // open group
      ++level;
      PUSH(Push, it, 1);
      toks[size - 1].level = level;
    } else if (*it == '}') {
      // close group
      if (--level < 0)
        ERROR("unexpected '}'");
      PUSH(Pop, it, 1);
      toks[size - 1].level = level + 1;
    } else if (*it == ',') {
      // pattern separator
      PUSH(Branch, it, 1);
      toks[size - 1].level = level;
    } else if (*it == '\\') {
      const char *bsbegin = it;
      if (*(++it) == '\0') {
        ERROR("unexpected end after '\\'");
      } else if (*it == '(') {
        // open group, same as {
        // this is wrong, the level of nesting shouldn't be shared, but whatever
        ++level;
        PUSH(Push, bsbegin, 2);
        toks[size - 1].level = level;
      } else if (*it == ')') {
        // close group, same as }
        if (--level < 0)
          ERROR("unexpected '}'");
        PUSH(Pop, bsbegin, 2);
        toks[size - 1].level = level + 1;
      } else if (*it == '|') {
        // pattern separator, same as ,
        PUSH(Branch, bsbegin, 2);
        toks[size - 1].level = level;
      } else if (strchr(",?{}", *it)) {
        // literal , ? { }
        isliteral = true;
      } else if (*it == '*') {
        PUSH(ZeroOrMore, bsbegin, 2);
      } else if (*it == '+') {
        PUSH(OneOrMore, bsbegin, 2);
      } else if (*it == '?' || *it == '=') {
        PUSH(ZeroOrOne, bsbegin, 2);
      } else if (strchr(CHARACTER_CLASSES, *it)) {
        PUSH(Class, bsbegin, 2);
      } else if (*it == '_') {
        if (*(++it) == '\0') {
          ERROR("unexpected end after '_'");
        } else if (strchr(CHARACTER_CLASSES, *it)) {
          PUSH(Class, bsbegin, 3);
        } else {
          ERROR("unknown character class after '_'");
        }
      } else if (*it == '\\') {
        if (*(++it) == '\0') {
          ERROR("unexpected end after '\\'");
        } else if (*it == '\\') {
          if (*(++it) == '\0') {
            ERROR("unexpected end after '\\'");
          } else if (*it == '{') {
            // lua: {} (*), {-} (-), {n} {-n} (unroll)
            // vim: {n,m} {n,} {,m} {-n,m} {-n,} {-,m}
            if (*(++it) == '\0')
              ERROR("unexpected end after '{'");
            if (*it == '-')
              ++it;
            while (isdigit(*it))
              ++it;
            if (*it == ',')
              ++it;
            while (isdigit(*it))
              ++it;
            if (*it != '\\')
              ERROR("invalid '{}' atom");
            if (*(++it) != '}')
              ERROR("invalid '{}' atom");
            PUSH(Count, bsbegin, it - bsbegin + 1);
          } else {
            ERROR("unknown escape sequence");
          }
        } else {
          ERROR("unknown escape sequence");
        }
      } else if (*it == '{') {
        ERROR("unknown regex pattern '\\{'");
      } else if (strchr("cCZmMvV", *it)) {
        // vim regex settings. force vim pattern
        PUSH(Opts, bsbegin, 2);
      } else {
        ERROR("unknown regex pattern");
      }
    } else if (*it == '[') {
      const char *csbegin = it;
      if (*(++it) == '\0') {
        ERROR("unclosed '['");
      } else if (*it == '^') {
        // negated character set ([^abc])
        bool nested = false;
        while (true) {
          if (*(++it) == '\0') {
            ERROR("unclosed '['");
          } else if (*it == '[') {
            if (nested)
              ERROR("unexpected '['");
            nested = true;
          } else if (*it == ']') {
            if (nested) {
              nested = false;
            } else {
              PUSH(Set, csbegin, it - csbegin + 1);
              break;
            }
          } else if (isalnum(*it) || strchr("-_.:", *it)) {
            // ...
          } else {
            ERROR("character from character set not supported");
          }
        }
      } else {
        // character set ([abc])
        --it; // decrement to keep the same as above
        bool nested = false;
        while (true) {
          if (*(++it) == '\0') {
            ERROR("unclosed '['");
          } else if (*it == '[') {
            if (nested)
              ERROR("unexpected '['");
            nested = true;
          } else if (*it == ']') {
            if (nested) {
              nested = false;
            } else {
              PUSH(Set, csbegin, it - csbegin + 1);
              break;
            }
          } else if (isalnum(*it) || strchr("-_.:", *it)) {
            // ...
          } else {
            ERROR("character from character set not supported");
          }
        }
      }
    } else if (*it == '*') {
      PUSH(AnyChars, it, 1);
    } else if (*it == '?') {
      PUSH(AnyChar, it, 1);
    } else {
      isliteral = true;
    }

    if (!isliteral && pushed == 0) {
      ERROR("not a literal and nothing was pushed");
    } else if (isliteral && literal == NULL) {
      literal = begin;
      // isliteral = false;
    } else if (!isliteral) {
      // push literals
      if (literal != NULL) {
        if (pushed > 0) {
          // since we're pushing the literal after we
          // got a non-literal, swap with last token
          if (pushed != 1)
            ERROR("pushed != 1");
          token_t copy = toks[size - 1];
          --size;
          PUSH(Literal, literal, begin - literal);
          toks[size++] = copy;
        } else {
          PUSH(Literal, literal, begin - literal);
        }
        literal = NULL;
      }

      // add empty literals for empty branches
      if (size > 1) {
        type_t t1 = toks[size - 1].type;
        type_t t2 = toks[size - 2].type;
        if ((t1 == Branch || t1 == Pop) && (t2 == Push || t2 == Branch)) {
          token_t copy = toks[size - 1];
          --size;
          PUSH(Empty, "", 0);
          toks[size++] = copy;
        }
      }
    }
  }

  if (isliteral) {
    // push literals
    if (literal != NULL) {
      PUSH(Literal, literal, it - literal);
    } else {
      PUSH(Literal, begin, 1);
    }
  } else if (size > 1) {
    // add empty literals for empty branches
    type_t t1 = toks[size - 1].type;
    type_t t2 = toks[size - 2].type;
    if ((t1 == Branch || t1 == Pop) && (t2 == Push || t2 == Branch)) {
      token_t copy = toks[size - 1];
      --size;
      PUSH(Empty, "", 0);
      toks[size++] = copy;
    }
  }

  if (level != 0) {
    ERROR("unclosed branches");
  }

  // printf("%s\n", toks[0].begin);
  // print_tokens(toks, size);
  unroll_branch(toks, size);
  return true;
}

/// Print string range that is represented by the token array
/// @param[out] buf     output buffer
/// @param[in]  bufsize output buffer size
/// @param[in]  toks    tokens
/// @param[in]  size    tokens size
/// @return             how many bytes were written
static size_t print_range(char *buf, size_t bufsize, const token_t *toks, size_t size)
{
  size_t n = 0;
  for (size_t i = 0; i < size; ++i) {
    const token_t *tok = &toks[i];
    if (n + tok->len < bufsize) {
      memcpy(buf + n, tok->begin, tok->len);
      n += tok->len;
    } else {
      if (bufsize > 0)
        *buf = '\0';
      fprintf(stderr, "range too long\n");
      return 0;
    }
  }
  buf[n] = '\0';
  return n;
}

/// Print a single token
/// @param[in]  tok     token
static void print_token(const token_t *tok)
{
  char buf[1024] = {0};
  const char *type = type_str(tok->type);
  if (tok->len < 1024) {
    memcpy(buf, tok->begin, tok->len);
    buf[tok->len] = '\0';
    if (tok->level > 0) {
      printf("        %s[%ld]: %s\n", type, tok->level, buf);
    } else {
      printf("        %s: %s\n", type, buf);
    }
  } else {
    printf("        %s: (token length too long)\n", type);
  }
}

/// Print token array
/// @param[in]  toks    tokens
/// @param[in]  size    tokens size
static void print_tokens(const token_t *toks, size_t size)
{
  for (size_t i = 0; i < size; ++i) {
    print_token(&toks[i]);
  }
}

static void unroll_branch(const token_t *toks, size_t size)
{
  size_t prev = 0;
  size_t i = 0;
  int branch = 0;
  char buf[1024] = {0};
  size_t n;

  if (size > 0) {
    printf("%s\n", toks[0].begin);
  }

  for (i = 0; i < size; ++i) {
    const token_t *tok = &toks[i];
    if (tok->type == Branch && tok->level == 0) {
      n = print_range(buf, 1024, toks + prev, i - prev);
      if (n > 0) {
        printf("    %d: %s\n", branch++, buf);
      } else {
        printf("    %d:\n", branch++);
      }
      unroll_subbranch(toks + prev, i - prev);
      prev = i + 1;
    }
  }

  n = print_range(buf, 1024, toks + prev, i - prev);
  if (n > 0) {
    printf("    %d: %s\n", branch++, buf);
  } else {
    printf("    %d:\n", branch++);
  }
  unroll_subbranch(toks + prev, i - prev);
}

static void unroll_subbranch(const token_t *toks, size_t size)
{
#define MAX_BRANCHES (64)

  // count branches and their sizes
  int total = 0;
  int totals[MAX_BRANCHES] = {0};
  for (size_t i = 0; i < size; ++i) {
    const token_t *tok = &toks[i];
    if (tok->level == 1) {
      if (tok->type == Push) {
        ++total;
        ++totals[total - 1];
      } else if (tok->type == Branch) {
        ++totals[total - 1];
      }
    } else if (tok->level > 2) {
      fprintf(stderr, "level of two not supported\n");
    }
  }

  int brs[MAX_BRANCHES] = {0};
  bool inc = false;
  do {
    printf("      ---\n");

    int nbranch = -1;
    int cur = -1;
    for (size_t i = 0; i < size; ++i) {
      const token_t *tok = &toks[i];

      if (tok->level == 1) {
        if (tok->type == Push) {
          ++nbranch;
          cur = 0;
        } else if (tok->type == Branch) {
          ++cur;
        } else if (tok->type == Pop) {
          cur = -1;
        }
        continue;
      }

      if (cur == -1 || brs[nbranch] == cur) {
        print_token(tok);
      }
    }

    inc = false;
    for (int i = 0; i < total; ++i) {
      if (++brs[i] < totals[i]) {
        inc = true;
        break;
      } else {
        brs[i] = 0;
      }
    }
  } while (inc);
}

/// Match autocommand name. in vim regex: "au%[utocmd]!?"
static bool match_autocmd(const char *str)
{
  if (str[0] != 'a' || str[1] != 'u')
    return false;
  if (str[2] == '\0' || (str[2] == '!' && str[3] == '\0'))
    return true;
  for (size_t i = 2; i < sizeof("autocmd"); ++i) {
    if (str[i] != "autocmd"[i])
      return false;
    if (str[i + 1] == '\0' || (str[i + 1] == '!' && str[i + 2] == '\0'))
      return true;
  }
  return false;
}

/// Match event names. BufNewFile and BufRead/BufReadPost
static bool match_events(const char *str)
{
#define EVENT_NAME_SIZE (16)
  char buf[EVENT_NAME_SIZE] = {0};
  const char *it = str;

  bool bufnewfile = false;
  bool bufread = false;
  bool bufreadpost = false;

  while (*it != '\0') {
    size_t i;
    for (i = 0; i < EVENT_NAME_SIZE - 1; ++i) {
      if (it[i] == '\0' || it[i] == ',')
        break;
      buf[i] = tolower(it[i]);
    }
    buf[i] = '\0';

    if (strncmp(buf, "bufnewfile", EVENT_NAME_SIZE) == 0) {
      bufnewfile = true;
    } else if (strncmp(buf, "bufread", EVENT_NAME_SIZE) == 0) {
      bufread = true;
    } else if (strncmp(buf, "bufreadpost", EVENT_NAME_SIZE) == 0) {
      bufreadpost = true;
    }

    if (it[i] == '\0')
      break;
    it += i + 1;
  }

  return bufnewfile && (bufread || bufreadpost);
}

int main(int argc, char *argv[])
{
  if (argc != 2) {
    fprintf(stderr, "Usage: gen_filetype <file>\n");
    return EXIT_FAILURE;
  }

  FILE *fp;
  if (argv[1][0] == '-' && argv[1][1] == '\0') {
    fp = stdin;
  } else {
    fp = fopen(argv[1], "rb");
    if (fp == NULL) {
      perror("fopen");
      return EXIT_FAILURE;
    }
  }

  char *line = NULL;
  size_t len = 0;
  ssize_t nread = 0;

#define SKIP_WHITESPACE \
  do { \
    while (*it != '\0' && isspace(*it)) \
      ++it; \
    if (*it == '\0') \
      continue; \
  } while (0)

#define SKIP_TO_WHITESPACE \
  do { \
    while (*it != '\0' && !isspace(*it)) \
      ++it; \
  } while (0)

  for (size_t lnum = 1; (nread = getline(&line, &len, fp)) >= 0; ++lnum) {
    char *it = line;
    SKIP_WHITESPACE;

    if (*it == 'a') {
      char *au = it;
      SKIP_TO_WHITESPACE;
      *it = '\0';
      if (!match_autocmd(au))
        continue;
      if (*(++it) == '\0')
        continue;

      SKIP_WHITESPACE;
      char *events = it;
      SKIP_TO_WHITESPACE;
      *it = '\0';
      if (!match_events(events))
        continue;
      if (*(++it) == '\0')
        continue;

      SKIP_WHITESPACE;
      char *pat = it;
      SKIP_TO_WHITESPACE;
      *it = '\0';
      if (*(++it) == '\0')
        continue;

      SKIP_WHITESPACE;
      char *cmd = it;

      // printf("evt: %s\n", events);
      // printf("%s\n", pat);
      bool res = lex(pat);
      if (!res) puts(error);
      // printf("cmd: %s\n", cmd);
      (void)cmd;
    } else if (*it == '\\') {
      ++it;
      SKIP_WHITESPACE;
      // printf("%s", it);
    }
  }

  free(line);
  if (fp != stdin) {
    fclose(fp);
  }
  return EXIT_SUCCESS;
}
