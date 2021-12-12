#include "auparser.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdnoreturn.h>
#include <ctype.h>

const char *type_str(type_t type)
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
    CASE(Cls);
    CASE(Opts);
#undef CASE
  }
  return "Unknown";
}

#define ISBRANCH(TYPE) ((TYPE) == Push || (TYPE) == Branch || (TYPE) == Pop)


/// Error message
const char *error = NULL;
#define ERROR(msg) \
  do { \
    error = msg; \
    return false; \
  } while (0)


#define CHARACTER_CLASSES "iIkKfFpPsSdDxXoOwWhHaAlLuU"

void print_token(const token_t *tok)
{
  char buf[256] = {0};
  const char *type = type_str(tok->type);
  if (tok->len < 256) {
    memcpy(buf, tok->beg, tok->len);
    buf[tok->len] = '\0';
    fprintf(stdout, "[%d]%s: %s\n", tok->lvl, type, buf);
  } else {
    fprintf(stdout, "[%d]%s: (token length too long)\n", tok->lvl, type);
  }
}

size_t print_range(char *buf, size_t bsize, const token_t *toks, size_t size)
{
  size_t n = 0;
  for (size_t i = 0; i < size; ++i) {
    const token_t *tok = &toks[i];
    if (n + tok->len < bsize) {
      memcpy(buf + n, tok->beg, tok->len);
      n += tok->len;
    } else {
      if (bsize > 0)
        *buf = '\0';
      fprintf(stderr, "range too long\n");
      return 0;
    }
  }
  buf[n] = '\0';
  return n;
}


/// Tokenize string
/// @return true on success
bool tokenize(const char *str, token_t *toks, size_t *bsize)
{
  const char *it;
  size_t size = 0;
  size_t pushed = 0;

  const char *literal = NULL;
  const char *beg = str;
  bool isliteral = false;

#define PUSH(TYPE, BEGIN, LEN) \
  do { \
    if (size >= *bsize) \
      ERROR("overflow"); \
    toks[size++] = (token_t){ \
      .type=(TYPE), .beg=(BEGIN), .len=(LEN), .lvl=0 \
    }; \
    ++pushed; \
  } while (0)

  for (it = str; *it != '\0'; ++it) {
    beg = it;
    pushed = 0;
    isliteral = false;

    if (*it == '{') {
      // open group
      PUSH(Push, it, 1);
    } else if (*it == '}') {
      // close group
      PUSH(Pop, it, 1);
    } else if (*it == ',') {
      // pattern separator
      PUSH(Branch, it, 1);
    } else if (*it == '\\') {
      const char *beg2 = it;
      if (*(++it) == '\0') {
        ERROR("unexpected end after '\\'");
      } else if (*it == '(') {
        // open group, same as {
        // this is wrong, the level of nesting shouldn't be shared, but whatever
        PUSH(Push, beg2, 2);
      } else if (*it == ')') {
        // close group, same as }
        PUSH(Pop, beg2, 2);
      } else if (*it == '|') {
        // pattern separator, same as ,
        PUSH(Branch, beg2, 2);
      } else if (strchr(",?{}", *it)) {
        // literal , ? { }
        isliteral = true;
      } else if (*it == '*') {
        PUSH(ZeroOrMore, beg2, 2);
      } else if (*it == '+') {
        PUSH(OneOrMore, beg2, 2);
      } else if (*it == '=') {
        PUSH(ZeroOrOne, beg2, 2);
      } else if (strchr(CHARACTER_CLASSES, *it)) {
        PUSH(Cls, beg2, 2);
      } else if (*it == '_') {
        if (*(++it) == '\0') {
          ERROR("unexpected end after '_'");
        } else if (strchr(CHARACTER_CLASSES, *it)) {
          PUSH(Cls, beg2, 3);
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
            PUSH(Count, beg2, it - beg2 + 1);
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
        PUSH(Opts, beg2, 2);
      } else {
        ERROR("unknown regex pattern");
      }
    } else if (*it == '[') {
      const char *beg2 = it;
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
              PUSH(Set, beg2, it - beg2 + 1);
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
        --it; // decrement to keep the loop same as above
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
              PUSH(Set, beg2, it - beg2 + 1);
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
      literal = beg;
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
          PUSH(Literal, literal, beg - literal);
          toks[size++] = copy;
        } else {
          PUSH(Literal, literal, beg - literal);
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
      PUSH(Literal, beg, 1);
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

#undef PUSH

  {
    // set levels
    int lvl = 0;
    for (size_t i = 0; i < size; ++i) {
      if (toks[i].type == Push) {
        toks[i].lvl = ++lvl;
      } else if (toks[i].type == Pop) {
        toks[i].lvl = lvl--;
        if (lvl < 0)
          ERROR("unexpected branch close");
      } else {
        toks[i].lvl = lvl;
      }
    }
    if (lvl != 0) {
      ERROR("unclosed branch");
    }
  }

  *bsize = size;
  return true;
}


#define STACK_SIZE (256)
static const token_t *stack[STACK_SIZE] = {0};
static size_t ssize = 0;

void print_stack()
{
  char buf[256];
  size_t n = 0;
  const token_t *tok;

  for (size_t i = 0; i < ssize; ++i) {
    tok = stack[i];
    if (n + tok->len < 256) {
      memcpy(buf + n, tok->beg, tok->len);
      n += tok->len;
    } else {
      *buf = '\0';
      fprintf(stderr, "range too long\n");
      return;
    }
  }
  buf[n] = '\0';
  fprintf(stdout, "    %s\n", buf);
}


static bool unroll_rec(const token_t *toks, size_t size, int lvl);

bool unroll(const token_t *toks, size_t size)
{
  if (size == 0)
    ERROR("pattern is empty");

  size_t prev = 0;
  size_t i = 0;

  for (; i < size; ++i) {
    if (toks[i].lvl == 0 && toks[i].type == Branch) {
      ssize = 0;
      if (!unroll_rec(toks + prev, i - prev, 0))
        return false;
      prev = i + 1;
    }
  }

  ssize = 0;
  return unroll_rec(toks + prev, i - prev, 0);
}

static bool unroll_rec(const token_t *toks, size_t size, int lvl)
{
  if (size == 0)
    ERROR("pattern is empty");
  if (lvl > 8)
    ERROR("pattern too deeply nested");

  bool left = false; // left current branch
  size_t ssize_p; // saved stack size
  int tlvl; // temporary level

  for (size_t i = 0; i < size; ++i) {
    if (toks[i].lvl < lvl)
      left = true;

    // skip other branches for the current level
    if (!left && toks[i].lvl == lvl) {
      if (toks[i].type == Branch) {
        while (i < size && toks[i].lvl >= lvl && toks[i].type != Pop)
          ++i;
        --i;
        continue;
      } else if (toks[i].type == Pop) {
        left = true;
        continue;
      }
    }

    // unroll every branch we encounter
    if (toks[i].type == Push) {
      tlvl = toks[i].lvl;
      ++i;
      ssize_p = ssize;
      if (!unroll_rec(toks + i, size - i, tlvl))
        return false;
      ssize = ssize_p; // restore stack size
      for (; i < size; ++i) {
        if (toks[i].lvl < tlvl) {
          break;
        } else if (toks[i].lvl == tlvl) {
          if (toks[i].type == Pop) {
            break;
          } else if (toks[i].type == Branch) {
            ++i;
            ssize_p = ssize;
            if (!unroll_rec(toks + i, size - i, tlvl))
              return false;
            ssize = ssize_p; // restore stack size
          }
        }
      }
      return true;
    }

    if (toks[i].type == Pop || toks[i].type == Branch) {
      if (toks[i].lvl == lvl) {
        print_stack();
        return true;
      } else {
        continue;
      }
    }

    if (ssize >= STACK_SIZE)
      ERROR("stack overflow");
    stack[ssize++] = &toks[i];
  }

  print_stack();
  return true;
}


bool match_autocmd(const char *str)
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

bool match_events(const char *str)
{
#define EVENT_NAME_SIZE (16)
  char buf[EVENT_NAME_SIZE] = {0};
  const char *it = str;
  size_t i;

  bool bufnewfile = false;
  bool bufread = false;
  bool bufreadpost = false;

  while (*it != '\0') {
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
