#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>

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

char *find_executable_path(const char *command, const char *path_env) {
    if (command == NULL || path_env == NULL) {
        return NULL;
    }

    const char *start = path_env;

    while (1) {
        //Find the end of the current directory in PATH
        const char *end = strchr(start, ':');

        size_t dir_length;

        if (end == NULL) {
            dir_length = strlen(start);
        } else {
            dir_length = (size_t) (end - start);
        }

        //An empty PATH entry means the current directory
        if (dir_length == 0) {
            dir_length = 1;
        }

        //candidate will contain something like: /usr/bin/ls
        //We need directory + "/" + command + '\0'

        size_t command_length = strlen(command);

        size_t candidate_length;

        if (dir_length == 1 && start[0] == ':') {
            candidate_length = command_length + 2;
        } else {
            candidate_length = dir_length + 1 + command_length + 1;
        }

        char *candidate = malloc(candidate_length);

        if (candidate == NULL) {
            perror("malloc");
            return NULL;
        }

        //Handle an empty PATH entry
        if (start[0] == ':' && dir_length == 1) {
            snprintf(candidate, candidate_length, "./%s", command);
        } else {
            snprintf(candidate, candidate_length, "%.*s/%s",
                     (int) dir_length, start, command);
        }

        // access checks whether the file exists and can be executed
        if (access(candidate, X_OK) == 0) {
            return candidate;
        }

        free(candidate);

        //end of path
        if (end == NULL) {
            break;
        }

        start = end + 1;
    }
    return NULL;
}


static void execute_command(Command *cmd, int background) {
    if (cmd == NULL || cmd->argc == 0) {
        return;
    }

    char *executable_path = NULL;

    //if argv[0] contains /, use it directly
    if (strchr(cmd->argv[0], '/') != NULL) {
        executable_path = malloc(strlen(cmd->argv[0]) + 1);

        if (executable_path == NULL) {
            perror("malloc");
            return;
        }

        strcpy(executable_path, cmd->argv[0]);
    } else {
        //Component 3
        //get full path
        const char *path_env = getenv("PATH");

        if (path_env == NULL) {
            fprintf(stderr, "tush: PATH is not set\n");
            return;
        }

        executable_path =
                find_executable_path(cmd->argv[0], path_env);

        if (executable_path == NULL) {
            fprintf(stderr, "tush: %s: command not found\n",
                    cmd->argv[0]);
            return;
        }
    }

    //Component 2
    pid_t pid = fork();

    if (pid < 0) {
        //fork() failed
        perror("fork");
        free(executable_path);
        return;
    }

    if (pid == 0) {
        //CHILD
        if (cmd->input_file != NULL) {
            int fd = open(cmd->input_file, O_RDONLY);

            if (fd < 0) {
                perror("open");
                _exit(1);
            }

            if (dup2(fd, STDIN_FILENO) < 0) {
                perror("dup2");
                close(fd);
                _exit(1);
            }

            close(fd);
        }

        if (cmd->output_file != NULL) {
            int fd = open(
                cmd->output_file,
                O_WRONLY | O_CREAT | O_TRUNC,
                0644
            );

            if (fd < 0) {
                perror("open");
                _exit(1);
            }

            if (dup2(fd, STDOUT_FILENO) < 0) {
                perror("dup2");
                close(fd);
                _exit(1);
            }

            close(fd);
        }
        //execv() replaces this child process with the requested program
        execv(executable_path, cmd->argv);

        //If we get here, execv() failed.
        perror("execv");

        //The child doesn't return to the shell loop
        _exit(1);
    }

    //Wait for the child to finish if its not a background command
    if (!background) {
        if (waitpid(pid, NULL, 0) < 0) {
            perror("waitpid");
        }
    } else {
        printf("%d\n", pid);
    }

    free(executable_path);
}


void reap_background_processes(void) {
    while (1) {
        pid_t result = waitpid(-1, NULL, WNOHANG);

        if (result > 0) {
            // A background child was reaped
            continue;
        }

        if (result == 0) {
            //A child exists, but it hasn't finished yet
            break;
        }

        if (errno == ECHILD) {
            //No children exist
            break;
        }

        perror("waitpid");
        break;
    }
}

//component 5
static void execute_pipeline(Command *left, Command *right, int background){
    int pipefd[2];

    //pipefd[0] = read end
    //pipefd[1] = write end
    if (pipe(pipefd) < 0) {
        perror("pipe");
        return;
    }

    //Create the LEFT child.
    pid_t left_pid = fork();

    if (left_pid < 0) {
        perror("fork");
        close(pipefd[0]);
        close(pipefd[1]);
        return;
    }

    if (left_pid == 0) {
        //LEFT CHILD
        if (dup2(pipefd[1], STDOUT_FILENO) < 0) {
            perror("dup2");
            _exit(1);
        }

        if (left->input_file != NULL) {
            int fd = open(left->input_file, O_RDONLY);

            if (fd < 0) {
                perror("open");
                _exit(1);
            }

            if (dup2(fd, STDIN_FILENO) < 0) {
                perror("dup2");
                close(fd);
                _exit(1);
            }

            close(fd);
        }

        close(pipefd[0]);
        close(pipefd[1]);

        //Run the left command
        char *left_path = NULL;

        // If the command already contains '/', use it directly.
        if (strchr(left->argv[0], '/') != NULL) {
            left_path = left->argv[0];
        } else {
            left_path = find_executable_path(left->argv[0], getenv("PATH"));

            if (left_path == NULL) {
                fprintf(stderr, "%s: command not found\n", left->argv[0]);
                _exit(1);
            }
        }

        execv(left_path, left->argv);
        perror("execv");

        // Only free the path if find_executable_path() allocated it.
        if (strchr(left->argv[0], '/') == NULL) {
            free(left_path);
        }

        _exit(1);
    }

    //Create the RIGHT child.
    pid_t right_pid = fork();

    if (right_pid < 0) {
        perror("fork");
        close(pipefd[0]);
        close(pipefd[1]);
        waitpid(left_pid, NULL, 0);
        return;
    }

    if (right_pid == 0) {
        //RIGHT CHILD
        if (dup2(pipefd[0], STDIN_FILENO) < 0) {
            perror("dup2");
            _exit(1);
        }

        if (right->output_file != NULL) {
            int fd = open(right->output_file,
                          O_WRONLY | O_CREAT | O_TRUNC,
                          0644);

            if (fd < 0) {
                perror("open");
                _exit(1);
            }

            if (dup2(fd, STDOUT_FILENO) < 0) {
                perror("dup2");
                close(fd);
                _exit(1);
            }

            close(fd);
        }

        close(pipefd[0]);
        close(pipefd[1]);

        //Run the right command
        char *right_path = NULL;

        // If the command already contains '/', use it directly.
        if (strchr(right->argv[0], '/') != NULL) {
            right_path = right->argv[0];
        } else {
            right_path = find_executable_path(right->argv[0], getenv("PATH"));

            if (right_path == NULL) {
                fprintf(stderr, "%s: command not found\n", right->argv[0]);
                _exit(1);
            }
        }

        execv(right_path, right->argv);
        perror("execv");

        // Only free the path if find_executable_path() allocated it.
        if (strchr(right->argv[0], '/') == NULL) {
            free(right_path);
        }

        _exit(1);
    }

    close(pipefd[0]);
    close(pipefd[1]);

    //wait if not background
    if (!background) {
        if (waitpid(left_pid, NULL, 0) < 0) {
            perror("waitpid");
        }

        if (waitpid(right_pid, NULL, 0) < 0) {
            perror("waitpid");
        }
    } else {
        printf("%d %d\n", left_pid, right_pid);
    }
}

//component 1
int main(void) {
    //storage for input
    char line[1024];

    while (1) {
        reap_background_processes();

        printf("tush> ");
        fflush(stdout);

        //read line and checks for EOF
        if (fgets(line, sizeof(line), stdin) == NULL) {
            //EOF, such as Ctrl-D
            printf("\n");
            break;
        }

        //Give the line to the parser
        CommandLine *cl = parse_command_line(line);

        //NULL means the line was empty or invalid
        if (cl == NULL) {
            continue;
        }

        if (cl->has_pipe) {
            execute_pipeline(&cl->left, &cl->right, cl->background);
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
            execute_command(&cl->left, cl->background);
        }
        //dynamically allocates so free memory
        free_command_line(cl);
    }

    return 0;
}

int handle_builtin(Command *cmd) {
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