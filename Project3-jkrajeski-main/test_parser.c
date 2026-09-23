/*
 * test_parser.c  --  stand-alone test driver for the wish shell parser.
 *
 * Compile:
 *   gcc -Wall -Wextra -o test_parser test_parser.c parser.c
 *
 * Run:
 *   ./test_parser              -- runs the built-in test suite
 *   ./test_parser -i           -- interactive mode: type lines, see parse
 */

#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Automated test suite                                                 */
/* ------------------------------------------------------------------ */

/*
 * Each test case specifies an input line and the expected values of
 * every field in the resulting CommandLine.  A field value of NULL
 * means "expect NULL"; IGNORE_ARGC means we do not check argc for
 * that command.
 */
#define IGNORE_ARGC (-1)

typedef struct {
    const char *input;          /* raw input line                     */
    int   expect_null;          /* 1 if parse_command_line should return NULL */
    int   has_pipe;
    int   background;
    /* left command */
    int         left_argc;
    const char *left_argv[8];   /* expected argv values (NULL = not checked)  */
    const char *left_input;
    const char *left_output;
    /* right command (only checked when has_pipe == 1) */
    int         right_argc;
    const char *right_argv[8];
    const char *right_input;
    const char *right_output;
} TestCase;

static TestCase tests[] = {

    /* --- empty / whitespace -------------------------------------- */
    {
        .input       = "",
        .expect_null = 1
    },
    {
        .input       = "   \t  \n",
        .expect_null = 1
    },

    /* --- simple commands ---------------------------------------- */
    {
        .input      = "ls",
        .has_pipe   = 0, .background = 0,
        .left_argc  = 1, .left_argv  = {"ls"}
    },
    {
        .input      = "ls -l /tmp",
        .has_pipe   = 0, .background = 0,
        .left_argc  = 3, .left_argv  = {"ls", "-l", "/tmp"}
    },
    {
        .input      = "/bin/ls -l",
        .has_pipe   = 0, .background = 0,
        .left_argc  = 2, .left_argv  = {"/bin/ls", "-l"}
    },

    /* --- output redirection ------------------------------------- */
    {
        .input       = "ls > out.txt",
        .has_pipe    = 0, .background = 0,
        .left_argc   = 1, .left_argv  = {"ls"},
        .left_output = "out.txt"
    },
    {
        /* Redirection operator with no surrounding spaces. */
        .input       = "ls>out.txt",
        .has_pipe    = 0, .background = 0,
        .left_argc   = 1, .left_argv  = {"ls"},
        .left_output = "out.txt"
    },

    /* --- input redirection -------------------------------------- */
    {
        .input      = "sort < data.txt",
        .has_pipe   = 0, .background = 0,
        .left_argc  = 1, .left_argv  = {"sort"},
        .left_input = "data.txt"
    },

    /* --- both redirections on one command ----------------------- */
    {
        .input       = "sort < in.txt > out.txt",
        .has_pipe    = 0, .background = 0,
        .left_argc   = 1, .left_argv  = {"sort"},
        .left_input  = "in.txt",
        .left_output = "out.txt"
    },
    {
        /* Redirection operators in the opposite order. */
        .input       = "sort > out.txt < in.txt",
        .has_pipe    = 0, .background = 0,
        .left_argc   = 1, .left_argv  = {"sort"},
        .left_input  = "in.txt",
        .left_output = "out.txt"
    },

    /* --- background --------------------------------------------- */
    {
        .input      = "sleep 30 &",
        .has_pipe   = 0, .background = 1,
        .left_argc  = 2, .left_argv  = {"sleep", "30"}
    },
    {
        .input       = "find / > results.txt &",
        .has_pipe    = 0, .background = 1,
        .left_argc   = 2, .left_argv  = {"find", "/"},
        .left_output = "results.txt"
    },

    /* --- simple pipe -------------------------------------------- */
    {
        .input       = "ls -l | grep foo",
        .has_pipe    = 1, .background = 0,
        .left_argc   = 2, .left_argv  = {"ls", "-l"},
        .right_argc  = 2, .right_argv = {"grep", "foo"}
    },

    /* --- pipe + left-side input redirection --------------------- */
    {
        .input      = "grep foo < in.txt | sort",
        .has_pipe   = 1, .background = 0,
        .left_argc  = 2, .left_argv  = {"grep", "foo"},
        .left_input = "in.txt",
        .right_argc = 1, .right_argv = {"sort"}
    },

    /* --- pipe + right-side output redirection ------------------- */
    {
        .input       = "ls -l | grep foo > out.txt",
        .has_pipe    = 1, .background = 0,
        .left_argc   = 2, .left_argv  = {"ls", "-l"},
        .right_argc  = 2, .right_argv = {"grep", "foo"},
        .right_output= "out.txt"
    },

    /* --- pipe + both redirections ------------------------------- */
    {
        .input        = "grep foo < in.txt | sort > out.txt",
        .has_pipe     = 1, .background = 0,
        .left_argc    = 2, .left_argv  = {"grep", "foo"},
        .left_input   = "in.txt",
        .right_argc   = 1, .right_argv = {"sort"},
        .right_output = "out.txt"
    },

    /* --- error cases -------------------------------------------- */
    {
        /* Multiple pipes: should fail. */
        .input       = "ls | grep foo | wc",
        .expect_null = 1
    },
    {
        /* Missing command before pipe. */
        .input       = "| sort",
        .expect_null = 1
    },
    {
        /* Missing command after pipe. */
        .input       = "ls |",
        .expect_null = 1
    },
    {
        /* Missing filename after '>'. */
        .input       = "ls >",
        .expect_null = 1
    },
    {
        /* Missing filename after '<'. */
        .input       = "sort <",
        .expect_null = 1
    },
    {
        /* '&' not at end. */
        .input       = "sleep & 30",
        .expect_null = 1
    },
};

/* ------------------------------------------------------------------ */

static int str_eq_null(const char *a, const char *b)
{
    if (a == NULL && b == NULL) return 1;
    if (a == NULL || b == NULL) return 0;
    return strcmp(a, b) == 0;
}

static int check_command(const Command *cmd,
                          int expected_argc,
                          const char *expected_argv[],
                          const char *expected_input,
                          const char *expected_output,
                          const char *side,
                          const char *input_line)
{
    int ok = 1;

    if (expected_argc != IGNORE_ARGC && cmd->argc != expected_argc) {
        printf("  FAIL [%s] \"%s\": %s argc = %d, expected %d\n",
               side, input_line, side, cmd->argc, expected_argc);
        ok = 0;
    }

    for (int i = 0; i < expected_argc && expected_argv[i] != NULL; i++) {
        if (i >= cmd->argc || strcmp(cmd->argv[i], expected_argv[i]) != 0) {
            printf("  FAIL [%s] \"%s\": %s argv[%d] = \"%s\", expected \"%s\"\n",
                   side, input_line, side,
                   i, (i < cmd->argc ? cmd->argv[i] : "(missing)"),
                   expected_argv[i]);
            ok = 0;
        }
    }

    if (!str_eq_null(cmd->input_file, expected_input)) {
        printf("  FAIL [%s] \"%s\": %s input_file = \"%s\", expected \"%s\"\n",
               side, input_line, side,
               cmd->input_file  ? cmd->input_file  : "(null)",
               expected_input   ? expected_input   : "(null)");
        ok = 0;
    }

    if (!str_eq_null(cmd->output_file, expected_output)) {
        printf("  FAIL [%s] \"%s\": %s output_file = \"%s\", expected \"%s\"\n",
               side, input_line, side,
               cmd->output_file ? cmd->output_file : "(null)",
               expected_output  ? expected_output  : "(null)");
        ok = 0;
    }

    return ok;
}

static void run_tests(void)
{
    int total  = (int)(sizeof(tests) / sizeof(tests[0]));
    int passed = 0;

    for (int t = 0; t < total; t++) {
        TestCase *tc  = &tests[t];
        CommandLine *cl = parse_command_line(tc->input);

        if (tc->expect_null) {
            if (cl == NULL) {
                printf("  PASS: \"%s\" -> NULL (expected)\n", tc->input);
                passed++;
            } else {
                printf("  FAIL: \"%s\" -> non-NULL, expected NULL\n", tc->input);
                free_command_line(cl);
            }
            continue;
        }

        /* We expected a valid parse. */
        if (cl == NULL) {
            printf("  FAIL: \"%s\" -> NULL, expected valid parse\n", tc->input);
            continue;
        }

        int ok = 1;

        if (cl->has_pipe != tc->has_pipe) {
            printf("  FAIL: \"%s\": has_pipe = %d, expected %d\n",
                   tc->input, cl->has_pipe, tc->has_pipe);
            ok = 0;
        }
        if (cl->background != tc->background) {
            printf("  FAIL: \"%s\": background = %d, expected %d\n",
                   tc->input, cl->background, tc->background);
            ok = 0;
        }

        ok &= check_command(&cl->left,
                             tc->left_argc, tc->left_argv,
                             tc->left_input, tc->left_output,
                             "LEFT", tc->input);

        if (tc->has_pipe)
            ok &= check_command(&cl->right,
                                 tc->right_argc, tc->right_argv,
                                 tc->right_input, tc->right_output,
                                 "RIGHT", tc->input);

        if (ok) {
            printf("  PASS: \"%s\"\n", tc->input);
            passed++;
        }

        free_command_line(cl);
    }

    printf("\n%d / %d tests passed.\n", passed, total);
}

/* ------------------------------------------------------------------ */
/* Interactive mode                                                      */
/* ------------------------------------------------------------------ */

static void run_interactive(void)
{
    char line[1024];
    printf("wish parser -- interactive mode  (Ctrl-D to quit)\n\n");

    while (1) {
        printf("wish> ");
        fflush(stdout);

        if (fgets(line, sizeof(line), stdin) == NULL) {
            printf("\n");
            break;
        }

        CommandLine *cl = parse_command_line(line);
        if (cl == NULL) {
            printf("(empty or error)\n\n");
        } else {
            print_command_line(cl);
            printf("\n");
            free_command_line(cl);
        }
    }
}

/* ------------------------------------------------------------------ */
/* main                                                                  */
/* ------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
    if (argc == 2 && strcmp(argv[1], "-i") == 0)
        run_interactive();
    else
        run_tests();

    return 0;
}
