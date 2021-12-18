#include "auparser.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdnoreturn.h>
#include <ctype.h>
#include <assert.h>

#define BUF_SIZE (1024)

static const char *progname = NULL;
static bool opt_unroll = false;
static bool opt_tree = true;
static bool opt_json = true;
static bool opt_raw_patterns = false;
static const char *opt_input = NULL;

// TODO: clean all of this up

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

static bool render_json(const char *pat, const char *cmd, size_t lnum)
{
  char buf[BUF_SIZE];
  int r;

  r = write_escaped(buf, BUF_SIZE, pat, strlen(pat));
  assert(r >= 0);
  printf("  {\n    \"pattern\":\"%s\"", buf);

  if (lnum != 0)
    printf(",\n    \"lnum\":%ld", lnum);
  if (cmd != NULL)
    printf(",\n    \"cmd\":\"%s\"", cmd);

  token_t *tokens = tokenize(pat);
  if (tokens == NULL) {
    r = write_escaped(buf, BUF_SIZE, error, strlen(error));
    assert(r >= 0);
    printf(",\n    \"error\":\"%s\"}", buf);
    return false;
  }

  if (opt_tree) {
    printf(",\n    \"tree\":[[");
    for (const token_t *tok = tokens; tok->type; ++tok) {
      const token_t *ntok = tok + 1;
      bool comma = ntok->type != End && ntok->type != Branch && ntok->type != Pop;

      if (tok->type == Push) {
        printf("\n    ");
        for (int i = 0; i < tok->lvl; ++i)
          printf("  ");
        printf("{\"type\":\"Branch\",\"value\":[[");
        continue;
      } else if (tok->type == Branch) {
        printf("],[");
        continue;
      } else if (tok->type == Pop) {
        printf("]]}");
        if (comma) {
          printf(",");
        } else if (!ntok->type) {
          printf("\n    ");
        }
        continue;
      }

      if (tok->type == Empty)
        continue;

      r = write_escaped(buf, BUF_SIZE, tok->beg, tok->len);
      assert(r >= 0);
      printf("\n      ");
      for (int i = 0; i < tok->lvl; ++i)
        printf("  ");
      printf("{\"type\":\"%s\",\"value\":\"%s\"}", type_str(tok->type), buf);
      if (comma) {
        printf(",");
      } else if (!ntok->type) {
        printf("\n    ");
      } else {
        printf("\n    ");
        for (int i = 0; i < tok->lvl; ++i)
          printf("  ");
      }
    }
    printf("]]");
  }

  if (opt_unroll) {
    const token_t ***res = unroll(tokens);
    if (res == NULL) {
      r = write_escaped(buf, BUF_SIZE, error, strlen(error));
      assert(r >= 0);
      printf(",\n    \"error\":\"%s\"}", buf);
      free(tokens);
      return false;
    }

    printf(",\n    \"result\":[");
    for (const token_t ***it = res; *it != NULL; ++it) {
      size_t n = 0;
      for (const token_t **p = *it; *p != NULL; ++p) {
        const token_t *tok = *p;
        r = write_escaped(buf + n, BUF_SIZE - n, tok->beg, tok->len);
        assert(r >= 0);
        n += r;
        assert(n < BUF_SIZE - 1);
      }
      printf("\n      {\"pattern\":\"%s\",\"tokens\":[", buf);
      for (const token_t **p = *it; *p != NULL; ++p) {
        const token_t *tok = *p;
        if (tok->type == Empty)
          continue;
        r = write_escaped(buf, BUF_SIZE, tok->beg, tok->len);
        assert(r >= 0);
        printf("\n        {\"type\":\"%s\",\"value\":\"%s\"}%s",
            type_str(tok->type), buf, *(p + 1) == NULL ? "" : ",");
      }
      printf("\n      ]}%s", *(it + 1) == NULL ? "\n    " : ",");
    }
    printf("]");
    free_tokens(res);
  }
  printf("\n  }");

  free(tokens);
  return true;
}

static void escape_cmd(char **str, size_t *cap)
{
  size_t ncap = strlen(*str) * 2 + 1;
  char *buf = malloc(ncap);
  assert(buf != NULL);
  size_t n = 0;
  for (char *c = *str; *c != '\0'; ++c) {
    if (*c == '\\' || *c == '"')
      buf[n++] = '\\';
    buf[n++] = *c;
  }
  buf[n] = '\0';
  free(*str);
  *str = buf;
  *cap = ncap;
}

static void print_help(void)
{
  fprintf(stderr, "Usage: %s [option]... <file>\n", progname);
  fprintf(stderr, "    -u  unroll branches\n");
  fprintf(stderr, "    -t  disable tree\n");
  fprintf(stderr, "    -p  parse raw patterns (parses vim script file by default)\n");
  fprintf(stderr, "    -d  for debugging\n");
}

static void parse_options(int argc, char **argv)
{
  for (int i = 1; i < argc; ++i) {
    if (argv[i][0] == '-') {
      if (argv[i][1] == '\0') {
        if (opt_input != NULL) {
          fprintf(stderr, "Multiple input files not allowed\n");
          print_help();
          exit(EXIT_FAILURE);
        }
        opt_input = "-";
      } else {
        for (char *c = argv[i] + 1; *c != '\0'; ++c) {
          if (*c == 'p') {
            opt_raw_patterns = true;
          } else if (*c == 'd') {
            opt_json = false;
          } else if (*c == 'u') {
            opt_unroll = true;
          } else if (*c == 't') {
            opt_tree = false;
          } else if (*c == 'h') {
            print_help();
            exit(EXIT_SUCCESS);
          } else {
            fprintf(stderr, "Invalid option: -%c\n", *c);
            print_help();
            exit(EXIT_FAILURE);
          }
        }
      }
    } else {
      if (opt_input != NULL) {
        fprintf(stderr, "Multiple input files not allowed\n");
        print_help();
      }
      opt_input = argv[i];
    }
  }

  if (opt_input == NULL) {
    fprintf(stderr, "No input file\n");
    print_help();
    exit(EXIT_FAILURE);
  }
}

int main(int argc, char *argv[])
{
  assert(argc > 0);
  progname = argv[0];
  parse_options(argc, argv);

  FILE *fp = NULL;
  if (opt_input[0] == '-' && opt_input[1] == '\0') {
    fp = stdin;
  } else {
    fp = fopen(opt_input, "rb");
    if (fp == NULL) {
      perror("fopen");
      return EXIT_FAILURE;
    }
  }

  char *line = NULL; /// line buffer
  size_t len = 0;    /// line buffer length
  ssize_t nread = 0; /// line bytes read

  size_t patcap = 64; /// pattern buffer capacity
  size_t cmdcap = 64; /// command buffer capacity
  size_t cmdlen = 0;  /// command buffer length
  char *patstr = malloc(patcap); /// pattern buffer
  char *cmdstr = malloc(cmdcap); /// command buffer
  assert(patstr != NULL);
  assert(cmdstr != NULL);

  size_t aulnum = 0;  /// autocmd source line number
  bool inau = false; /// inside autocmd lines
  bool comma = false; /// needs comma

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

#define SKIP_TO_NEWLINE \
    do { \
      while (*it != '\0' && *it != '\r' && *it != '\n') \
        ++it; \
    } while (0)

  if (opt_json)
    printf("[\n");
  if (opt_raw_patterns) {
    for (size_t lnum = 1; (nread = getline(&line, &len, fp)) >= 0; ++lnum) {
      char *it = line;
      SKIP_WHITESPACE;
      char *pat = it;
      SKIP_TO_WHITESPACE;
      *it = '\0';

      if (opt_json) {
        if (comma)
          printf(",\n");
        render_json(pat, NULL, aulnum);
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
        if (inau) {
          if (opt_json) {
            if (comma)
              printf(",\n");
            escape_cmd(&cmdstr, &cmdcap);
            render_json(patstr, cmdstr, aulnum);
            comma = true;
          } else {
            parse(patstr);
          }
        }

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
        size_t patlen = it - pat;
        if (patlen >= patcap) {
          free(patstr);
          patstr = malloc(patlen + 1);
          assert(patstr != NULL);
        }
        memcpy(patstr, pat, patlen);
        patstr[patlen] = '\0';
        if (*(++it) != '\0') {
          SKIP_WHITESPACE;
          char *cmd = it;
          SKIP_TO_NEWLINE;
          cmdlen = it - cmd;
          if (cmdlen >= cmdcap) {
            free(cmdstr);
            cmdstr = malloc(cmdlen + 1);
            assert(cmdstr != NULL);
          }
          memcpy(cmdstr, cmd, cmdlen);
          cmdstr[cmdlen] = '\0';
        } else {
          cmdlen = 0;
        }

        aulnum = lnum;
        inau = true;
      } else if (inau && *it == '\\') {
        ++it;
        SKIP_WHITESPACE;
        char *cmd = it;
        SKIP_TO_NEWLINE;
        size_t len = it - cmd;
        if (cmdlen + len >= cmdcap) {
          char *buf = realloc(cmdstr, cmdlen + len + 1);
          assert(buf != NULL);
          cmdstr = buf;
        }
        memcpy(cmdstr + cmdlen, cmd, len);
        cmdlen = cmdlen + len;
        cmdstr[cmdlen] = '\0';
      } else {
        if (inau) {
          if (opt_json) {
            if (comma)
              printf(",\n");
            escape_cmd(&cmdstr, &cmdcap);
            render_json(patstr, cmdstr, aulnum);
            comma = true;
          } else {
            parse(patstr);
          }
        }
        cmdlen = 0;
        inau = false;
      }
    }
    if (inau) {
      if (opt_json) {
        if (comma)
          printf(",\n");
        escape_cmd(&cmdstr, &cmdcap);
        render_json(patstr, cmdstr, aulnum);
        comma = true;
      } else {
        parse(patstr);
      }
    }
  }
  if (opt_json)
    printf("\n]\n");

  free(patstr);
  free(cmdstr);
  free(line);
  if (fp != stdin)
    fclose(fp);
  return EXIT_SUCCESS;
}
