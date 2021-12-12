#include "auparser.h"
#include "bdd-for-c.h"
#include <assert.h>

typedef struct {
  type_t type;
  const char *str;
  int lvl;
} tok_case;

static bool tok_fail(const char *pat)
{
  token_t *toks = NULL;
  if (!tokenize(pat, &toks))
    return false;
  free(toks);
  return true;
}

static bool tok_ok(const char *input, tok_case *expected)
{
  token_t *toks = NULL;
  size_t size = tokenize(input, &toks);
  if (!size) {
    fprintf(stderr, "tokenizing failed: %s\n", error);
    return false;
  }

  size_t i;
  for (i = 0; expected[i].str != NULL; ++i) {
    if (i >= size) {
      fprintf(stderr, "token %ld out of bounds\n", i);
      goto ret_err;
    }

    const token_t *t = &toks[i];
    const tok_case *c = &expected[i];

    if (t->type != c->type) {
      fprintf(stderr, "got type %s, expected %s\n", type_str(t->type), type_str(c->type));
      goto ret_err;
    }

    size_t len = strlen(c->str);
    if (t->len != len || strncmp(t->beg, c->str, len) != 0) {
      char buf[256] = {0};
      assert(t->len < 255);
      memcpy(buf, t->beg, t->len);
      fprintf(stderr, "got string '%s', expected '%s'\n", buf, c->str);
      goto ret_err;
    }

    if (t->lvl != c->lvl) {
      fprintf(stderr, "got level %d, expected %d\n", t->lvl, c->lvl);
      goto ret_err;
    }
  }

  if (size != i) {
    fprintf(stderr, "got size %ld, expected %ld\n", size, i);
    goto ret_err;
  }

  free(toks);
  return true;
ret_err:
  free(toks);
  return false;
}

spec("tokenizer")
{
  it("should tokenize literals") {
    check(tok_ok("a", (tok_case[]){
      { Literal, "a", 0 },
      { 0, 0, 0 },
    }));
    check(tok_ok("abc", (tok_case[]){
      { Literal, "abc", 0 },
      { 0, 0, 0 },
    }));
    check(tok_ok("\\,\\?\\{\\}", (tok_case[]){
      { Literal, "\\,\\?\\{\\}", 0 },
      { 0, 0, 0 },
    }));
    check(tok_ok("a\\,b\\?c", (tok_case[]){
      { Literal, "a\\,b\\?c", 0 },
      { 0, 0, 0 },
    }));
  }

  it("should tokenize * and ?") {
    check(tok_ok("*", (tok_case[]){
      { AnyChars, "*", 0 },
      { 0, 0, 0 },
    }));
    check(tok_ok("?", (tok_case[]){
      { AnyChar, "?", 0 },
      { 0, 0, 0 },
    }));
    check(tok_ok("*?", (tok_case[]){
      { AnyChars, "*", 0 },
      { AnyChar, "?", 0 },
      { 0, 0, 0 },
    }));
  }

  it("should tokenize \\*, \\+ and \\=") {
    check(tok_ok("\\*", (tok_case[]){
      { ZeroOrMore, "\\*", 0 },
      { 0, 0, 0 },
    }));
    check(tok_ok("\\+", (tok_case[]){
      { OneOrMore, "\\+", 0 },
      { 0, 0, 0 },
    }));
    check(tok_ok("\\=", (tok_case[]){
      { ZeroOrOne, "\\=", 0 },
      { 0, 0, 0 },
    }));
    check(tok_ok("\\*\\+\\=", (tok_case[]){
      { ZeroOrMore, "\\*", 0 },
      { OneOrMore, "\\+", 0 },
      { ZeroOrOne, "\\=", 0 },
      { 0, 0, 0 },
    }));
  }

  it("should tokenize character sets") {
    check(tok_ok("[a]", (tok_case[]){
      { Set, "[a]", 0 },
      { 0, 0, 0 },
    }));
    check(tok_ok("[abc]", (tok_case[]){
      { Set, "[abc]", 0 },
      { 0, 0, 0 },
    }));
    check(tok_ok("[A-Za-z0-9]", (tok_case[]){
      { Set, "[A-Za-z0-9]", 0 },
      { 0, 0, 0 },
    }));
    check(tok_ok("[[:digit:]]", (tok_case[]){
      { Set, "[[:digit:]]", 0 },
      { 0, 0, 0 },
    }));
    check(tok_ok("[-_]", (tok_case[]){
      { Set, "[-_]", 0 },
      { 0, 0, 0 },
    }));
    check(tok_ok("[^a]", (tok_case[]){
      { Set, "[^a]", 0 },
      { 0, 0, 0 },
    }));
    check(tok_ok("[^abc]", (tok_case[]){
      { Set, "[^abc]", 0 },
      { 0, 0, 0 },
    }));
    check(tok_ok("[^[:digit:]]", (tok_case[]){
      { Set, "[^[:digit:]]", 0 },
      { 0, 0, 0 },
    }));
    check(tok_ok("[abc][^abc][A-Z[:digit:]-_]", (tok_case[]){
      { Set, "[abc]", 0 },
      { Set, "[^abc]", 0 },
      { Set, "[A-Z[:digit:]-_]", 0 },
      { 0, 0, 0 },
    }));
  }

  it("should tokenize character classes") {
    check(tok_ok("\\d", (tok_case[]){
      { Cls, "\\d", 0 },
      { 0, 0, 0 },
    }));
    check(tok_ok("\\d\\d\\d", (tok_case[]){
      { Cls, "\\d", 0 },
      { Cls, "\\d", 0 },
      { Cls, "\\d", 0 },
      { 0, 0, 0 },
    }));
  }

  it("should fail to tokenize invalid character sets") {
    check(!tok_fail("["));
    check(!tok_fail("[^"));
    check(!tok_fail("[[]"));
    check(!tok_fail("[^[]"));
    check(!tok_fail("[[[]]]"));
    check(!tok_fail("[^[[]]]"));
    check(!tok_fail("[[^[]]]"));
    check(!tok_fail("[[[^]]]"));
  }

  it("should fail to tokenize plain backslashes") {
    check(!tok_fail("\\"));
    check(!tok_fail("\\\\"));
    check(!tok_fail("\\\\\\"));
    check(!tok_fail("\\\\\\\\"));
  }

  describe("branching") {
    it("should tokenize branches at the root level") {
      check(tok_ok("a,b", (tok_case[]){
        { Literal, "a", 0 },
        { Branch, ",", 0 },
        { Literal, "b", 0 },
        { 0, 0, 0 },
      }));
      check(tok_ok("a,b,c", (tok_case[]){
        { Literal, "a", 0 },
        { Branch, ",", 0 },
        { Literal, "b", 0 },
        { Branch, ",", 0 },
        { Literal, "c", 0 },
        { 0, 0, 0 },
      }));
    }

    it("should insert empty tokens at the root level") {
      check(tok_ok("a,,c", (tok_case[]){
        { Literal, "a", 0 },
        { Branch, ",", 0 },
        { Empty, "", 0 },
        { Branch, ",", 0 },
        { Literal, "c", 0 },
        { 0, 0, 0 },
      }));
      check(tok_ok("a,b,", (tok_case[]){
        { Literal, "a", 0 },
        { Branch, ",", 0 },
        { Literal, "b", 0 },
        { Branch, ",", 0 },
        // { Empty, "", 0 },
        { 0, 0, 0 },
      }));
      check(tok_ok(",b,c", (tok_case[]){
        // { Empty, "", 0 },
        { Branch, ",", 0 },
        { Literal, "b", 0 },
        { Branch, ",", 0 },
        { Literal, "c", 0 },
        { 0, 0, 0 },
      }));
    }

    it("should tokenize child branches") {
      check(tok_ok("{a}", (tok_case[]){
        { Push, "{", 1 },
        { Literal, "a", 1 },
        { Pop, "}", 1 },
        { 0, 0, 0 },
      }));
      check(tok_ok("{a,b}", (tok_case[]){
        { Push, "{", 1 },
        { Literal, "a", 1 },
        { Branch, ",", 1 },
        { Literal, "b", 1 },
        { Pop, "}", 1 },
        { 0, 0, 0 },
      }));
      check(tok_ok("a{b}c", (tok_case[]){
        { Literal, "a", 0 },
        { Push, "{", 1 },
        { Literal, "b", 1 },
        { Pop, "}", 1 },
        { Literal, "c", 0 },
        { 0, 0, 0 },
      }));
      check(tok_ok("{a}b{c}", (tok_case[]){
        { Push, "{", 1 },
        { Literal, "a", 1 },
        { Pop, "}", 1 },
        { Literal, "b", 0 },
        { Push, "{", 1 },
        { Literal, "c", 1 },
        { Pop, "}", 1 },
        { 0, 0, 0 },
      }));
      check(tok_ok("{a}{b}", (tok_case[]){
        { Push, "{", 1 },
        { Literal, "a", 1 },
        { Pop, "}", 1 },
        { Push, "{", 1 },
        { Literal, "b", 1 },
        { Pop, "}", 1 },
        { 0, 0, 0 },
      }));
      check(tok_ok("{{{a}}}", (tok_case[]){
        { Push, "{", 1 },
        { Push, "{", 2 },
        { Push, "{", 3 },
        { Literal, "a", 3 },
        { Pop, "}", 3 },
        { Pop, "}", 2 },
        { Pop, "}", 1 },
        { 0, 0, 0 },
      }));
      check(tok_ok("{a{b,c}d}", (tok_case[]){
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
      check(tok_ok("{a,}", (tok_case[]){
        { Push, "{", 1 },
        { Literal, "a", 1 },
        { Branch, ",", 1 },
        { Empty, "", 1 },
        { Pop, "}", 1 },
        { 0, 0, 0 },
      }));
      check(tok_ok("{,a}", (tok_case[]){
        { Push, "{", 1 },
        { Empty, "", 1 },
        { Branch, ",", 1 },
        { Literal, "a", 1 },
        { Pop, "}", 1 },
        { 0, 0, 0 },
      }));
      check(tok_ok("{a,,b}", (tok_case[]){
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
      check(!tok_fail("{"));
      check(!tok_fail("}"));
      check(!tok_fail("{}{"));
      check(!tok_fail("{}}"));
      check(!tok_fail("{{}"));
      check(!tok_fail("}{}"));
    }

    it("should tokenize vim regex groups") {
      check(tok_ok("\\(a\\)", (tok_case[]){
        { Push, "\\(", 1 },
        { Literal, "a", 1 },
        { Pop, "\\)", 1 },
        { 0, 0, 0 },
      }));
      check(tok_ok("\\(a\\|b\\)", (tok_case[]){
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
    check(tok_ok("\\c\\C", (tok_case[]){
      { Opts, "\\c", 0 },
      { Opts, "\\C", 0 },
      { 0, 0, 0 },
    }));
  }

  it("should tokenize vim regex count") {
    check(tok_ok("\\\\\\{\\}", (tok_case[]){
      { Count, "\\\\\\{\\}", 0 },
      { 0, 0, 0 },
    }));
    check(tok_ok("\\\\\\{1\\}", (tok_case[]){
      { Count, "\\\\\\{1\\}", 0 },
      { 0, 0, 0 },
    }));
    check(tok_ok("\\\\\\{1,\\}", (tok_case[]){
      { Count, "\\\\\\{1,\\}", 0 },
      { 0, 0, 0 },
    }));
    check(tok_ok("\\\\\\{,1\\}", (tok_case[]){
      { Count, "\\\\\\{,1\\}", 0 },
      { 0, 0, 0 },
    }));
    check(tok_ok("\\\\\\{1,1\\}", (tok_case[]){
      { Count, "\\\\\\{1,1\\}", 0 },
      { 0, 0, 0 },
    }));
    check(tok_ok("\\\\\\{-\\}", (tok_case[]){
      { Count, "\\\\\\{-\\}", 0 },
      { 0, 0, 0 },
    }));
    check(tok_ok("\\\\\\{-1\\}", (tok_case[]){
      { Count, "\\\\\\{-1\\}", 0 },
      { 0, 0, 0 },
    }));
    check(tok_ok("\\\\\\{-1,\\}", (tok_case[]){
      { Count, "\\\\\\{-1,\\}", 0 },
      { 0, 0, 0 },
    }));
    check(tok_ok("\\\\\\{-,1\\}", (tok_case[]){
      { Count, "\\\\\\{-,1\\}", 0 },
      { 0, 0, 0 },
    }));
    check(tok_ok("\\\\\\{-1,1\\}", (tok_case[]){
      { Count, "\\\\\\{-1,1\\}", 0 },
      { 0, 0, 0 },
    }));
  }

  it("should fail to tokenize invalid vim regex count") {
    check(!tok_fail("\\\\\\{a\\}"));
    check(!tok_fail("\\\\\\{+\\}"));
    check(!tok_fail("\\\\\\{1.\\}"));
    check(!tok_fail("\\\\\\{\\"));
    check(!tok_fail("\\\\\\{"));
    check(!tok_fail("\\\\\\{}"));
  }
}
