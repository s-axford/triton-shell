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

void    sig_int_handler(); // handles ctrl - c SIGINT terminations
size_t   prompt_user(char * buffer, size_t bufsize); // prompts user for command

int children; // number of child processes
int parent_pid; // top parent pid number

int main(int argc, char *argv[]) {
    // interrupt handler setup
    signal(SIGINT,sig_int_handler);

    // history file setup
    char* history_path = strcat(getenv("HOME"), "/.triton_history");
    int his_fd = open(history_path, O_RDWR | O_CREAT | O_APPEND, 0640 ); 
    if (his_fd == -1) {
        err(-1, "failed to opened or create ~/.triton_history\n");
    }
    const int max_num_args = 50;
    const char delim = ' ';
    children = 0;
    parent_pid = getpid();
    while(true) {
        char *buf;
        size_t max_buf_size = 128;
        
        buf = (char *)calloc(max_buf_size,sizeof(char));
        if( buf == NULL) {
            err(-1, "Unable to allocate buffer");
        }

        int buf_size = prompt_user(buf, max_buf_size);
        
        pid_t child = fork();

        switch (child){
            case -1:
                err(-1, "Error in fork()");

            case 0: {
                children = 0;
                char* args[max_num_args];
                int n_args = 0;
                while (buf != NULL && n_args < max_num_args-1) {
                    char* arg = strsep(&buf, &delim);
                    args[n_args] = arg;
                    n_args = n_args + 1;
                }
                args[n_args] = NULL;
                // history command
                if (strcmp(args[0], "history") == 0) {              
                    char c;
                    int offset = 0;
                    // read char from history file
                    while( pread(his_fd, &c, sizeof(char), offset) > 0 ) {
                        offset += 1;
                        // write char to stdout
                        if((write(STDOUT_FILENO, &c, 1)) < 0) {
                            err(-1, "Unable to write to stdout\n");
                        }
                    }
                    exit(0);
                }
                else { /* default: */
                    execvp(args[0], args);
                    err(-1, "Failed to execute binary\n");
                    break;
                }
            }

            default: {
                int status;
                children += 1;
                char* com = strcat(buf, "\n");
                if (write(his_fd, com, buf_size+1) == -1) {
                    err(-1, "Write to ~/.triton_history failed\n");
                }
                if (waitpid(child, &status, 0) == -1) {
                    err(-1, "Failed to waitpid()");
                }
                if (WIFEXITED(status)) {
                    printf("program exited with code: %d\n", WEXITSTATUS(status));
                    children -= 1;
                }
                if (WIFSIGNALED(status)) {
                    printf("\nprocess terminated\n");
                    children -= 1;
                }
                free(buf);
                break;
            }
        }
    }
    return 0;
}

void sig_int_handler() {
    // if the process is a child process, terminate
    if (getpid() != parent_pid) {
        exit(128 + SIGINT);
    } else {
        // if this is the parent process, and there are no remaining children, terminate
        if (children == 0) {
            exit(128 + SIGINT);
        }
    }
}

size_t prompt_user(char * buffer, size_t max_buf_size) {
    write(1,"triton> ", 8);
    size_t chars = getline(&buffer, &max_buf_size, stdin);
    // remove new-line character
    if ((buffer)[chars - 1] == '\n') {
        (buffer)[chars - 1] = '\0';
    }
    return chars;
}
