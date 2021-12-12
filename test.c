#include "auparser.h"
#include "bdd-for-c.h"
#include <assert.h>

typedef struct {
  type_t type;
  const char *input;
  int lvl;
} tok_case;

#define END_CASES { 0, NULL, 0 }

static bool tok_fail(const char *pat)
{
  token_t *tokens = tokenize(pat);
  if (tokens == NULL)
    return true;
  free(tokens);
  return false;
}

static bool tok_ok(const char *input, tok_case *expected)
{
  token_t *tokens = tokenize(input);
  if (tokens == NULL) {
    fprintf(stderr, "tokenizing failed: %s\n", error);
    return false;
  }

  for (size_t i = 0;; ++i) {
    const token_t *t = &tokens[i];
    const tok_case *c = &expected[i];

    if (c->input == NULL) {
      if (t->type) {
        fprintf(stderr, "results longer than expected\n");
        goto fail;
      }
      break;
    } else if (!t->type) {
      fprintf(stderr, "results shorter than expected\n");
      goto fail;
    }

    if (t->type != c->type) {
      fprintf(stderr, "got type %s, expected %s at index %ld\n", type_str(t->type), type_str(c->type), i);
      goto fail;
    }

    size_t len = strlen(c->input);
    if (t->len != len || strncmp(t->beg, c->input, len) != 0) {
      char buf[256] = {0};
      assert(t->len < 255);
      memcpy(buf, t->beg, t->len);
      fprintf(stderr, "got string '%s', expected '%s' at index %ld\n", buf, c->input, i);
      goto fail;
    }

    if (t->lvl != c->lvl) {
      fprintf(stderr, "got level %d, expected %d at index %ld\n", t->lvl, c->lvl, i);
      goto fail;
    }
  }

  free(tokens);
  return true;

fail:
  free(tokens);
  return false;
}

static bool unroll_fail(const char *input)
{
  token_t *tokens = tokenize(input);
  if (tokens == NULL) {
    fprintf(stderr, "tokenizing failed: %s\n", error);
    return false;
  }

  const token_t ***res = unroll(tokens);
  if (res == NULL) {
    free(tokens);
    return true;
  }

  free_tokens(res);
  free(tokens);
  return false;
}

static bool unroll_ok(const char *input, const char **expected)
{
  token_t *tokens = tokenize(input);
  if (tokens == NULL) {
    fprintf(stderr, "tokenizing failed: %s\n", error);
    return false;
  }

  const token_t ***res = unroll(tokens);
  if (res == NULL) {
    fprintf(stderr, "unrolling failed: %s\n", error);
    free(tokens);
    return false;
  }

  for (size_t i = 0;; ++i) {
    if (expected[i] == NULL) {
      if (res[i] != NULL) {
        fprintf(stderr, "results longer than expected\n");
        goto fail;
      }
      break;
    } else if (res[i] == NULL) {
      fprintf(stderr, "results shorter than expected\n");
      goto fail;
    }

    char buf[256];
    size_t n = 0;
    for (const token_t **tok = res[i]; *tok != NULL; ++tok) {
      if (n + (*tok)->len < 256) {
        memcpy(buf + n, (*tok)->beg, (*tok)->len);
        n += (*tok)->len;
      } else {
        fprintf(stderr, "range too long\n");
        goto fail;
      }
    }
    buf[n] = '\0';

    size_t len = strlen(expected[i]);
    if (n != len || strncmp(buf, expected[i], len) != 0) {
      fprintf(stderr, "got string '%s', expected '%s' at index %ld\n", buf, expected[i], i);
      goto fail;
    }
  }

  free_tokens(res);
  free(tokens);
  return true;

fail:
  free_tokens(res);
  free(tokens);
  return false;
}

spec("auparser")
{
  describe("tokenize") {
    it("should tokenize literals") {
      check(tok_ok("a", (tok_case[]){
        { Literal, "a", 0 },
        END_CASES,
      }));
      check(tok_ok("abc", (tok_case[]){
        { Literal, "abc", 0 },
        END_CASES,
      }));
      check(tok_ok("\\,\\?\\{\\}", (tok_case[]){
        { Literal, "\\,\\?\\{\\}", 0 },
        END_CASES,
      }));
      check(tok_ok("a\\,b\\?c", (tok_case[]){
        { Literal, "a\\,b\\?c", 0 },
        END_CASES,
      }));
    }

    it("should tokenize * and ?") {
      check(tok_ok("*", (tok_case[]){
        { AnyChars, "*", 0 },
        END_CASES,
      }));
      check(tok_ok("?", (tok_case[]){
        { AnyChar, "?", 0 },
        END_CASES,
      }));
      check(tok_ok("*?", (tok_case[]){
        { AnyChars, "*", 0 },
        { AnyChar, "?", 0 },
        END_CASES,
      }));
    }

    it("should tokenize \\*, \\+ and \\=") {
      check(tok_ok("\\*", (tok_case[]){
        { ZeroOrMore, "\\*", 0 },
        END_CASES,
      }));
      check(tok_ok("\\+", (tok_case[]){
        { OneOrMore, "\\+", 0 },
        END_CASES,
      }));
      check(tok_ok("\\=", (tok_case[]){
        { ZeroOrOne, "\\=", 0 },
        END_CASES,
      }));
      check(tok_ok("\\*\\+\\=", (tok_case[]){
        { ZeroOrMore, "\\*", 0 },
        { OneOrMore, "\\+", 0 },
        { ZeroOrOne, "\\=", 0 },
        END_CASES,
      }));
    }

    it("should tokenize character sets") {
      check(tok_ok("[a]", (tok_case[]){
        { Set, "[a]", 0 },
        END_CASES,
      }));
      check(tok_ok("a[b]c", (tok_case[]){
        { Literal, "a", 0 },
        { Set, "[b]", 0 },
        { Literal, "c", 0 },
        END_CASES,
      }));
      check(tok_ok("[abc]", (tok_case[]){
        { Set, "[abc]", 0 },
        END_CASES,
      }));
      check(tok_ok("[A-Za-z0-9]", (tok_case[]){
        { Set, "[A-Za-z0-9]", 0 },
        END_CASES,
      }));
      check(tok_ok("[[:digit:]]", (tok_case[]){
        { Set, "[[:digit:]]", 0 },
        END_CASES,
      }));
      check(tok_ok("[-_]", (tok_case[]){
        { Set, "[-_]", 0 },
        END_CASES,
      }));
      check(tok_ok("[^a]", (tok_case[]){
        { Set, "[^a]", 0 },
        END_CASES,
      }));
      check(tok_ok("[^abc]", (tok_case[]){
        { Set, "[^abc]", 0 },
        END_CASES,
      }));
      check(tok_ok("[^[:digit:]]", (tok_case[]){
        { Set, "[^[:digit:]]", 0 },
        END_CASES,
      }));
      check(tok_ok("[abc][^abc][A-Z[:digit:]-_]", (tok_case[]){
        { Set, "[abc]", 0 },
        { Set, "[^abc]", 0 },
        { Set, "[A-Z[:digit:]-_]", 0 },
        END_CASES,
      }));
    }

    it("should tokenize character classes") {
      check(tok_ok("\\d", (tok_case[]){
        { Cls, "\\d", 0 },
        END_CASES,
      }));
      check(tok_ok("\\d\\d\\d", (tok_case[]){
        { Cls, "\\d", 0 },
        { Cls, "\\d", 0 },
        { Cls, "\\d", 0 },
        END_CASES,
      }));
    }

    it("should fail on invalid character sets") {
      check(tok_fail("["));
      check(tok_fail("[^"));
      check(tok_fail("[[]"));
      check(tok_fail("[^[]"));
      check(tok_fail("[[[]]]"));
      check(tok_fail("[^[[]]]"));
      check(tok_fail("[[^[]]]"));
      check(tok_fail("[[[^]]]"));
    }

    it("should fail on plain backslashes") {
      check(tok_fail("\\"));
      check(tok_fail("\\\\"));
      check(tok_fail("\\\\\\"));
      check(tok_fail("\\\\\\\\"));
    }

    describe("branching") {
      it("should tokenize branches at the root level") {
        check(tok_ok("a,b", (tok_case[]){
          { Literal, "a", 0 },
          { Branch, ",", 0 },
          { Literal, "b", 0 },
          END_CASES,
        }));
        check(tok_ok("a,b,c", (tok_case[]){
          { Literal, "a", 0 },
          { Branch, ",", 0 },
          { Literal, "b", 0 },
          { Branch, ",", 0 },
          { Literal, "c", 0 },
          END_CASES,
        }));
      }

      it("should insert empty tokens at the root level") {
        check(tok_ok("a,,c", (tok_case[]){
          { Literal, "a", 0 },
          { Branch, ",", 0 },
          { Empty, "", 0 },
          { Branch, ",", 0 },
          { Literal, "c", 0 },
          END_CASES,
        }));
        check(tok_ok("a,b,", (tok_case[]){
          { Literal, "a", 0 },
          { Branch, ",", 0 },
          { Literal, "b", 0 },
          { Branch, ",", 0 },
          // { Empty, "", 0 },
          END_CASES,
        }));
        check(tok_ok(",b,c", (tok_case[]){
          // { Empty, "", 0 },
          { Branch, ",", 0 },
          { Literal, "b", 0 },
          { Branch, ",", 0 },
          { Literal, "c", 0 },
          END_CASES,
        }));
      }

      it("should tokenize child branches") {
        check(tok_ok("{a}", (tok_case[]){
          { Push, "{", 1 },
          { Literal, "a", 1 },
          { Pop, "}", 1 },
          END_CASES,
        }));
        check(tok_ok("{a,b}", (tok_case[]){
          { Push, "{", 1 },
          { Literal, "a", 1 },
          { Branch, ",", 1 },
          { Literal, "b", 1 },
          { Pop, "}", 1 },
          END_CASES,
        }));
        check(tok_ok("a{b}c", (tok_case[]){
          { Literal, "a", 0 },
          { Push, "{", 1 },
          { Literal, "b", 1 },
          { Pop, "}", 1 },
          { Literal, "c", 0 },
          END_CASES,
        }));
        check(tok_ok("{a}b{c}", (tok_case[]){
          { Push, "{", 1 },
          { Literal, "a", 1 },
          { Pop, "}", 1 },
          { Literal, "b", 0 },
          { Push, "{", 1 },
          { Literal, "c", 1 },
          { Pop, "}", 1 },
          END_CASES,
        }));
        check(tok_ok("{a}{b}", (tok_case[]){
          { Push, "{", 1 },
          { Literal, "a", 1 },
          { Pop, "}", 1 },
          { Push, "{", 1 },
          { Literal, "b", 1 },
          { Pop, "}", 1 },
          END_CASES,
        }));
        check(tok_ok("{{{a}}}", (tok_case[]){
          { Push, "{", 1 },
          { Push, "{", 2 },
          { Push, "{", 3 },
          { Literal, "a", 3 },
          { Pop, "}", 3 },
          { Pop, "}", 2 },
          { Pop, "}", 1 },
          END_CASES,
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
          END_CASES,
        }));
      }

      it("should insert empty tokens in child branches") {
        check(tok_ok("{a,}", (tok_case[]){
          { Push, "{", 1 },
          { Literal, "a", 1 },
          { Branch, ",", 1 },
          { Empty, "", 1 },
          { Pop, "}", 1 },
          END_CASES,
        }));
        check(tok_ok("{,a}", (tok_case[]){
          { Push, "{", 1 },
          { Empty, "", 1 },
          { Branch, ",", 1 },
          { Literal, "a", 1 },
          { Pop, "}", 1 },
          END_CASES,
        }));
        check(tok_ok("{a,,b}", (tok_case[]){
          { Push, "{", 1 },
          { Literal, "a", 1 },
          { Branch, ",", 1 },
          { Empty, "", 1 },
          { Branch, ",", 1 },
          { Literal, "b", 1 },
          { Pop, "}", 1 },
          END_CASES,
        }));
      }

      it("should fail on unmatched brackets") {
        check(tok_fail("{"));
        check(tok_fail("}"));
        check(tok_fail("{}{"));
        check(tok_fail("{}}"));
        check(tok_fail("{{}"));
        check(tok_fail("}{}"));
      }

      it("should tokenize vim regex groups") {
        check(tok_ok("\\(a\\)", (tok_case[]){
          { Push, "\\(", 1 },
          { Literal, "a", 1 },
          { Pop, "\\)", 1 },
          END_CASES,
        }));
        check(tok_ok("\\(a\\|b\\)", (tok_case[]){
          { Push, "\\(", 1 },
          { Literal, "a", 1 },
          { Branch, "\\|", 1 },
          { Literal, "b", 1 },
          { Pop, "\\)", 1 },
          END_CASES,
        }));
      }
    }

    it("should tokenize vim regex options") {
      check(tok_ok("\\c\\C", (tok_case[]){
        { Opts, "\\c", 0 },
        { Opts, "\\C", 0 },
        END_CASES,
      }));
    }

    it("should tokenize vim regex count") {
      check(tok_ok("\\\\\\{\\}", (tok_case[]){
        { Count, "\\\\\\{\\}", 0 },
        END_CASES,
      }));
      check(tok_ok("\\\\\\{1\\}", (tok_case[]){
        { Count, "\\\\\\{1\\}", 0 },
        END_CASES,
      }));
      check(tok_ok("\\\\\\{1,\\}", (tok_case[]){
        { Count, "\\\\\\{1,\\}", 0 },
        END_CASES,
      }));
      check(tok_ok("\\\\\\{,1\\}", (tok_case[]){
        { Count, "\\\\\\{,1\\}", 0 },
        END_CASES,
      }));
      check(tok_ok("\\\\\\{1,1\\}", (tok_case[]){
        { Count, "\\\\\\{1,1\\}", 0 },
        END_CASES,
      }));
      check(tok_ok("\\\\\\{-\\}", (tok_case[]){
        { Count, "\\\\\\{-\\}", 0 },
        END_CASES,
      }));
      check(tok_ok("\\\\\\{-1\\}", (tok_case[]){
        { Count, "\\\\\\{-1\\}", 0 },
        END_CASES,
      }));
      check(tok_ok("\\\\\\{-1,\\}", (tok_case[]){
        { Count, "\\\\\\{-1,\\}", 0 },
        END_CASES,
      }));
      check(tok_ok("\\\\\\{-,1\\}", (tok_case[]){
        { Count, "\\\\\\{-,1\\}", 0 },
        END_CASES,
      }));
      check(tok_ok("\\\\\\{-1,1\\}", (tok_case[]){
        { Count, "\\\\\\{-1,1\\}", 0 },
        END_CASES,
      }));
    }

    it("should fail on invalid vim regex count") {
      check(tok_fail("\\\\\\{a\\}"));
      check(tok_fail("\\\\\\{+\\}"));
      check(tok_fail("\\\\\\{1.\\}"));
      check(tok_fail("\\\\\\{\\"));
      check(tok_fail("\\\\\\{"));
      check(tok_fail("\\\\\\{}"));
    }
  }

  describe("unroll") {
    it("should do nothing when there aren't any branches") {
      check(unroll_ok("a", (const char*[]){
        "a",
        NULL,
      }));
      check(unroll_ok("a*", (const char*[]){
        "a*",
        NULL,
      }));
    }

    it("should unroll branches at the root level") {
      check(unroll_ok("a,b", (const char*[]){
        "a",
        "b",
        NULL,
      }));
      check(unroll_ok("a,b,c", (const char*[]){
        "a",
        "b",
        "c",
        NULL,
      }));
    }

    it("should not unroll empty branches at the root level") {
      check(unroll_ok(",", (const char*[]){
        NULL,
      }));
      check(unroll_ok(",a", (const char*[]){
        "a",
        NULL,
      }));
      check(unroll_ok("a,", (const char*[]){
        "a",
        NULL,
      }));
      check(unroll_ok(",,", (const char*[]){
        NULL,
      }));
    }

    it("should unroll basic nested branches") {
      check(unroll_ok("{a}", (const char*[]){
        "a",
        NULL,
      }));
      check(unroll_ok("a{b}c", (const char*[]){
        "abc",
        NULL,
      }));
      check(unroll_ok("{a,b}", (const char*[]){
        "a",
        "b",
        NULL,
      }));
      check(unroll_ok("a{b,c}d", (const char*[]){
        "abd",
        "acd",
        NULL,
      }));
    }

    it("should unroll empty branches") {
      check(unroll_ok("{,a}", (const char*[]){
        "",
        "a",
        NULL,
      }));
      check(unroll_ok("{a,}", (const char*[]){
        "a",
        "",
        NULL,
      }));
      check(unroll_ok("a{,b}c", (const char*[]){
        "ac",
        "abc",
        NULL,
      }));
      check(unroll_ok("a{b,}c", (const char*[]){
        "abc",
        "ac",
        NULL,
      }));
      check(unroll_ok("a{,b,c}d", (const char*[]){
        "ad",
        "abd",
        "acd",
        NULL,
      }));
      check(unroll_ok("a{b,c,}d", (const char*[]){
        "abd",
        "acd",
        "ad",
        NULL,
      }));
      check(unroll_ok("a{b,,c}d", (const char*[]){
        "abd",
        "ad",
        "acd",
        NULL,
      }));
    }

    it("should unroll multiple basic nested branches") {
      check(unroll_ok("{a}{b}", (const char*[]){
        "ab",
        NULL,
      }));
      check(unroll_ok("a{b}c{d}e", (const char*[]){
        "abcde",
        NULL,
      }));
      check(unroll_ok("{a,b}{c}", (const char*[]){
        "ac",
        "bc",
        NULL,
      }));
      check(unroll_ok("a{b,c}d{e}f", (const char*[]){
        "abdef",
        "acdef",
        NULL,
      }));
      check(unroll_ok("{a}{b,c}", (const char*[]){
        "ab",
        "ac",
        NULL,
      }));
      check(unroll_ok("a{b}c{d,e}f", (const char*[]){
        "abcdf",
        "abcef",
        NULL,
      }));
      check(unroll_ok("{a,b}{c,d}", (const char*[]){
        "ac",
        "ad",
        "bc",
        "bd",
        NULL,
      }));
      check(unroll_ok("a{b,c}d{e,f}g", (const char*[]){
        "abdeg",
        "abdfg",
        "acdeg",
        "acdfg",
        NULL,
      }));
    }

    it("should unroll deeply nested branches, basic") {
      check(unroll_ok("{{a}}", (const char*[]){
        "a",
        NULL,
      }));
      check(unroll_ok("{{a,b}}", (const char*[]){
        "a",
        "b",
        NULL,
      }));
      check(unroll_ok("a{b{c}d}e", (const char*[]){
        "abcde",
        NULL,
      }));
    }

    it("should unroll deeply nested branches, complex") {
      check(unroll_ok("a{b,c}d{e,f{g,h}}i", (const char*[]){
        "abdei",
        "abdfgi",
        "abdfhi",
        "acdei",
        "acdfgi",
        "acdfhi",
        NULL,
      }));
      check(unroll_ok("a{b,c{d,e}}f{g,h}i", (const char*[]){
        "abfgi",
        "abfhi",
        "acdfgi",
        "acdfhi",
        "acefgi",
        "acefhi",
        NULL,
      }));
    }

    it("should fail on too deeply nested branches") {
      check(unroll_fail("{{{{{{{{{{a}}}}}}}}}}"));
    }
  }
}
