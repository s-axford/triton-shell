/*
    Triton Shell
    Created by: Spencer Axford
    Contains: 
    Cancellation
    History
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
        char *buf;
        size_t max_buf_size = 128;

        buf = (char *)calloc(max_buf_size, sizeof(char));
        if (buf == NULL)
        {
            err(-1, "Unable to allocate buffer");
        }

        int buf_size = prompt_user(buf, max_buf_size);

        char *c_buf = buf;              // copy of buf that can be iterated through
        char *args[max_num_args];       // will store all argument strings
        int si_rows = 5;
        int section_info[max_num_args][si_rows]; // will store start and end indexes of args and relevant pid
                                        // [0] = pid, [1] = start index, [2] end index
                                        // [3] = input [4] output
        int n_args = 0;                 // total number of arguments - pipes + null terminator
        int pipes = 0;                  // number of "|" chars
        section_info[0][1] = n_args; // start of first command
        while (c_buf != NULL && n_args < max_num_args - 1)
        {
            char *arg = strsep(&c_buf, &delim);
            if (strcmp(arg, "|") == 0) // pipe found
            {
                args[n_args] = NULL; // swap '|' for NULL (to end execvp)
                section_info[pipes][2] = n_args; // this NULL is the end index of the last section
                n_args += 1;
                pipes += 1; // there is a new section
                section_info[pipes][1] = n_args; // next arg is the start of new section
            }
            else
            {
                args[n_args] = arg;
                n_args += 1;
            }
        }
        section_info[pipes][2] = n_args; // end index of last process
        args[n_args] = NULL; // null ptr for end of args

        int req_children = pipes + 1;

        // set up pipes and input output fd's for each child
        section_info[0][3] = STDIN_FILENO;
        int pipe_fds[pipes*2];
        for (int i = 0; i < req_children; i++) {
            int fildes[2];
            pipe(fildes);
            section_info[i][4] = fildes[1];
            section_info[i + 1][3] = fildes[0];
            pipe_fds[i] = fildes[0];
            pipe_fds[i*2] = fildes[1];
        }
        section_info[pipes][4] = STDOUT_FILENO;
  
        // save secton info to memory so it may be shared by children
        int *si[req_children];
        for (int i = 0; i < req_children; i++)
            si[i] = (int *)malloc(si_rows * sizeof(int)); 
    
        // copy data from section_info to si
        for (int i = 0; i <  req_children; i++) 
        for (int j = 0; j < si_rows; j++) 
            si[i][j] = section_info[i][j]; // or *(*(arr+i)+j)

        int current_child = 0;
        for (int i = 0; i < req_children; i++)
        {   
            pid_t child = fork();
            children += 1;
            
            section_info[i][0] = child;
            si[i][0] = child;

            // // print si
            // for (int i = 0; i <  req_children; i++) 
            // for (int j = 0; j < si_rows; j++)
            //     printf("si[%i][%i]: %d \n", i, j, si[i][j]);

            switch (child)
            {
            case -1:
                err(-1, "Error in fork()");

            case 0:
            {
                int s_index = section_info[current_child][1];
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
                int status;
                char *com = strcat(buf, "\n");
                if (write(his_fd, com, buf_size + 1) == -1)
                {
                    err(-1, "Write to ~/.triton_history failed\n");
                }
                if (waitpid(child, &status, 0) == -1)
                {
                    err(-1, "Failed to waitpid()");
                }
                if (WIFEXITED(status))
                {
                    printf("program exited with code: %d\n", WEXITSTATUS(status));
                    children -= 1;
                }
                if (WIFSIGNALED(status))
                {
                    printf("\nprocess terminated\n");
                    children -= 1;
                }
                break;
            }
            }
            current_child += 1;
        }
        // free memory
        free(buf);
        for (int i = 0; i < req_children; i++)
        {
            free(si[i]);
        }

        //close any pipe fd's
        for(int i = 0; i < pipes*2; i++) {
            close(pipe_fds[i]);
        }
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
