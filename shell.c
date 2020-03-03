/*
    Triton Shell
    Created by: Spencer Axford
    Contains: 
    Cancellation
    History
    Pipes
    Redirection
*/

#include <sys/types.h>
#include <sys/wait.h>

#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>

void sig_int_handler();                           // handles ctrl - c SIGINT terminations
size_t prompt_user(char *buffer, size_t bufsize); // prompts user for command

int children;   // number of child processes
int parent_pid; // top parent pid number

int main(int argc, char *argv[])
{
    // interrupt handler setup
    signal(SIGINT, sig_int_handler);

    // history file setup
    char *history_path = strcat(getenv("HOME"), "/.triton_history");
    int his_fd = open(history_path, O_RDWR | O_CREAT | O_APPEND, 0640);
    if (his_fd == -1)
    {
        err(-1, "failed to opened or create ~/.triton_history\n");
    }
    const int max_num_args = 50;
    const char delim = ' ';
    children = 0;
    parent_pid = getpid();
    while (true)
    {
        // buffer to store incoming command
        char *buf;
        size_t max_buf_size = 128;

        buf = (char *)calloc(max_buf_size, sizeof(char));
        if (buf == NULL)
        {
            err(-1, "Unable to allocate buffer");
        }

        int buf_size = prompt_user(buf, max_buf_size);

        if (write(his_fd, buf, buf_size) == -1)
        {
            err(-1, "Write to ~/.triton_history failed\n");
        }
        if (write(his_fd, "\n", 1) == -1)
        {
            err(-1, "Write newline to ~/.triton_history failed\n");
        }

        char *buf_p = buf;         // new pointer for free() since buf will change in strsep

        char *args[max_num_args];       // array of command line arguments
        
        // section information data structure will be used to connect pipes
        int si_rows = 4;
        int section_info[max_num_args][si_rows]; // will store start index and pipe fd's
                                        // [0] = start index, [1] = input, [2] output, [3] error
        int n_args = 0;                 // total number of arguments - pipes + null terminator
        int pipes = 0;                  // number of "|" chars
        int err_fd = STDERR_FILENO;
        int out_fd = STDOUT_FILENO;
        int in_fd = STDIN_FILENO;
        section_info[0][0] = n_args; // start of first command
        for(int i = 0; i < max_num_args; i++)
        {
            section_info[i][1] = in_fd;
            section_info[i][2] = out_fd;
            section_info[i][3] = err_fd;
        }
        while (buf != NULL && n_args < max_num_args - 1)
        {
            char *arg = strsep(&buf, &delim);
            if (strcmp(arg, "|") == 0) // pipe found
            {
                args[n_args] = NULL; // swap '|' for NULL (to end execvp)
                n_args += 1;
                pipes += 1; // there is a new section
                section_info[pipes][0] = n_args; // next arg is the start of new section
            }
            else if (strcmp(arg, ">") == 0) // output redirection found
            {
                char *arg = strsep(&buf, &delim);
                out_fd = open(arg, O_RDWR | O_CREAT | O_APPEND, 0640);
                if (out_fd == -1)
                {
                    err(-1, "failed to opened or create output log file\n");
                }
                section_info[pipes][2] = out_fd; // output is changed to new file
            }
            else if (strcmp(arg, "2>") == 0) // error redirection found
            {
                char *arg = strsep(&buf, &delim);
                err_fd = open(arg, O_RDWR | O_CREAT | O_APPEND, 0640);
                if (err_fd == -1)
                {
                    err(-1, "failed to opened or create error log file\n");
                }
                section_info[pipes][3] = err_fd; // error is changed to new file
            }
            else
            {
                args[n_args] = arg;
                n_args += 1;
            }
        }
        args[n_args] = NULL; // null ptr for end of args

        int req_children = pipes + 1;

        // set up pipes and input output fd's for each child
        int pipe_fds[pipes*2];

        // if cli pipes were found, create pipes and store fd's in section_info
        if (pipes > 0) {
            for (int i = 0; i < req_children; i++) {
                int fildes[2];
                pipe(fildes);
                section_info[i][2] = fildes[1];
                section_info[i + 1][1] = fildes[0];
                pipe_fds[i] = fildes[0];
                pipe_fds[i*2] = fildes[1];
            }
        }  
        int current_child = 0;
        int child_pids[req_children];
        for (int i = 0; i < req_children; i++)
        {   
            pid_t child = fork();

            switch (child)
            {
            case -1:
                err(-1, "Error in fork()");

            case 0:
            {
                // this will swap input / output to the proper pipe if relevant
                dup2(section_info[current_child][1], STDIN_FILENO);
                dup2(section_info[current_child][2], STDOUT_FILENO);
                dup2(section_info[current_child][3], STDERR_FILENO);
                int s_index = section_info[current_child][0];
                // history command
                if (strcmp(args[s_index], "history") == 0)
                {
                    char c;
                    int offset = 0;
                    // read char from history file
                    while (pread(his_fd, &c, sizeof(char), offset) > 0)
                    {
                        offset += 1;
                        // write char to stdout
                        if ((write(STDOUT_FILENO, &c, 1)) < 0)
                        {
                            err(-1, "Unable to write to stdout\n");
                        }
                    }
                    exit(0);
                }
                else
                { /* default: */
                    execvp(args[s_index], args + s_index);
                    err(-1, "Failed to execute binary\n");
                    break;
                }
            }

            default:
            {
                children += 1;
                int status;
                if (waitpid(child, &status, 0) == -1)
                {
                    err(-1, "Failed to waitpid()");
                }
                if (WIFEXITED(status))
                {
                    printf("program %i exited with code: %d\n", child, WEXITSTATUS(status));
                    children -= 1;
                }
                if (WIFSIGNALED(status))
                {
                    printf("\nprocess terminated with SIGINT\n");
                    children -= 1;
                }
                // close pipes
                if (section_info[current_child][1] != 0) {
                    close(section_info[current_child][1]);
                }
                if (section_info[current_child][2] != 1) {
                    close(section_info[current_child][2]);
                }
                break;
            }
            }
            current_child += 1;
        }
        // free buffer
        free(buf_p);
    }
    return 0;
}

void sig_int_handler()
{
    // if the process is a child process, terminate
    if (getpid() != parent_pid)
    {
        exit(128 + SIGINT);
    }
    else
    {
        // if this is the parent process, and there are no remaining children, terminate
        if (children == 0)
        {
            exit(128 + SIGINT);
        }
    }
}

size_t prompt_user(char *buffer, size_t max_buf_size)
{
    write(1, "triton> ", 8);
    size_t chars = getline(&buffer, &max_buf_size, stdin);
    // remove new-line character
    if ((buffer)[chars - 1] == '\n')
    {
        (buffer)[chars - 1] = '\0';
    }
    return chars;
}
