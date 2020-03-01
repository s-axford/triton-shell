/*
    Triton Shell
    Created by: Spencer Axford
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
char*   prompt_user(char * buffer, size_t bufsize); // prompts user for command

int children; // number of child processes
int parent_pid; // top parent pid number

int main(int argc, char *argv[]) {
    // interrupt handler setup
    signal(SIGINT,sig_int_handler);
    const int max_num_args = 50;
    const char delim = ' ';
    children = 0;
    parent_pid = getpid();
    while(true) {
        char *buf;
        char* c_buf = buf;
        size_t bufsize = 128;
        
        buf = (char *)calloc(bufsize,sizeof(char));
        if( buf == NULL) {
            err(-1, "Unable to allocate buffer");
        }

        prompt_user(buf, bufsize);
        
        pid_t child = fork();

        switch (child){
            case -1:
                err(-1, "Error in fork()");

            case 0: {
                children = 0;
                char* args[max_num_args];
                int n_args = 0;
                while (buf != NULL && n_args < max_num_args-1){
                    char* arg = strsep(&buf, &delim);
                    args[n_args] = arg;
                    n_args = n_args + 1;
                }
                args[n_args] = NULL;
                execvp(args[0], args);
                err(-1, "Failed to execute binary");
                break;
            }

            default: {
                int status;
                children += 1;
                if (waitpid(child, &status, 0) == -1) {
                    err(-1, "Failed to waitpid()");
                }
                if (WIFEXITED(status)) {
                    printf("exited with code: %d\n", WEXITSTATUS(status));
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
    // If the process is a child process, terminate
    if (getpid() != parent_pid) {
        exit(128 + SIGINT);
    } else {
        // If this is the parent process, and there are no remaining children, terminate
        if (children == 0) {
            exit(128 + SIGINT);
        }
    }
}

char* prompt_user(char * buffer, size_t bufsize) {
    write(1,"triton> ", 8);
    size_t chars = getline(&buffer, &bufsize, stdin);
    if ((buffer)[chars - 1] == '\n') {
        (buffer)[chars - 1] = '\0';
    }
    return buffer;
}