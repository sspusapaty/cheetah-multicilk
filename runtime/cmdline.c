#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global_state.h"

enum {
  NONE, NPROC, DEQ_DEPTH, STACK_SIZE, ALLOC_BATCH, HELP, END_OPTIONS
};

static struct options {
  char *string;
  int option;
  char *help;
} optarray[] = {
  {
    "", END_OPTIONS, "-- : end of option parsing"
  },
  {
    "nproc", NPROC, "--nproc <n> : set number of processors"
  },
  {
    "deqdepth", DEQ_DEPTH, "--deqdepth <n> : set number of entries per deque"
  },
  {
    "stacksize", STACK_SIZE, "--stacksize <n> : set the size of a fiber"
  },
  {
    "alloc-batch", ALLOC_BATCH,
    "--alloc-batch <n> : set batch length for memory allocator"
  },
  {
    "help", HELP, "--help : print this message"
  },
  {
    (char *) 0, NONE, ""
  }
};

static void print_help(void) {
  struct options *p;

  fprintf(stderr, "cheetah runtime options:\n");

  for (p = optarray; p->string; ++p)
    if (p->help)
      fprintf(stderr, "     %s\n", p->help);

  fprintf(stderr, "\n");
}

/*
static void print_version(void) {
  int debug = 0, stats = 0, timing = 0;
  WHEN_CILK_DEBUG(debug = 1);
  WHEN_CILK_STATS(stats = 1);
  WHEN_CILK_TIMING(timing = 1);
  fprintf(stderr, "version " VERSION "\n");
  fprintf(stderr, "compilation options: ");
  if (debug) fprintf(stderr, "CILK_DEBUG ");
  if (stats) fprintf(stderr, "CILK_STATS ");
  if (timing) fprintf(stderr, "CILK_TIMING ");
  if (!(debug | stats | timing))
    fprintf(stderr, "none");
  fprintf(stderr, "\n");
}
*/

/* look for a given string in the option table */
static struct options *parse_option(char *s) {
  struct options *p;

  for (p = optarray; p->string; ++p)
    if (strncmp(s, p->string, strlen(p->string)+1) == 0)
      break;
  return p;
}

#define CHECK(cond, complaint) \
if (!(cond)) { \
  fprintf(stderr, "Bad option argument for -%s: %s\n", \
          p->string, complaint); return 1; \
}

int parse_command_line(struct rts_options *options, int *argc, char *argv[]) {
  struct options *p;
  /* gcc allows to write directly into *options, but other compilers
   * only allow you to initialize this way.
   */
  struct rts_options default_options = DEFAULT_OPTIONS;

  /* default options */
  *options = default_options;

  int j = 1;
  for (int i = 1; i < *argc; ++i) {
    if (argv[i][0] == '-' && argv[i][1] == '-') {
      p = parse_option(argv[i] + 2);

      switch (p->option) {
        case NPROC:
          ++i;
          CHECK(i < *argc, "argument missing");
          options->nproc = atoi(argv[i]);
          break;

        case DEQ_DEPTH:
          ++i;
          CHECK(i < *argc, "argument missing");
          options->deqdepth= atoi(argv[i]);
          CHECK(options->deqdepth > 0, "non-positive deque depth");
          break;

        case STACK_SIZE:
          ++i;
          CHECK(i < *argc, "argument missing");
          options->stacksize = atoi(argv[i]);
          CHECK(options->stacksize > 0, "non-positive stack size");
          break;

        case HELP:
          print_help();
          return 1;
          break;

        case ALLOC_BATCH:
          ++i;
          CHECK(i < *argc, "argument missing");
          options->alloc_batch_size = atoi(argv[i]);
          if (options->alloc_batch_size < 8)
            options->alloc_batch_size = 8;
          break;

        default:
          fprintf(stderr, "Unrecognized options.\n");
          print_help();
          return 1;
          break;
      }
    } else {
      assert(j <= i);
      argv[j++] = argv[i]; // keep it
    }
  }
  *argc = j;

  return 0;
}
