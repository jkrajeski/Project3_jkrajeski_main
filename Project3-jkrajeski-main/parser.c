/*
 * parser.c  --  command-line parser for wish shell
 *
 * See parser.h for the full interface documentation.
 *
 * Implementation overview
 * -----------------------
 * Parsing happens in three stages:
 *
 *   1. TOKENIZE  -- split the raw input line into an array of token
 *                   strings, treating whitespace as a delimiter and
 *                   recognising the special single-character operators
 *                   |  <  >  &  as tokens in their own right.
 *
 *   2. SCAN      -- walk the token array once to find the positions of
 *                   any '|' and '&' tokens, and verify basic structure.
 *
 *   3. BUILD     -- fill in a CommandLine struct by interpreting the
 *                   tokens between the structural markers found in step 2.
 *
 * All heap allocation is centralised in two helpers (xstrdup / xmalloc)
 * that print a message and exit on allocation failure, so the rest of
 * the code never needs to check for NULL from strdup/malloc.
 */

#include "parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ------------------------------------------------------------------ */
/* Internal constants                                                   */
/* ------------------------------------------------------------------ */

/* Maximum tokens on one input line (generous upper bound).            */
#define MAX_TOKENS 128

/* ------------------------------------------------------------------ */
/* Allocation helpers                                                   */
/* ------------------------------------------------------------------ */

static void *xmalloc(size_t n)
{
    void *p = malloc(n);
    if (p == NULL) {
        fprintf(stderr, "wish: out of memory\n");
        exit(1);
    }
    return p;
}

static char *xstrdup(const char *s)
{
    char *p = strdup(s);
    if (p == NULL) {
        fprintf(stderr, "wish: out of memory\n");
        exit(1);
    }
    return p;
}

/* ------------------------------------------------------------------ */
/* Stage 1 -- tokeniser                                                 */
/* ------------------------------------------------------------------ */

/*
 * is_operator -- return 1 if ch is a shell operator character that
 * must always be treated as a standalone token.
 */
static int is_operator(char ch)
{
    return ch == '|' || ch == '<' || ch == '>' || ch == '&';
}

/*
 * tokenize -- split `line` into tokens stored in `tokens[]`.
 *
 * Rules:
 *   - Whitespace separates tokens and is otherwise discarded.
 *   - Each of  |  <  >  &  is always a token by itself, even if not
 *     surrounded by whitespace (e.g. "ls>out.txt" yields three tokens).
 *   - All other sequences of non-whitespace, non-operator characters
 *     form a single word token.
 *
 * Returns the number of tokens produced, or -1 if the token limit is
 * exceeded.  Each token in tokens[] is a heap-allocated string; the
 * caller owns them.
 */
static int tokenize(const char *line, char *tokens[], int max_tokens)
{
    int   count = 0;
    const char *p = line;

    while (*p != '\0') {

        /* Skip whitespace. */
        if (isspace((unsigned char)*p)) {
            p++;
            continue;
        }

        /* Operator: becomes a one-character token on its own. */
        if (is_operator(*p)) {
            if (count >= max_tokens) return -1;
            char buf[2] = { *p, '\0' };
            tokens[count++] = xstrdup(buf);
            p++;
            continue;
        }

        /* Word: consume until whitespace or operator. */
        const char *start = p;
        while (*p != '\0' && !isspace((unsigned char)*p) && !is_operator(*p))
            p++;

        if (count >= max_tokens) return -1;
        /* Copy exactly the characters in [start, p). */
        size_t len = (size_t)(p - start);
        char  *tok = xmalloc(len + 1);
        memcpy(tok, start, len);
        tok[len] = '\0';
        tokens[count++] = tok;
    }

    return count;
}

/* Free every string in tokens[0..count-1]. */
static void free_tokens(char *tokens[], int count)
{
    for (int i = 0; i < count; i++) {
        free(tokens[i]);
        tokens[i] = NULL;
    }
}

/* ------------------------------------------------------------------ */
/* Stage 2 -- structural scan                                           */
/* ------------------------------------------------------------------ */

/*
 * scan_structure -- locate structural operators in the token array and
 * perform basic validity checks.
 *
 * On success, writes:
 *   *pipe_pos  -- index of the '|' token, or -1 if absent.
 *   *bg_pos    -- index of the '&' token, or -1 if absent.
 *
 * Returns 0 on success, -1 if a structural error is detected (multiple
 * pipes, '&' not at end, empty input, etc.).
 */
static int scan_structure(char *tokens[], int count,
                           int *pipe_pos, int *bg_pos)
{
    *pipe_pos = -1;
    *bg_pos   = -1;

    for (int i = 0; i < count; i++) {
        if (strcmp(tokens[i], "|") == 0) {
            if (*pipe_pos != -1) {
                /* More than one pipe -- not supported. */
                fprintf(stderr,
                    "wish: only a single pipe operator is supported\n");
                return -1;
            }
            *pipe_pos = i;
        } else if (strcmp(tokens[i], "&") == 0) {
            if (i != count - 1) {
                /* '&' must be the very last token. */
                fprintf(stderr,
                    "wish: '&' must appear at the end of the command\n");
                return -1;
            }
            *bg_pos = i;
        }
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* Stage 3 -- command builder                                           */
/* ------------------------------------------------------------------ */

/*
 * build_command -- fill in a Command struct from tokens[start..end-1].
 *
 * Tokens that form argument words are collected into cmd->argv.
 * Redirection operators ('<', '>') consume the following token as a
 * filename and set cmd->input_file or cmd->output_file.
 *
 * Returns 0 on success, -1 on error (missing filename after operator,
 * too many arguments, operator used twice for the same direction, etc.).
 */
static int build_command(char *tokens[], int start, int end, Command *cmd)
{
    /* Zero the struct so all pointers start NULL and argc starts 0. */
    memset(cmd, 0, sizeof(Command));

    int i = start;
    while (i < end) {
        const char *tok = tokens[i];

        if (strcmp(tok, "<") == 0) {
            /* Input redirection: next token must be a filename. */
            if (i + 1 >= end) {
                fprintf(stderr, "wish: missing filename after '<'\n");
                return -1;
            }
            if (cmd->input_file != NULL) {
                fprintf(stderr, "wish: duplicate '<' redirection\n");
                return -1;
            }
            cmd->input_file = xstrdup(tokens[i + 1]);
            i += 2;

        } else if (strcmp(tok, ">") == 0) {
            /* Output redirection: next token must be a filename. */
            if (i + 1 >= end) {
                fprintf(stderr, "wish: missing filename after '>'\n");
                return -1;
            }
            if (cmd->output_file != NULL) {
                fprintf(stderr, "wish: duplicate '>' redirection\n");
                return -1;
            }
            cmd->output_file = xstrdup(tokens[i + 1]);
            i += 2;

        } else {
            /* Regular argument word. */
            if (cmd->argc >= MAX_ARGS) {
                fprintf(stderr,
                    "wish: too many arguments (limit is %d)\n", MAX_ARGS);
                return -1;
            }
            cmd->argv[cmd->argc++] = xstrdup(tok);
            i++;
        }
    }

    /* argv must be NULL-terminated for execv. */
    cmd->argv[cmd->argc] = NULL;

    /* A command with no program name is an error. */
    if (cmd->argc == 0) {
        fprintf(stderr, "wish: empty command\n");
        return -1;
    }

    return 0;
}

/*
 * free_command_fields -- release heap memory inside a Command struct.
 * Does not free the Command itself (it may be embedded in a CommandLine).
 */
static void free_command_fields(Command *cmd)
{
    for (int i = 0; i < cmd->argc; i++) {
        free(cmd->argv[i]);
        cmd->argv[i] = NULL;
    }
    cmd->argc = 0;
    free(cmd->input_file);
    cmd->input_file = NULL;
    free(cmd->output_file);
    cmd->output_file = NULL;
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

CommandLine *parse_command_line(const char *line)
{
    if (line == NULL) return NULL;

    /* ---- Stage 1: tokenise ---------------------------------------- */

    char *tokens[MAX_TOKENS];
    int   count = tokenize(line, tokens, MAX_TOKENS);

    if (count < 0) {
        fprintf(stderr, "wish: command line too complex\n");
        return NULL;
    }
    if (count == 0) {
        /* Empty or all-whitespace line -- not an error, just nothing to do. */
        return NULL;
    }

    /* ---- Stage 2: scan for structure ------------------------------ */

    int pipe_pos, bg_pos;
    if (scan_structure(tokens, count, &pipe_pos, &bg_pos) != 0) {
        free_tokens(tokens, count);
        return NULL;
    }

    /* The effective end of the token range: stop before '&' if present. */
    int effective_end = (bg_pos != -1) ? bg_pos : count;

    /* A line that is only '&' is an error. */
    if (effective_end == 0) {
        fprintf(stderr, "wish: missing command before '&'\n");
        free_tokens(tokens, count);
        return NULL;
    }

    /* ---- Stage 3: build CommandLine ------------------------------- */

    CommandLine *cl = xmalloc(sizeof(CommandLine));
    memset(cl, 0, sizeof(CommandLine));
    cl->has_pipe   = (pipe_pos != -1) ? 1 : 0;
    cl->background = (bg_pos   != -1) ? 1 : 0;

    if (cl->has_pipe) {
        /* Tokens before the pipe -> left command.
           Tokens after  the pipe (up to effective_end) -> right command. */
        int left_end   = pipe_pos;          /* exclusive                 */
        int right_start = pipe_pos + 1;     /* skip the '|' token        */

        if (left_end == 0) {
            fprintf(stderr, "wish: missing command before '|'\n");
            goto error;
        }
        if (right_start >= effective_end) {
            fprintf(stderr, "wish: missing command after '|'\n");
            goto error;
        }

        if (build_command(tokens, 0,           left_end,      &cl->left)  != 0)
            goto error;
        if (build_command(tokens, right_start, effective_end, &cl->right) != 0)
            goto error;

    } else {
        /* No pipe: entire token range is one command. */
        if (build_command(tokens, 0, effective_end, &cl->left) != 0)
            goto error;
    }

    free_tokens(tokens, count);
    return cl;

error:
    free_command_fields(&cl->left);
    free_command_fields(&cl->right);
    free(cl);
    free_tokens(tokens, count);
    return NULL;
}

/* ------------------------------------------------------------------ */

void free_command_line(CommandLine *cl)
{
    if (cl == NULL) return;
    free_command_fields(&cl->left);
    free_command_fields(&cl->right);
    free(cl);
}

/* ------------------------------------------------------------------ */

void print_command_line(const CommandLine *cl)
{
    if (cl == NULL) {
        printf("(null CommandLine)\n");
        return;
    }

    printf("has_pipe  : %s\n", cl->has_pipe   ? "yes" : "no");
    printf("background: %s\n", cl->background ? "yes" : "no");

    /* Helper lambda-like macro to avoid duplicating the print logic. */
    #define PRINT_CMD(label, cmd)                                    \
        do {                                                         \
            printf("%s command:\n", (label));                        \
            for (int _i = 0; _i < (cmd).argc; _i++)                 \
                printf("  argv[%d] : %s\n", _i, (cmd).argv[_i]);    \
            if ((cmd).input_file)                                    \
                printf("  stdin   : %s\n", (cmd).input_file);       \
            if ((cmd).output_file)                                   \
                printf("  stdout  : %s\n", (cmd).output_file);      \
        } while (0)

    PRINT_CMD("LEFT", cl->left);
    if (cl->has_pipe)
        PRINT_CMD("RIGHT", cl->right);

    #undef PRINT_CMD
}
