/*
 * parser.h  --  command-line parser for wish shell
 *
 * This module turns a single line of shell input into a structured
 * representation that the rest of the shell can act on without doing
 * any string handling of its own.
 *
 * STRUCTURES
 * ----------
 *   Command      -- one executable unit: program name, arguments, and
 *                   optional input/output redirection filenames.
 *
 *   CommandLine  -- the top-level result of parsing one input line.
 *                   Contains one Command (or two if a pipe is present)
 *                   and flags for pipe and background.
 *
 * TYPICAL USAGE
 * -------------
 *
 *   char line[1024];
 *   printf("wish> ");
 *   if (fgets(line, sizeof(line), stdin) == NULL) { ... handle EOF ... }
 *
 *   CommandLine *cl = parse_command_line(line);
 *   if (cl == NULL) {
 *       // empty line or parse error -- just re-prompt
 *       continue;
 *   }
 *
 *   // Inspect cl->left, cl->right, cl->has_pipe, cl->background ...
 *
 *   free_command_line(cl);   // always free when done
 *
 * WHAT THE PARSER HANDLES
 * -----------------------
 *   Simple command          ls -l /tmp
 *   Absolute path           /bin/ls -l
 *   Output redirection      ls > out.txt
 *   Input redirection       sort < data.txt
 *   Both redirections       grep foo < in.txt > out.txt
 *   Background              sleep 10 &
 *   Single pipe             ls -l | grep foo
 *   Pipe + redirection      grep foo < in.txt | sort > out.txt
 *
 * WHAT THE PARSER DOES NOT HANDLE
 * --------------------------------
 *   Multi-stage pipelines   ls | grep foo | wc     (not required)
 *   Quoted strings          echo "hello world"     (not required)
 *   Environment variables   echo $HOME             (not required)
 *   Semicolons              ls ; pwd               (not required)
 *
 * MEMORY OWNERSHIP
 * ----------------
 *   parse_command_line() heap-allocates the CommandLine and all strings
 *   inside it.  Call free_command_line() when done -- do not free any
 *   individual fields yourself.
 */

#ifndef PARSER_H
#define PARSER_H

/* Maximum number of arguments a single command may have (including argv[0]). */
#define MAX_ARGS 64

/*
 * Command -- represents one executable unit on the command line.
 *
 * Example:  grep foo < input.txt > output.txt
 *
 *   argv[0]  = "grep"
 *   argv[1]  = "foo"
 *   argv[2]  = NULL          (always NULL-terminated)
 *   argc     = 2
 *   input_file  = "input.txt"
 *   output_file = "output.txt"
 */
typedef struct {
    char *argv[MAX_ARGS + 1]; /* argument vector; argv[argc] == NULL          */
    int   argc;               /* number of arguments (not counting NULL)      */
    char *input_file;         /* filename after '<', or NULL if none          */
    char *output_file;        /* filename after '>', or NULL if none          */
} Command;

/*
 * CommandLine -- the fully parsed representation of one input line.
 *
 * If has_pipe == 0:
 *   - left  holds the single command.
 *   - right is zeroed and should be ignored.
 *
 * If has_pipe == 1:
 *   - left  holds the command to the left  of '|'.
 *   - right holds the command to the right of '|'.
 *
 * background == 1 means the line ended with '&'.
 *
 * Examples:
 *
 *   Input:  ls -l
 *     has_pipe   = 0
 *     background = 0
 *     left.argv  = {"ls", "-l", NULL}
 *
 *   Input:  grep foo < in.txt | sort > out.txt
 *     has_pipe         = 1
 *     background       = 0
 *     left.argv        = {"grep", "foo", NULL}
 *     left.input_file  = "in.txt"
 *     right.argv       = {"sort", NULL}
 *     right.output_file= "out.txt"
 *
 *   Input:  sleep 30 &
 *     has_pipe   = 0
 *     background = 1
 *     left.argv  = {"sleep", "30", NULL}
 */
typedef struct {
    Command left;       /* command (or left side of pipe)  */
    Command right;      /* right side of pipe; ignore if has_pipe == 0 */
    int     has_pipe;   /* 1 if a '|' was present, 0 otherwise         */
    int     background; /* 1 if '&' was present,  0 otherwise          */
} CommandLine;

/*
 * parse_command_line -- parse one line of shell input.
 *
 * Parameters:
 *   line  --  the raw string read from the user (may include '\n').
 *             The string is not modified.
 *
 * Returns:
 *   A heap-allocated CommandLine on success.
 *   NULL if the line is empty, contains only whitespace, or is malformed.
 *
 * The caller must pass the returned pointer to free_command_line() when
 * finished with it.
 */
CommandLine *parse_command_line(const char *line);

/*
 * free_command_line -- release all memory allocated by parse_command_line.
 *
 * Passing NULL is safe (no-op).
 */
void free_command_line(CommandLine *cl);

/*
 * print_command_line -- print a human-readable description of a parsed
 * command line to stdout.  Useful for Component 1 debugging.
 *
 * Example output for "grep foo < in.txt | sort > out.txt":
 *
 *   has_pipe  : yes
 *   background: no
 *   LEFT command:
 *     argv[0] : grep
 *     argv[1] : foo
 *     stdin   : in.txt
 *   RIGHT command:
 *     argv[0] : sort
 *     stdout  : out.txt
 */
void print_command_line(const CommandLine *cl);

#endif /* PARSER_H */
