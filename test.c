#include "auparser.h"
#include "bdd-for-c.h"
#include <assert.h>

static token_t toks[512];
static size_t size = 512;

typedef struct {
  type_t type;
  const char *str;
  int lvl;
} tcase_t;

static bool trun(const char *pat) {
  size = 512;
  if (!tokenize(pat, toks, &size)) {
    // fprintf(stderr, "tokenize failed: %s\n", error);
    return false;
  }
  return true;
}

static bool tcheck(const char *input, tcase_t *expected) {
  size = 512;
  if (!tokenize(input, toks, &size)) {
    fprintf(stderr, "tokenizing failed: %s\n", error);
    return false;
  }
  size_t i = 0;
  for (; expected[i].str != NULL; ++i) {
    if (i >= size) {
      fprintf(stderr, "token %ld out of bounds\n", i);
      return false;
    }
    token_t *t = &toks[i];
    tcase_t *c = &expected[i];
    if (t->type != c->type) {
      fprintf(stderr, "got type %s, expected %s\n", type_str(t->type), type_str(c->type));
      return false;
    }
    size_t len = strlen(c->str);
    if (t->len != len || strncmp(t->beg, c->str, len) != 0) {
      char buf[256] = {0};
      assert(t->len < 255);
      memcpy(buf, t->beg, t->len);
      fprintf(stderr, "got string '%s', expected '%s'\n", buf, c->str);
      return false;
    }
    if (t->lvl != c->lvl) {
      fprintf(stderr, "got level %d, expected %d\n", t->lvl, c->lvl);
      return false;
    }
  }
  if (size != i) {
    fprintf(stderr, "got size %ld, expected %ld\n", size, i);
    return false;
  }
  return true;
}

spec("tokenizer")
{
  it("should tokenize literals") {
    check(tcheck("a", (tcase_t[]){
      { Literal, "a", 0 },
      { 0, 0, 0 },
    }));
    check(tcheck("abc", (tcase_t[]){
      { Literal, "abc", 0 },
      { 0, 0, 0 },
    }));
    check(tcheck("\\,\\?\\{\\}", (tcase_t[]){
      { Literal, "\\,\\?\\{\\}", 0 },
      { 0, 0, 0 },
    }));
    check(tcheck("a\\,b\\?c", (tcase_t[]){
      { Literal, "a\\,b\\?c", 0 },
      { 0, 0, 0 },
    }));
  }

  it("should tokenize * and ?") {
    check(tcheck("*", (tcase_t[]){
      { AnyChars, "*", 0 },
      { 0, 0, 0 },
    }));
    check(tcheck("?", (tcase_t[]){
      { AnyChar, "?", 0 },
      { 0, 0, 0 },
    }));
    check(tcheck("*?", (tcase_t[]){
      { AnyChars, "*", 0 },
      { AnyChar, "?", 0 },
      { 0, 0, 0 },
    }));
  }

  it("should tokenize \\*, \\+ and \\=") {
    check(tcheck("\\*", (tcase_t[]){
      { ZeroOrMore, "\\*", 0 },
      { 0, 0, 0 },
    }));
    check(tcheck("\\+", (tcase_t[]){
      { OneOrMore, "\\+", 0 },
      { 0, 0, 0 },
    }));
    check(tcheck("\\=", (tcase_t[]){
      { ZeroOrOne, "\\=", 0 },
      { 0, 0, 0 },
    }));
    check(tcheck("\\*\\+\\=", (tcase_t[]){
      { ZeroOrMore, "\\*", 0 },
      { OneOrMore, "\\+", 0 },
      { ZeroOrOne, "\\=", 0 },
      { 0, 0, 0 },
    }));
  }

  it("should tokenize character sets") {
    check(tcheck("[a]", (tcase_t[]){
      { Set, "[a]", 0 },
      { 0, 0, 0 },
    }));
    check(tcheck("[abc]", (tcase_t[]){
      { Set, "[abc]", 0 },
      { 0, 0, 0 },
    }));
    check(tcheck("[A-Za-z0-9]", (tcase_t[]){
      { Set, "[A-Za-z0-9]", 0 },
      { 0, 0, 0 },
    }));
    check(tcheck("[[:digit:]]", (tcase_t[]){
      { Set, "[[:digit:]]", 0 },
      { 0, 0, 0 },
    }));
    check(tcheck("[-_]", (tcase_t[]){
      { Set, "[-_]", 0 },
      { 0, 0, 0 },
    }));
    check(tcheck("[^a]", (tcase_t[]){
      { Set, "[^a]", 0 },
      { 0, 0, 0 },
    }));
    check(tcheck("[^abc]", (tcase_t[]){
      { Set, "[^abc]", 0 },
      { 0, 0, 0 },
    }));
    check(tcheck("[^[:digit:]]", (tcase_t[]){
      { Set, "[^[:digit:]]", 0 },
      { 0, 0, 0 },
    }));
    check(tcheck("[abc][^abc][A-Z[:digit:]-_]", (tcase_t[]){
      { Set, "[abc]", 0 },
      { Set, "[^abc]", 0 },
      { Set, "[A-Z[:digit:]-_]", 0 },
      { 0, 0, 0 },
    }));
  }

  it("should tokenize character classes") {
    check(tcheck("\\d", (tcase_t[]){
      { Cls, "\\d", 0 },
      { 0, 0, 0 },
    }));
    check(tcheck("\\d\\d\\d", (tcase_t[]){
      { Cls, "\\d", 0 },
      { Cls, "\\d", 0 },
      { Cls, "\\d", 0 },
      { 0, 0, 0 },
    }));
  }

  it("should fail to tokenize invalid character sets") {
    check(!trun("["));
    check(!trun("[^"));
    check(!trun("[[]"));
    check(!trun("[^[]"));
    check(!trun("[[[]]]"));
    check(!trun("[^[[]]]"));
    check(!trun("[[^[]]]"));
    check(!trun("[[[^]]]"));
  }

  it("should fail to tokenize plain backslashes") {
    check(!trun("\\"));
    check(!trun("\\\\"));
    check(!trun("\\\\\\"));
    check(!trun("\\\\\\\\"));
  }

  describe("branching") {
    it("should tokenize branches at the root level") {
      check(tcheck("a,b", (tcase_t[]){
        { Literal, "a", 0 },
        { Branch, ",", 0 },
        { Literal, "b", 0 },
        { 0, 0, 0 },
      }));
      check(tcheck("a,b,c", (tcase_t[]){
        { Literal, "a", 0 },
        { Branch, ",", 0 },
        { Literal, "b", 0 },
        { Branch, ",", 0 },
        { Literal, "c", 0 },
        { 0, 0, 0 },
      }));
    }

    it("should insert empty tokens at the root level") {
      check(tcheck("a,,c", (tcase_t[]){
        { Literal, "a", 0 },
        { Branch, ",", 0 },
        { Empty, "", 0 },
        { Branch, ",", 0 },
        { Literal, "c", 0 },
        { 0, 0, 0 },
      }));
      check(tcheck("a,b,", (tcase_t[]){
        { Literal, "a", 0 },
        { Branch, ",", 0 },
        { Literal, "b", 0 },
        { Branch, ",", 0 },
        // { Empty, "", 0 },
        { 0, 0, 0 },
      }));
      check(tcheck(",b,c", (tcase_t[]){
        // { Empty, "", 0 },
        { Branch, ",", 0 },
        { Literal, "b", 0 },
        { Branch, ",", 0 },
        { Literal, "c", 0 },
        { 0, 0, 0 },
      }));
    }

    it("should tokenize child branches") {
      check(tcheck("{a}", (tcase_t[]){
        { Push, "{", 1 },
        { Literal, "a", 1 },
        { Pop, "}", 1 },
        { 0, 0, 0 },
      }));
      check(tcheck("{a,b}", (tcase_t[]){
        { Push, "{", 1 },
        { Literal, "a", 1 },
        { Branch, ",", 1 },
        { Literal, "b", 1 },
        { Pop, "}", 1 },
        { 0, 0, 0 },
      }));
      check(tcheck("a{b}c", (tcase_t[]){
        { Literal, "a", 0 },
        { Push, "{", 1 },
        { Literal, "b", 1 },
        { Pop, "}", 1 },
        { Literal, "c", 0 },
        { 0, 0, 0 },
      }));
      check(tcheck("{a}b{c}", (tcase_t[]){
        { Push, "{", 1 },
        { Literal, "a", 1 },
        { Pop, "}", 1 },
        { Literal, "b", 0 },
        { Push, "{", 1 },
        { Literal, "c", 1 },
        { Pop, "}", 1 },
        { 0, 0, 0 },
      }));
      check(tcheck("{a}{b}", (tcase_t[]){
        { Push, "{", 1 },
        { Literal, "a", 1 },
        { Pop, "}", 1 },
        { Push, "{", 1 },
        { Literal, "b", 1 },
        { Pop, "}", 1 },
        { 0, 0, 0 },
      }));
      check(tcheck("{{{a}}}", (tcase_t[]){
        { Push, "{", 1 },
        { Push, "{", 2 },
        { Push, "{", 3 },
        { Literal, "a", 3 },
        { Pop, "}", 3 },
        { Pop, "}", 2 },
        { Pop, "}", 1 },
        { 0, 0, 0 },
      }));
      check(tcheck("{a{b,c}d}", (tcase_t[]){
        { Push, "{", 1 },
        { Literal, "a", 1 },
        { Push, "{", 2 },
        { Literal, "b", 2 },
        { Branch, ",", 2 },
        { Literal, "c", 2 },
        { Pop, "}", 2 },
        { Literal, "d", 1 },
        { Pop, "}", 1 },
        { 0, 0, 0 },
      }));
    }

    it("should insert empty tokens in child branches") {
      check(tcheck("{a,}", (tcase_t[]){
        { Push, "{", 1 },
        { Literal, "a", 1 },
        { Branch, ",", 1 },
        { Empty, "", 1 },
        { Pop, "}", 1 },
        { 0, 0, 0 },
      }));
      check(tcheck("{,a}", (tcase_t[]){
        { Push, "{", 1 },
        { Empty, "", 1 },
        { Branch, ",", 1 },
        { Literal, "a", 1 },
        { Pop, "}", 1 },
        { 0, 0, 0 },
      }));
      check(tcheck("{a,,b}", (tcase_t[]){
        { Push, "{", 1 },
        { Literal, "a", 1 },
        { Branch, ",", 1 },
        { Empty, "", 1 },
        { Branch, ",", 1 },
        { Literal, "b", 1 },
        { Pop, "}", 1 },
        { 0, 0, 0 },
      }));
    }

    it("should fail to tokenize unmatched brackets") {
      check(!trun("{"));
      check(!trun("}"));
      check(!trun("{}{"));
      check(!trun("{}}"));
      check(!trun("{{}"));
      check(!trun("}{}"));
    }

    it("should tokenize vim regex groups") {
      check(tcheck("\\(a\\)", (tcase_t[]){
        { Push, "\\(", 1 },
        { Literal, "a", 1 },
        { Pop, "\\)", 1 },
        { 0, 0, 0 },
      }));
      check(tcheck("\\(a\\|b\\)", (tcase_t[]){
        { Push, "\\(", 1 },
        { Literal, "a", 1 },
        { Branch, "\\|", 1 },
        { Literal, "b", 1 },
        { Pop, "\\)", 1 },
        { 0, 0, 0 },
      }));
    }
  }

  it("should tokenize vim regex options") {
    check(tcheck("\\c\\C", (tcase_t[]){
      { Opts, "\\c", 0 },
      { Opts, "\\C", 0 },
      { 0, 0, 0 },
    }));
  }

  it("should tokenize vim regex count") {
    check(tcheck("\\\\\\{\\}", (tcase_t[]){
      { Count, "\\\\\\{\\}", 0 },
      { 0, 0, 0 },
    }));
    check(tcheck("\\\\\\{1\\}", (tcase_t[]){
      { Count, "\\\\\\{1\\}", 0 },
      { 0, 0, 0 },
    }));
    check(tcheck("\\\\\\{1,\\}", (tcase_t[]){
      { Count, "\\\\\\{1,\\}", 0 },
      { 0, 0, 0 },
    }));
    check(tcheck("\\\\\\{,1\\}", (tcase_t[]){
      { Count, "\\\\\\{,1\\}", 0 },
      { 0, 0, 0 },
    }));
    check(tcheck("\\\\\\{1,1\\}", (tcase_t[]){
      { Count, "\\\\\\{1,1\\}", 0 },
      { 0, 0, 0 },
    }));
    check(tcheck("\\\\\\{-\\}", (tcase_t[]){
      { Count, "\\\\\\{-\\}", 0 },
      { 0, 0, 0 },
    }));
    check(tcheck("\\\\\\{-1\\}", (tcase_t[]){
      { Count, "\\\\\\{-1\\}", 0 },
      { 0, 0, 0 },
    }));
    check(tcheck("\\\\\\{-1,\\}", (tcase_t[]){
      { Count, "\\\\\\{-1,\\}", 0 },
      { 0, 0, 0 },
    }));
    check(tcheck("\\\\\\{-,1\\}", (tcase_t[]){
      { Count, "\\\\\\{-,1\\}", 0 },
      { 0, 0, 0 },
    }));
    check(tcheck("\\\\\\{-1,1\\}", (tcase_t[]){
      { Count, "\\\\\\{-1,1\\}", 0 },
      { 0, 0, 0 },
    }));
  }

  it("should fail to tokenize invalid vim regex count") {
    check(!trun("\\\\\\{a\\}"));
    check(!trun("\\\\\\{+\\}"));
    check(!trun("\\\\\\{\\"));
    check(!trun("\\\\\\{"));
    check(!trun("\\\\\\{}"));
  }
}
