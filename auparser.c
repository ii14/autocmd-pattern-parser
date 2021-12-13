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
    CASE(End);
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


/// Error message
const char *error = NULL;

#define ERROR(msg) \
  do { \
    error = (msg); \
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

void print_tokens(const token_t **toks)
{
  char buf[256];
  size_t n = 0;
  for (const token_t **p = toks; *p != NULL; ++p) {
    const token_t *tok = *p;
    if (n + tok->len < 255) {
      memcpy(buf + n, tok->beg, tok->len);
      n += tok->len;
    } else {
      fprintf(stderr, "(range too long)\n");
      return;
    }
  }
  buf[n] = '\0';
  fprintf(stdout, "%s\n", buf);
}


token_t *tokenize(const char *pat)
{
#define ERR(msg) \
  do { \
    error = (msg); \
    free(toks); \
    return NULL; \
  } while (0)

#define PUSH(TYPE, BEGIN, LEN) \
  do { \
    if (size >= cap) { \
      cap *= 2; \
      token_t *ntoks = realloc(toks, cap * sizeof(token_t)); \
      if (ntoks == NULL) \
        ERR("realloc"); \
      toks = ntoks; \
    } \
    toks[size++] = (token_t){ \
      .type=(TYPE), .beg=(BEGIN), .len=(LEN), .lvl=0 \
    }; \
    ++pushed; \
  } while (0)

  size_t size = 0;
  size_t cap = 64;
  token_t *toks = malloc(cap * sizeof(token_t));
  if (toks == NULL)
    ERR("malloc");

  const char *it;
  size_t pushed = 0;
  const char *literal = NULL;
  const char *beg = pat;
  bool isliteral = false;

  for (it = pat; *it != '\0'; ++it) {
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
        ERR("unexpected end after '\\'");
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
          ERR("unexpected end after '_'");
        } else if (strchr(CHARACTER_CLASSES, *it)) {
          PUSH(Cls, beg2, 3);
        } else {
          ERR("unknown character class after '_'");
        }
      } else if (*it == '\\') {
        if (*(++it) == '\0') {
          ERR("unexpected end after '\\'");
        } else if (*it == '\\') {
          if (*(++it) == '\0') {
            ERR("unexpected end after '\\'");
          } else if (*it == '{') {
            // lua: {} (*), {-} (-), {n} {-n} (unroll)
            // vim: {n,m} {n,} {,m} {-n,m} {-n,} {-,m}
            if (*(++it) == '\0')
              ERR("unexpected end after '{'");
            if (*it == '-')
              ++it;
            while (isdigit(*it))
              ++it;
            if (*it == ',')
              ++it;
            while (isdigit(*it))
              ++it;
            if (*it != '\\')
              ERR("invalid '{}' atom");
            if (*(++it) != '}')
              ERR("invalid '{}' atom");
            PUSH(Count, beg2, it - beg2 + 1);
          } else {
            ERR("unknown escape sequence");
          }
        } else {
          ERR("unknown escape sequence");
        }
      } else if (*it == '{') {
        ERR("unknown regex pattern '\\{'");
      } else if (strchr("cCZmMvV", *it)) {
        // vim regex settings. force vim pattern
        PUSH(Opts, beg2, 2);
      } else {
        ERR("unknown regex pattern");
      }
    } else if (*it == '[') {
      // character set ([abc])
      const char *beg2 = it;
      if (*(++it) == '\0')
        ERR("unclosed '['");
      if (*it == '^') // negated character set ([^abc])
        ++it;
      for (bool nested = false;; ++it) {
        if (*it == '\0') {
          ERR("unclosed '['");
        } else if (*it == '[') {
          if (nested)
            ERR("unexpected '['");
          nested = true;
        } else if (*it == ']') {
          if (nested) {
            nested = false;
          } else {
            PUSH(Set, beg2, it - beg2 + 1);
            break;
          }
        } else if (!isalnum(*it) && !strchr("-_.:", *it)) {
          ERR("character from character set not supported");
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
      ERR("not a literal and nothing was pushed");
    } else if (isliteral && literal == NULL) {
      literal = beg;
    } else if (!isliteral) {
      // push literals
      if (literal != NULL) {
        if (pushed > 0) {
          // since we're pushing the literal after we
          // got a non-literal, swap with last token
          if (pushed != 1)
            ERR("pushed != 1");
          token_t copy = toks[--size];
          PUSH(Literal, literal, beg - literal);
          PUSH(copy.type, copy.beg, copy.len);
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
          token_t copy = toks[--size];
          PUSH(Empty, "", 0);
          PUSH(copy.type, copy.beg, copy.len);
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
      token_t copy = toks[--size];
      PUSH(Empty, "", 0);
      PUSH(copy.type, copy.beg, copy.len);
    }
  }

  {
    // set levels
    int lvl = 0;
    for (size_t j = 0; j < size; ++j) {
      if (toks[j].type == Push) {
        toks[j].lvl = ++lvl;
      } else if (toks[j].type == Pop) {
        toks[j].lvl = lvl--;
        if (lvl < 0)
          ERR("unexpected branch close");
      } else {
        toks[j].lvl = lvl;
      }
    }
    if (lvl != 0) {
      ERR("unclosed branch");
    }
  }

  PUSH(End, NULL, 0);
  return toks;

#undef PUSH
#undef ERR
}


// unroll stack
#define USTACK_SIZE (256)
static const token_t *ustack[USTACK_SIZE] = {0};
static size_t ussize = 0;
// unroll results
static const token_t ***ures = NULL;
static size_t ures_cap = 0;
static size_t ures_size = 0;

static bool unroll_rec(const token_t *toks, int lvl)
{
  if (!toks->type)
    return true;
  if (lvl > 8)
    ERROR("pattern too deeply nested");

  bool left = false; // left current branch
  size_t ssize_p; // saved stack size
  int tlvl; // temporary level

  for (const token_t *it = toks; it->type; ++it) {
    // if below current level, we for sure already left the current branch.
    // we need to keep track of this, because there could be other branches
    // with the same level later, eg. `{a,b}c{d,e}`
    if (it->lvl < lvl)
      left = true;

    // skip other branches for the current level
    if (!left && it->lvl == lvl) {
      if (it->type == Branch) {
        // another branch for the current level, skip it
        while (it->type && it->lvl >= lvl && !(it->lvl == lvl && it->type == Pop))
          ++it;
        --it;
        continue;
      } else if (it->type == Pop) {
        // pop for current level, we left the current branch
        left = true;
        continue;
      }
    }

    // unroll every branch we encounter
    if (it->type == Push) {
      tlvl = it->lvl;
      ++it;
      ssize_p = ussize;
      if (!unroll_rec(it, tlvl))
        return false;
      ussize = ssize_p; // restore stack
      for (; it->type; ++it) {
        if (it->lvl < tlvl)
          break;
        if (it->lvl == tlvl) {
          if (it->type == Pop)
            break;
          if (it->type == Branch) {
            ++it;
            ssize_p = ussize;
            if (!unroll_rec(it, tlvl))
              return false;
            ussize = ssize_p; // restore stack
          }
        }
      }
      return true;
    }

    if (it->type == Branch) {
      if (it ->lvl <= lvl)
        break;
      continue;
    }

    if (it->type == Pop) {
      // break out from branches at the current level,
      // they're handled by recursive calls
      if (it->lvl == lvl)
        break;
      continue;
    }

    if (ussize >= USTACK_SIZE)
      ERROR("stack overflow");
    ustack[ussize++] = it;
  }

  // ignore empty branches on root level
  if (lvl == 0) {
    bool isempty = true;
    for (size_t i = 0; i < ussize; ++i) {
      if (ustack[i]->type != Empty)
        isempty = false;
    }
    if (isempty)
      return true;
  }

  // resize result array if necessary
  if (ures_size >= ures_cap) {
    ures_cap *= 2;
    const token_t ***nures = realloc(ures, ures_cap * sizeof(const token_t**));
    if (nures == NULL)
      ERROR("realloc");
    ures = nures;
  }

  // write current stack state to results
  const token_t **buf = malloc((ussize + 1) * sizeof(const token_t*));
  if (buf == NULL)
    ERROR("malloc");
  memcpy(buf, ustack, ussize * sizeof(const token_t*));
  buf[ussize] = NULL;
  ures[ures_size++] = buf;
  return true;
}

const token_t ***unroll(const token_t *toks)
{
  if (!toks->type) {
    error = "pattern is empty";
    return NULL;
  }

  // reset stack state
  ures_size = 0;
  ures_cap = 16;
  ures = malloc(ures_cap * sizeof(const token_t**));
  if (ures == NULL) {
    error = "malloc";
    return NULL;
  }

  const token_t *it = toks;
  const token_t *beg = it; // branch start

  for (; it->type; ++it) {
    // look for , at the root level
    if (it->lvl == 0 && it->type == Branch) {
      ussize = 0; // clean stack
      if (!unroll_rec(beg, 0))
        goto fail;
      beg = it + 1;
    }
  }

  // parse last branch
  ussize = 0; // clean stack
  if (!unroll_rec(beg, 0))
    goto fail;

  // resize array for the null pointer if necessary
  if (ures_size >= ures_cap) {
    ++ures_cap;
    const token_t ***nures = realloc(ures, ures_cap * sizeof(const token_t**));
    if (nures == NULL) {
      error = "realloc";
      goto fail;
    }
    ures = nures;
  }

  // write null pointer at the end
  ures[ures_size] = NULL;
  return ures;

fail:
  for (size_t i = 0; i < ures_size; ++i)
    free(ures[i]);
  free(ures);
  return NULL;
}

void free_tokens(const token_t ***toks)
{
  for (const token_t ***p = toks; *p != NULL; ++p)
    free(*p);
  free(toks);
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


int write_escaped(char *out, size_t max, const char *str, size_t len)
{
  size_t n = 0;
  for (size_t i = 0; i < len; ++i) {
    char c = str[i];
    if (c == '\\' || c == '"') {
      if (n + 1 >= max - 1) {
        fprintf(stderr, "value too long\n");
        return -1;
      }
      out[n++] = '\\';
    }
    if (n + 1 >= max - 1) {
      fprintf(stderr, "value too long\n");
      return -1;
    }
    out[n++] = str[i];
  }
  out[n] = '\0';
  return n;
}
