#include "auparser.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdnoreturn.h>
#include <ctype.h>
#include <assert.h>

#define BUF_SIZE (512)

static bool parse(const char *pat)
{
  fprintf(stdout, "%s\n", pat);

  token_t *tokens = tokenize(pat);
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

  for (const token_t ***it = res; *it != NULL; ++it) {
    fprintf(stdout, "    ");
    print_tokens(*it);
  }

  free_tokens(res);
  free(tokens);
  return true;
}

static bool render_json(const char *pat)
{
  char buf[BUF_SIZE];
  int r;

  r = write_escaped(buf, BUF_SIZE, pat, strlen(pat));
  assert(r >= 0);
  printf(" {\n  \"pattern\":\"%s\"", buf);

  token_t *tokens = tokenize(pat);
  if (tokens == NULL) {
    r = write_escaped(buf, BUF_SIZE, error, strlen(error));
    assert(r >= 0);
    printf(",\n  \"error\":\"%s\"}", buf);
    return false;
  }

  printf(",\n  \"tree\":[[");
  for (const token_t *tok = tokens; tok->type; ++tok) {
    const token_t *ntok = tok + 1;
    bool comma = ntok->type != End && ntok->type != Branch && ntok->type != Pop;

    if (tok->type == Push) {
      printf("\n  ");
      for (int i = 0; i < tok->lvl; ++i)
        printf(" ");
      printf("{\"type\":\"Branch\",\"value\":[[");
      continue;
    } else if (tok->type == Branch) {
      printf("\n  ");
      for (int i = 0; i < tok->lvl; ++i)
        printf(" ");
      printf("],[");
      continue;
    } else if (tok->type == Pop) {
      printf("\n  ");
      for (int i = 0; i < tok->lvl; ++i)
        printf(" ");
      printf("]]}");
      if (comma) {
        printf(",");
      } else if (!ntok->type) {
        printf("\n  ");
      }
      continue;
    }

    r = write_escaped(buf, BUF_SIZE, tok->beg, tok->len);
    assert(r >= 0);
    printf("\n   ");
    for (int i = 0; i < tok->lvl; ++i)
      printf(" ");
    printf("{\"type\":\"%s\",\"value\":\"%s\"}", type_str(tok->type), buf);
    if (comma) {
      printf(",");
    } else if (!ntok->type) {
      printf("\n  ");
    }
  }
  printf("]]");

  const token_t ***res = unroll(tokens);
  if (res == NULL) {
    r = write_escaped(buf, BUF_SIZE, error, strlen(error));
    assert(r >= 0);
    printf(",\n  \"error\":\"%s\"}", buf);
    free(tokens);
    return false;
  }

  printf(",\n  \"result\":[");
  for (const token_t ***it = res; *it != NULL; ++it) {
    size_t n = 0;
    for (const token_t **p = *it; *p != NULL; ++p) {
      const token_t *tok = *p;
      r = write_escaped(buf + n, BUF_SIZE - n, tok->beg, tok->len);
      assert(r >= 0);
      n += r;
      assert(n < BUF_SIZE - 1);
    }
    printf("\n   {\"pattern\":\"%s\",\"tokens\":[", buf);
    for (const token_t **p = *it; *p != NULL; ++p) {
      const token_t *tok = *p;
      r = write_escaped(buf, BUF_SIZE, tok->beg, tok->len);
      assert(r >= 0);
      printf("\n    {\"type\":\"%s\",\"value\":\"%s\"}%s",
          type_str(tok->type), buf, *(p + 1) == NULL ? "" : ",");
    }
    printf("\n   ]}%s", *(it + 1) == NULL ? "\n  " : ",");
  }
  printf("]\n }");

  free_tokens(res);
  free(tokens);
  return true;
}

static const char *progname = NULL;
static noreturn void print_help()
{
  fprintf(stderr, "Usage: %s [option]... <file>\n", progname);
  fprintf(stderr, "    -p  parse raw patterns (parses vim script file by default)\n");
  fprintf(stderr, "    -j  serialize to json\n");
  exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
  progname = argv[0];

  const char *filename = NULL;
  bool raw_patterns = false;
  bool to_json = false;

  for (int i = 1; i < argc; ++i) {
    if (argv[i][0] == '-') {
      if (argv[i][1] == '\0') {
        if (filename != NULL)
          print_help();
        filename = "-";
      } else {
        for (char *c = argv[i] + 1; *c != '\0'; ++c) {
          if (*c == 'p') {
            raw_patterns = true;
          } else if (*c == 'j') {
            to_json = true;
          } else {
            print_help();
            break;
          }
        }
      }
    } else {
      if (filename != NULL)
        print_help();
      filename = argv[i];
    }
  }
  if (filename == NULL) {
    print_help();
  }

  FILE *fp;
  char *line = NULL;
  size_t len = 0;
  ssize_t nread = 0;

  if (filename[0] == '-' && filename[1] == '\0') {
    fp = stdin;
  } else {
    fp = fopen(filename, "rb");
    if (fp == NULL) {
      perror("fopen");
      return EXIT_FAILURE;
    }
  }

  bool comma = false;

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

  if (to_json)
    printf("[\n");
  if (raw_patterns) {
    for (size_t lnum = 1; (nread = getline(&line, &len, fp)) >= 0; ++lnum) {
      char *it = line;
      SKIP_WHITESPACE;
      char *pat = it;
      SKIP_TO_WHITESPACE;
      *it = '\0';

      if (to_json) {
        if (comma)
          printf(",\n");
        render_json(pat);
        comma = true;
      } else {
        parse(pat);
      }
    }
  } else {
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

        // printf("cmd: %s\n", cmd);

        if (to_json) {
          if (comma)
            printf(",\n");
          render_json(pat);
          comma = true;
        } else {
          parse(pat);
        }
        (void)cmd;
      } else if (*it == '\\') {
        ++it;
        SKIP_WHITESPACE;
        // printf("%s", it);
      }
    }
  }
  if (to_json)
    printf("\n]\n");

  free(line);
  if (fp != stdin)
    fclose(fp);
  return EXIT_SUCCESS;
}
