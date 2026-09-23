#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "parser.h"

int handle_builtin(Command *cmd);

//tu shell
//component 1, 2, 3
//component 4
//component 5
//Print a prompt (e.g., tush> ) to stdout.
//Read a line of input using fgets or getline.
//Pass the raw input string to parser.
//Print a summary of what was parsed — the command name, each argument

char *find_executable_path(const char *command, const char *path_env)
{
    if (command == NULL || path_env == NULL)
    {
        return NULL;
    }

    const char *start = path_env;

    while (1)
    {
        //Find the end of the current directory in PATH
        const char *end = strchr(start, ':');

        size_t dir_length;

        if (end == NULL)
        {
            dir_length = strlen(start);
        }
        else
        {
            dir_length = (size_t)(end - start);
        }

        //An empty PATH entry means the current directory
        if (dir_length == 0)
        {
            dir_length = 1;
        }

        //candidate will contain something like: /usr/bin/ls
        //We need directory + "/" + command + '\0'

        size_t command_length = strlen(command);

        size_t candidate_length;

        if (dir_length == 1 && start[0] == ':')
        {
            candidate_length = command_length + 2;
        }
        else
        {
            candidate_length = dir_length + 1 + command_length + 1;
        }

        char *candidate = malloc(candidate_length);

        if (candidate == NULL)
        {
            perror("malloc");
            return NULL;
        }

        //Handle an empty PATH entry
        if (start[0] == ':' && dir_length == 1)
        {
            snprintf(candidate, candidate_length, "./%s", command);
        }
        else
        {
            snprintf(candidate, candidate_length, "%.*s/%s",
                     (int)dir_length, start, command);
        }

        // access checks whether the file exists and can be executed
        if (access(candidate, X_OK) == 0)
        {
            return candidate;
        }

        free(candidate);

        //end of path
        if (end == NULL)
        {
            break;
        }

        start = end + 1;
    }
    return NULL;
}


static void execute_command(Command *cmd)
{
    if (cmd == NULL || cmd->argc == 0)
    {
        return;
    }

    //no redirection
    if (cmd->input_file != NULL || cmd->output_file != NULL)
    {
        fprintf(stderr, "Redirection is not implemented yet.\n");
        return;
    }

    char *executable_path = NULL;

    //if argv[0] contains /, use it directly
    if (strchr(cmd->argv[0], '/') != NULL)
    {
        executable_path = malloc(strlen(cmd->argv[0]) + 1);

        if (executable_path == NULL)
        {
            perror("malloc");
            return;
        }

        strcpy(executable_path, cmd->argv[0]);
    }
    else
    {
        //Component 3
        const char *path_env = getenv("PATH");

        if (path_env == NULL)
        {
            fprintf(stderr, "tush: PATH is not set\n");
            return;
        }

        executable_path =
            find_executable_path(cmd->argv[0], path_env);

        if (executable_path == NULL)
        {
            fprintf(stderr, "tush: %s: command not found\n",
                    cmd->argv[0]);
            return;
        }
    }

    //Component 2
    pid_t pid = fork();

    if (pid < 0)
    {
        //fork() failed
        perror("fork");
        free(executable_path);
        return;
    }

    if (pid == 0)
    {
        //CHILD
        //execv() replaces this child process with the requested program
        execv(executable_path, cmd->argv);

        //If we get here, execv() failed.
        perror("execv");

        //The child doesn't return to the shell loop
        _exit(1);
    }

    //Wait for the foreground child to finish
    int status;

    if (waitpid(pid, &status, 0) < 0)
    {
        perror("waitpid");
    }

    free(executable_path);
}

//component 1
int main(void)
{
    //storage for input
    char line[1024];

    while (1)
    {
        printf("tush> ");
        fflush(stdout);

        //read line and checks for EOF
        if (fgets(line, sizeof(line), stdin) == NULL)
        {
            //EOF, such as Ctrl-D
            printf("\n");
            break;
        }

        //Give the line to the parser
        CommandLine *cl = parse_command_line(line);

        //NULL means the line was empty or invalid
        if (cl == NULL)
        {
            continue;
        }

        //Pipes not implemented yet
        if (cl->has_pipe)
        {
            fprintf(stderr, "Pipes are not implemented yet\n");
            free_command_line(cl);
            continue;
        }

        //Background not implemented yet
        if (cl->background)
        {
            fprintf(stderr,"Background not implemented yet\n");
            free_command_line(cl);
            continue;
        }

        int builtin_result = handle_builtin(&cl->left);

        //exit was entered
        if (builtin_result == -1) {
            free_command_line(cl);
            break;
        }

        //executes non-built-in commands
        if (builtin_result == 0) {
            execute_command(&cl->left);
        }
        //dynamically allocates so free memory
        free_command_line(cl);
    }

    return 0;
}

int handle_builtin(Command *cmd)
{
    if (cmd->argc == 0) {
        return 0;
    }

    //exit
    if (strcmp(cmd->argv[0], "exit") == 0) {
        if (cmd->argc != 1) {
            fprintf(stderr, "exit: too many arguments\n");
            return 1;
        }

        return -1;
    }

    //cd
    if (strcmp(cmd->argv[0], "cd") == 0) {
        if (cmd->argc > 2) {
            fprintf(stderr, "cd: too many arguments\n");
            return 1;
        }

        const char *directory;

        if (cmd->argc == 1) {
            directory = getenv("HOME");

            if (directory == NULL) {
                fprintf(stderr, "cd: HOME not set\n");
                return 1;
            }
        } else {
            directory = cmd->argv[1];
        }

        if (chdir(directory) != 0) {
            perror("cd");
        }

        return 1;
    }

    return 0;
}