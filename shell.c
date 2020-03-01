#include <sys/types.h>
#include <sys/wait.h>

#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

char* prompt_user(char * buffer, size_t bufsize) {
    write(1,"triton> ", 8);
    size_t chars = getline(&buffer, &bufsize, stdin);
    if ((buffer)[chars - 1] == '\n') {
        (buffer)[chars - 1] = '\0';
    }
    return buffer;
}

int main(int argc, char *argv[])
{
    const int max_num_args = 50;
    const char delim = ' ';
    while(1){
        char *buf;
        char* c_buf = buf;
        size_t bufsize = 128;
        buf = (char *)calloc(bufsize,sizeof(char));
        if( buf == NULL)
        {
            err(-1, "Unable to allocate buffer");
        }

        prompt_user(buf, bufsize);
        
        pid_t child = fork();

        switch (child){
            case -1:
                err(-1, "Error in fork()");

            case 0: {
                char* c_buf = buf;
                char* args[max_num_args];
                int n_args = 0;
                while (c_buf != NULL && n_args < max_num_args-1){
                    char* arg = strsep(&c_buf, &delim);
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
                if (waitpid(child, &status, 0) == -1) {
                    err(-1, "Failed to waitpid()");
                }
                if (WIFEXITED(status)) {
                    printf("exited with code: %d\n", WEXITSTATUS(status));
                }
                free(buf);
                break;
            }
        }
    }
    return 0;
}
