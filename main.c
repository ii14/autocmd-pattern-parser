#include "auparser.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdnoreturn.h>
#include <ctype.h>

static bool parse(const char *pat)
{
  fprintf(stdout, "%s\n", pat);

  token_t *tokens = NULL;
  size_t size = tokenize(pat, &tokens);
  if (!size) {
    fprintf(stderr, "tokenizing failed: %s\n", error);
    return false;
  }

  const token_t ***res = unroll(tokens, size);
  if (res == NULL) {
    fprintf(stderr, "unrolling failed: %s\n", error);
    free(tokens);
    return false;
  }

  for (const token_t ***it = res; *it != NULL; ++it) {
    fprintf(stdout, "    ");
    print_tokens(*it);
  }

  unroll_free(res);
  free(tokens);
  return true;
}

static noreturn void print_help()
{
  fprintf(stderr, "Usage: gen_filetype [-p] <file>\n");
  exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
  const char *filename = NULL;
  bool raw_patterns = false;

  for (int i = 1; i < argc; ++i) {
    if (argv[i][0] == '-') {
      if (argv[i][1] == '\0') {
        if (filename != NULL)
          print_help();
        filename = "-";
      } else if (strcmp(argv[i], "-p") == 0) {
        raw_patterns = true;
      } else {
        print_help();
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

  if (raw_patterns) {
    for (size_t lnum = 1; (nread = getline(&line, &len, fp)) >= 0; ++lnum) {
      char *it = line;
      SKIP_WHITESPACE;
      char *pat = it;
      SKIP_TO_WHITESPACE;
      *it = '\0';
      parse(pat);
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

        // printf("evt: %s\n", events);
        // printf("%s\n", pat);
        // printf("cmd: %s\n", cmd);

        parse(pat);
        (void)cmd;
      } else if (*it == '\\') {
        ++it;
        SKIP_WHITESPACE;
        // printf("%s", it);
      }
    }
  }

  free(line);
  if (fp != stdin)
    fclose(fp);
  return EXIT_SUCCESS;
}
