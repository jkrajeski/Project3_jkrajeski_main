# Project3_Building-a-Linux-shell

# **Overview**

In the first two projects of this course, you built foundational systems programming skills: reading and writing files with cat, and navigating the filesystem and its metadata with ls. Both projects gave you direct experience with the POSIX API and the Unix philosophy of treating nearly everything as a file. Now it is time to put those skills to work in a more ambitious setting.

In this project, you will build **tush** — the **T**emple**U**niversity **SH**ell. A shell is the command-line interpreter that sits between you and the operating system kernel. Every time you type ls -l or grep foo bar.txt | sort | uniq into a terminal, a shell program is doing the work of parsing your input, locating the program you asked for, setting up pipes and redirections, and launching processes. By building one yourself, you will gain a concrete and lasting understanding of how processes are created and managed, how the kernel’s I/O model works, and how the programs you run every day actually come to life.

Your shell will be non-trivial but tractable: it will support command execution, PATH-based program lookup, input/output redirection, background processes, and a single pipeline stage. It will not need to support every feature of bash, but when you are finished, it will be a real, working shell that you could actually use.

# **Background**

## **What Does a Shell Actually Do?**

At its core, a shell executes a simple loop — often called the read-eval-print loop (REPL):  
1.     Print a prompt and read a line of input from the user.  
2.     Parse the input into a command and its arguments.  
3.     Execute the command.  
4.     Go back to step 1.  

Each “command” is ultimately just a program stored somewhere on the filesystem. When you type ls, the shell finds the file /bin/ls, creates a new child process, loads ls into that process, and waits for it to finish. The elegance of Unix is that this mechanism — fork and exec — is uniform and general. Pipelines, redirection, and background execution are all built on top of this same foundation.

## **Provided: The Command-Line Parser**

Parsing a shell command line correctly is surprisingly tricky. Handling whitespace, special characters, and operator tokens in a robust way would be a project on its own. To keep the focus on the operating systems concepts — process management, file descriptors, and inter-process communication — the command-line parser is provided to you as a helper module.

The parser takes a raw input string and returns a structured CommandLine value. Its interface is documented in parser.h. You are encouraged to read the parser code, but you are not responsible for modifying or debugging it. Everything built on top of the parser is your responsibility.

# **Components**

## **Component 1 — Shell Skeleton and Command Decoding**

Before any process is launched, your shell must be running and able to make sense of what the user typed. Your first task is to build the skeleton of the shell and wire it up to the parser.
Your shell should:  
•       Print a prompt (e.g., tush> ) to stdout.  
•       Read a line of input using fgets or getline.  
•       Pass the raw input string to the provided parser.  
•       Walk the resulting parse structure and print a clean, human-readable summary of what was parsed — the command name, each argument, any redirection targets, whether the command is backgrounded, and whether there is a pipeline.  

This “echo back” behavior is your sanity check. You are not executing anything yet; you are confirming that you understand the data structures the parser gives you before you build on top of them. Do not move on to Component 2 until the parser output makes sense to you in every case.

**Deliverable:** A shell that prompts, reads, parses, and accurately describes the structure of any command the user types, then loops.

## **Component 2 — Basic Process Creation with fork/execv**

With the parse structure in hand, you can now actually run programs. This component introduces the two most important system calls in this project.

fork() creates an exact copy of the calling process. After fork, there are two processes — parent and child — both running the same code. The return value of fork tells you which one you are: the parent receives the child’s PID, and the child receives zero.

execv(path, argv) replaces the current process image with a new program loaded from path, passing it the argument vector argv. Critically, execv does not return on success — the calling process *becomes* the new program.

In this component, you will support commands specified with **absolute paths only** — for example, /bin/ls or /usr/bin/wc. This sidesteps the question of how to find a program and lets you focus purely on the mechanics of launching it.

Your shell should:  
•       Fork a child process.  
•       In the child, build the argv array from the parse structure and call execv.  
•       In the parent, call waitpid to wait for the child to finish before prompting again.  
•       Handle errors: if execv fails, the child must print an error message and call exit — it must not return to the shell loop.  

Pay close attention to the **fork/exec/wait pattern**. A common mistake is letting the child process fall through into the shell loop after exec fails, or forgetting to wait in the parent and creating zombie processes. Get this right before proceeding.

**Deliverable:** A shell that executes programs given as absolute paths, waits for them to complete, and handles errors gracefully.

## **Component 3 — Program Lookup with PATH**

Requiring users to type /bin/ls instead of ls would be maddening. In this component, you will implement find_executable_path, which resolves a simple command name like ls to the full path of its executable.

The environment variable PATH contains a colon-separated list of directories to search — for example:

|   /usr/local/bin:/usr/bin:/bin  |
|---|

When the user types ls, the shell searches each directory in PATH order, checking whether an executable file named ls exists in that directory. The first match wins. If no match is found, the shell should report an error.

You will implement this function yourself:  

|   char *find_executable_path(const char *command, const char *path_env);  |
|---|

It should:

•       Retrieve and parse the PATH variable using getenv.  
•       For each directory in PATH, construct a candidate full path and test it with access(candidate, X_OK).  
•       Return a heap-allocated string containing the full path on success, or NULL on failure.  
•       The caller is responsible for freeing the returned string.  

Once find_executable_path works, integrate it into your shell so that simple command names are resolved automatically before the execv call.

**Deliverable:** A shell that accepts simple command names, searches PATH correctly, and reports a clear error when a command is not found.

## **Component 4 — Redirection and Background Processing**

Unix programs communicate through **file descriptors**: small integers that refer to open files, pipes, sockets, and other I/O objects. By convention, file descriptor 0 is stdin, 1 is stdout, and 2 is stderr. The shell’s power to redirect I/O comes entirely from manipulating these descriptors *in the child process, before exec is called*.

### **Input and Output Redirection**

The < operator redirects stdin: instead of reading from the terminal, the program reads from a file. The > operator redirects stdout to a file, creating or truncating it. The mechanism is the same in both cases:

1.     Open the file with appropriate flags using open.  
2.     Use dup2(fd, 0) or dup2(fd, 1) to replace the standard descriptor with the opened file.  
3.     Close the original file descriptor.  
4.     Call execv — the new program inherits the redirected descriptors without knowing anything changed.  

All of this happens in the child, after fork but before exec. The parent’s file descriptors are not affected.

### **Background Processing**

Normally your shell waits for each command to finish (waitpid with no flags). When the user appends & to a command, the shell should launch the process and return immediately to the prompt without waiting.

For background processes in this project:

•       Do not call waitpid immediately after fork for background jobs.  
•       Print the child’s PID so the user knows what was launched.  
•       To avoid accumulating zombie processes, call waitpid with WNOHANG at the top of your main loop to reap any children that have finished since the last prompt.  

**Deliverable:** A shell that supports < and > redirection and & background execution, with correct file descriptor management and zombie cleanup.

## **Component 5 — Pipelines**

A pipeline like ls -l  |  grep txt connects two programs so that the stdout of the left-hand command feeds directly into the stdin of the right-hand command. Your shell is required to support **a single pipe operator** on a command line. Chains of three or more stages are not required.

What **is** required — and what makes this component genuinely challenging — is that **redirection and piping can be combined**. A command like:  

   **grep foo < input.txt | sort > output.txt**  

**must work correctly** : grep reads from input.txt via <, its output flows through the pipe to sort, and sort writes to output.txt via >. Getting this right requires careful, deliberate management of file descriptors in each child process.

### **Mechanics**

The pipe() system call takes an array of two integers and fills them with a connected pair of file descriptors: pipefd[0] is the read end and pipefd[1] is the write end. Anything written to pipefd[1] can be read from pipefd[0].

To execute a single pipeline stage:

1.     Call pipe(pipefd) in the parent **before** forking.  
2.     Fork the **left-hand child**. In that child: use dup2(pipefd[1], 1) to redirect stdout to the pipe’s write end; apply any additional redirection (e.g., <); close both pipefd[0] and pipefd[1]; call execv.  
3.     Fork the **right-hand child**. In that child: use dup2(pipefd[0], 0) to redirect stdin to the pipe’s read end; apply any additional redirection (e.g., >); close both pipefd[0] and pipefd[1]; call execv.  
4.     In the **parent**, close both pipefd[0] and pipefd[1], then call waitpid for both children.  

### **The Critical Rule: Close Every Unused Pipe End**

A process reading from a pipe will block indefinitely if any process anywhere in the system still holds an open copy of the write end — even if that process never writes a single byte. This means:

•       The **parent** must close both pipe ends after forking, since it does not participate in the data transfer.  
•       Each **child** must close both raw pipe file descriptors after dup2 has wired up the one it needs.  
•       If a child also has I/O redirection, that redirection must be applied *in addition to* the pipe wiring — the two mechanisms are fully composable.  

**Neglecting to close even one file descriptor on the write end will cause the reading process to hang.** This is the single most common bug in this component. Before writing any code, **draw the two child processes, all file descriptors, and the pipe, and trace every dup2 and close call explicitly**. Students who do this on paper first consistently finish this component faster than those who dive straight into code.

### **Testing**

Test in this order:  

1.     A bare pipe with no redirection: ls -l | grep txt.  
2.     Redirection on the left side only: grep foo < input.txt | sort  
3.     Redirection on the right side only: ls -l | grep txt > output.txt  
4.     Redirection on both sides: grep foo < input.txt | sort > output.txt  

Each case exercises a different combination of file descriptor operations. Make sure all four work before submitting.

**Deliverable:** A shell that correctly executes a single pipeline with optional input/output redirection on either or both sides, with proper pipe and file descriptor cleanup in all processes.

# **Built-in Commands**

A small number of commands must be handled directly by the shell process rather than by a child, because they need to change the shell’s own state. You should implement at minimum:

•       **exit** — terminate the shell.  
•       **cd [dir]** — change the shell’s working directory using chdir. If cd were run in a child, the child’s directory would change but the shell’s would not — chdir only affects the calling process.  

Check for built-in commands **before** calling fork.

# **Error Handling**

Good error handling is not optional. For every system call that can fail — fork, execv, open, pipe, dup2, chdir — check the return value and respond appropriately. Use perror or strerror(errno) to produce informative messages. A program that silently ignores errors and produces mysterious behavior is far harder to debug than one that fails loudly and clearly.

# **Suggested Development Order**

Work through the components in order. Each one builds directly on the previous, and each has a clear testable deliverable. Do not move to the next component until the current one is solid.

|   **Phase**  |   **Component**  |   **What to verify before continuing**  |  
|---|---|---|
|   1  |   Parser integration  |   Every command type is decoded and printed correctly  |
|   2  |   fork/execv  |   Absolute-path programs run; zombies are reaped; exec errors are caught  |
|   3  |   PATH lookup  |   Simple names resolve correctly; bad names produce a clear error  |
|   4  |   Redirection & &  |   Files are created/read correctly; background PIDs are printed  |
|   5  |   Pipeline  |   Single pipe executes correctly; redirection composes on both sides; shell does not hang  |

# **Appendix: Useful System Calls — Quick Reference**

|   **System Call**  |   **Purpose**  |  
|---|---|
|   fork  |   Create a child process  |
|   execv  |   Replace process image with a new program  |
|   waitpid  |   Wait for a child process to change state  |
|   pipe  |   Create a pair of connected file descriptors  |
|   dup2  |   Copy a file descriptor to a specific number  |
|   open / close  |   Open and close files  |
|   access  |   Test file existence and permissions  |
|   getenv  |   Read an environment variable  |
|   chdir  |   Change working directory  |
|   perror  |   Print a human-readable error message  |

**Project 3 Deliverables**

 
**Deliverable 1 — End of Week 1 (Day 7): The Executing Shell**
This checkpoint demonstrates Components 1, 2, and 3 together. By this point you should have a shell that prompts, reads input, resolves command names through PATH, forks a child, executes the program, and waits for it to finish. You should be able to use it to run programs. It excludes all the file descriptor work, to be done next.

**Deliverable 2 — End of Week 2 (Day 14): Redirection and Background**

This checkpoint adds Component 4. You should now have a shell that handles `<`, `>`, and `&` correctly on top of everything from Deliverable 1. You should be able to test this: run `ls > out.txt`, verify the file, run `sort < out.txt`, run `sleep 10 &` and confirm the prompt returns immediately.

**Final Submission — End of Week 3 (Day 21): The Complete Shell**

The final week is devoted entirely to Component 5 — the pipe and its combination with redirection. The pipe is where the most debugging time will go. The complete redirection/pipe case (`grep foo < in.txt | sort > out.txt`) requires getting four separate file descriptor operations right simultaneously. 
