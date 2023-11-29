#include <stdio.h>

#include <stdlib.h>

#include <string.h>

#include <unistd.h>

#include <syslog.h>

#include <signal.h>

#include <sys/resource.h>

#include <fcntl.h>

#include <sys/types.h>

#include <dirent.h>

#include <sys/stat.h>

#include <setjmp.h>

#define BUFSIZE 256

sigjmp_buf jbuf;
pid_t pid;

void SIGQUIT_Handler(int signo) {
    printf("\nSignal Ctrl+z called!\n");
    exit(1);
}

void SIGINT_Handler(int signo, pid_t pid) {
    if (kill(pid, SIGTERM) != 0) {
        printf("\nSignal Ctrl+c called!\n");
    }
}

int main() {
    signal(SIGINT, SIGINT_Handler);
    signal(SIGTSTP, SIGQUIT_Handler);

    int argc;
    int i = 0;

    while (1) {
        char buf[BUFSIZE];
        char * argv[50] = {
            '\0'
        };
        printf("shell > ");
        fgets(buf, sizeof(buf), stdin);
        buf[strlen(buf) - 1] = '\0';

        // exit
        if (strcmp(buf, "exit") == 0) {
            printf("Bye Bye\n");
            exit(0);
        }

        /* Arguments 입력 받기 */
        argc = getargs(buf, argv);
        handler(argc, argv);
    }
}

/*
 * 입력받은 명령어를 구분해서 처리하는 함수
 */
void handler(int argc, char ** argv) {

    int i = 0;
    int isBackground = 0, isRedirectionOrPipe = 0;

    for (i = 0; i < argc; i++) {
        if ((!strcmp(argv[i], ">")) || (!strcmp(argv[i], "<")) || (!strcmp(argv[i], "|"))) {
            isRedirectionOrPipe = 1;
            break;
        } else if (!strcmp(argv[i], "&")) {
            isBackground = 1;
            break;
        }
    }

    if (isBackground) {
        launch(argc, argv);
        isBackground = 0;
    } else if (isRedirectionOrPipe) {
        redirection_pipes(argv, argc);
        isRedirectionOrPipe = 0;
    } else if (!strcmp(argv[0], "mkdir")) {
       mkdir_command(argc, argv);
    } else if (!strcmp(argv[0], "rmdir")) {
        rmdir_command(argc, argv);
    } else if (!strcmp(argv[0], "ln")) {
        ln_command(argc, argv);
    } else if (!strcmp(argv[0], "cp")) {
        cp(argv, argc);
    } else if (!strcmp(argv[0], "rm")) {
        rm(argv, argc);
    } else if (!strcmp(argv[0], "mv")) {
        mv(argv, argc);
    } else if (!strcmp(argv[0], "ls")) {
        ls(argc, argv);
    } else if (!strcmp(argv[0], "cd")) {
        cd(argc, argv);
    } else if (!strcmp(argv[0], "pwd")) {
        pwd();
    } else if (!strcmp(argv[0], "cat")) {
        cat(argc, argv);
    } else {
        launch(argc, argv);
    }
}

void cp(char ** argv, int narg) {
    int sourceFile;
    int copyFile;
    char buf[256];
    ssize_t bytesRead;
    ssize_t totalBytesCopied = 0;
    mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;

    if (narg < 3) {
        fprintf(stderr, "Usage: file_copy source_file copy_file\n");
        //exit(1);
        return;
    }

    if ((sourceFile = open(argv[1], O_RDONLY)) == -1) {
        perror("[ERROR]SRC OPEN");
        //exit(1);
        return;
    }

    if ((copyFile = creat(argv[2], mode)) == -1) {
        perror("[ERROR]DST OPEN");
        //exit(1);
        return;
    }

    while ((bytesRead = read(sourceFile, buf, 256)) > 0) {
        totalBytesCopied += write(copyFile, buf, bytesRead);
    }

    if (bytesRead < 0) {
        perror("[ERROR]READ");
        //exit(1);
    	return;
    }

    close(sourceFile);
    close(copyFile);
}

void rm(char ** argv, int narg) {
    if (narg < 2)
        fprintf(stderr, "Path is not exists\\n");
    else {
        if (remove(argv[1]) < 0) {
            perror("[ERROR] RM");
            exit(EXIT_FAILURE);
        }
    }
}

void mv(char ** argv, int narg) {
    struct stat filebuf;
    char * target;
    char * src_file_name_only;

    if (narg < 3) {
        fprintf(stderr, "Usage: file_rename src target\n");
        //exit(1);
        return;
    }

    if (access(argv[1], F_OK) < 0) {
        fprintf(stderr, "%s not exists\n", argv[1]);
        //exit(1);
        return;
    } else {
        char * slash = strrchr(argv[1], '/');
        src_file_name_only = argv[1];

        if (slash != NULL) {
            src_file_name_only = slash + 1;
        }
    }

    target = (char * ) calloc(strlen(argv[2]) + 1, sizeof(char));
    strcpy(target, argv[2]);

    if (access(argv[2], F_OK) == 0) {
        if (lstat(argv[2], & filebuf) < 0) {
            perror("lstat");
            //exit(1);
            return;
        } else {
            if (S_ISDIR(filebuf.st_mode)) {
                free(target);
                target = (char * ) calloc(strlen(argv[1]) + strlen(argv[2]) + 2, sizeof(char));
                strcpy(target, argv[2]);
                strcat(target, "/");
                strcat(target, src_file_name_only);
            }
        }
    }
    printf("target = %s\n", target);

    if (rename(argv[1], target) < 0) {
        perror("rename");
        //exit(1);
        return;
    }

    free(target);
}

void redirection_pipes(char ** argv, int narg) {

    int fd[2], pid;
    int i, k, pd_idx = 0;
    int command_pos = 0, count_pipe = 0;
    int in = 0, out = 0;
    char * inputFile = NULL, * outputFile = NULL;

    int write_flags = O_WRONLY | O_CREAT | O_TRUNC;
    mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;

    while (argv[command_pos] != NULL) {
        if (argv[command_pos][0] == '|')
            count_pipe++;
        command_pos++;
    }

    for (i = 0; i < count_pipe; i++) {
        if (pipe(fd + i * 2) < 0) {
            perror("[ERROR] PIPE: ");
            exit(EXIT_FAILURE);
        }
    }

    command_pos = 0;

    for (i = 0; i <= count_pipe; i++) {
        int j = 0;
        char ** command = (char ** ) malloc(narg * sizeof(char * ));
        char ** redirection_command = (char ** ) malloc(narg * sizeof(char * ));

        if (!strcmp(argv[command_pos], "|"))
            command_pos++;

        if (i < count_pipe) {
            while (strcmp(argv[command_pos], "|")) {
                command[j] = (char * ) malloc(100 * sizeof(char));
                strcpy(command[j], argv[command_pos]);
                command_pos++;
                j++;
            }
        } else {
            while (argv[command_pos] != NULL) {
                command[j] = (char * ) malloc(100 * sizeof(char));
                strcpy(command[j], argv[command_pos]);
                command_pos++;
                j++;
            }
        }

        command[j] = NULL;

        for (int l = 0; l < j; l++) {
            if (!strcmp(command[l], ">")) {
                outputFile = command[l + 1];
                command[l] = NULL;
                out = 1;
            } else if (!strcmp(command[l], "<")) {
                inputFile = command[l + 1];
                command[l] = NULL; in = 1;
            } else {
                redirection_command[l] = command[l];
            }
        }

        pid = fork();

        if (pid == 0) {
            if ( in ) {
                if ((fd[0] = open(inputFile, O_RDONLY)) == -1) {
                    perror("[ERROR] OPEN: ");
                    exit(EXIT_FAILURE);
                }

                if (dup2(fd[0], STDIN_FILENO) == -1) {
                    perror("[ERROR] DUP2: ");
                    exit(EXIT_FAILURE);
                }

                if (close(fd[0]) == -1) {
                    perror("[ERROR] CLOSE: ");
                    exit(EXIT_FAILURE);
                }
            }

            if (i < count_pipe) {
                if (dup2(fd[pd_idx + 1], 1) < 0) {
                    perror("[ERROR] DUP2: ");
                    exit(EXIT_FAILURE);
                }
            }

            if (pd_idx != 0) {
                if (dup2(fd[pd_idx - 2], 0) < 0) {
                    perror("[ERROR] DUP2: ");
                    exit(EXIT_FAILURE);
                }
            }

            if (out && i == count_pipe) {
                int fd_out;

                if ((fd_out = open(outputFile, write_flags, mode)) == -1) {
                    perror("[ERROR] OPEN: ");
                    exit(EXIT_FAILURE);
                }

                if (dup2(fd_out, STDOUT_FILENO) == -1) {
                    perror("[ERROR] DUP2: ");
                    exit(EXIT_FAILURE);
                }

                if (close(fd_out) == -1) {
                    perror("[ERROR] CLOSE: ");
                    exit(EXIT_FAILURE);
                }
            }

            for (k = 0; k < 2 * count_pipe; k++) {
                if (close(fd[k]) == -1) {
                    perror("[ERROR] CLOSE: ");
                    exit(EXIT_FAILURE);
                }
            }

            if (execvp(redirection_command[0], redirection_command) < 0) {
                perror(redirection_command[0]);
                exit(EXIT_FAILURE);
            }
        } else if (pid < 0) {
            perror("[ERROR] FORK: ");
            exit(EXIT_FAILURE);
        }

        pd_idx += 2;
        free(command);
        free(redirection_command);
    }

    for (k = 0; k < 2 * count_pipe; k++) {
        if (close(fd[k]) == -1) {
            perror("[ERROR] CLOSE: ");
            exit(EXIT_FAILURE);
        }
    }

    for (k = 0; k < count_pipe + 1; k++)
        if (wait(NULL) == -1) {
            perror("[ERROR] WAIT: ");
            exit(EXIT_FAILURE);
        }
}

/*
 * list Function
 */
void ls(int argc, char ** argv) {
    char buf[256];
    if (argc == 1) {
        getcwd(buf, 256);
        printf("%s", buf);
        argv[1] = buf;
    }

    DIR * pdir;
    if ((pdir = opendir(argv[1])) < 0) {
        perror("[ ERROR ] OPEN DIR: ");
    }

    printf("\n");

    int i = 0;
    struct dirent * pde;

    while ((pde = readdir(pdir)) != NULL) {
        printf("%-20s", pde->d_name);
        if (++i % 3 == 0)
            printf("\n");
    }

    printf("\n");

    closedir(pdir);
}

void pwd() {
    char * buf = (char * ) malloc(sizeof(char) * BUFSIZE);

    if (getcwd(buf, BUFSIZE) == NULL) {
        perror("[ ERROR ] pwd");
        exit(EXIT_FAILURE);
    } else
        printf("%s \n", buf);

    free(buf);
}

void cd(int arg_cnt, char ** argv) {
    if (arg_cnt == 1) {
        chdir("HOME");
    } else {
        if (chdir(argv[1]) == -1) {
            printf("%s : No Search File or Directory\n", argv[1]);
        }
    }
}

void cat(int argc, char ** argv) {
    if (argc < 2) {
        fprintf(stderr, "Please Input Files : \n");
        //exit(1);
        return;
    }

    FILE * file;
    for (int i = 1; i < argc; i++) {
        file = fopen(argv[i], "r");
        if (file == NULL) {
            printf("Cat: %s: No Such File or Directory\n", argv[i]);
            continue;
        }

        int buf;
        while ((buf = fgetc(file)) != EOF) {
            putchar(buf);
        }

        printf("\n");
        if (fclose(file) != 0) {
            perror("[ ERROR ] Closing File");
        }
    }
}

/*
 * 백그라운드에서 실행하도록 하는 함수
 */
void launch(int arg_cnt, char ** argv) {
    pid = 0;
    int i = 0;
    int isBackground = 0;

    if (arg_cnt != 0 && !strcmp(argv[arg_cnt - 1], "&")) {
        argv[arg_cnt - 1] = NULL;
        isBackground = 1;
    }

    pid = fork();

    if (pid == 0) {
        if (isBackground) {
            printf("\n[ CREATE ] BACKGROUND PROCESS(%ld)\n", (long) getpid());
        }

        if (execvp(argv[0], argv) < 0) {
            perror("[ ERROR ] CREATE BACKGROUND");
        }
    } else {
        if (isBackground == 0) {
            wait(pid);
        }
    }
}

void mkdir_command(int argc, char **argv){
    if(argc != 2){
        fprintf(stderr, "Usage: mkdir <directory_name>\n");
        return;
    }

    if(mkdir(argv[1], 0777) == -1){
        perror("[ ERROR ] mkdir");
    } else {
        printf("Directory created successfully.\n");
    }
}

/*
 * rmdir Command Function
 */
void rmdir_command(int argc, char **argv){
    if(argc != 2){
        fprintf(stderr, "Usage: rmdir <directory_name>\n");
        return;
    }

    if(rmdir(argv[1]) == -1){
        perror("[ ERROR ] rmdir");
    } else {
        printf("Directory removed successfully.\n");
    }
}

/*
 * ln Command Function
 */
void ln_command(int argc, char **argv){
    if(argc != 3){
        fprintf(stderr, "Usage: ln <source> <destination>\n");
        return;
    }

    if(link(argv[1], argv[2]) == -1){
        perror("[ ERROR ] ln");
    } else {
        printf("Link created successfully.\n");
    }
}


/*
 * Return the number of Arguments
 */
int getargs(char * cmd, char ** argv) {
    int argc = 0;
    while ( * cmd) {
        if ( * cmd == ' ' || * cmd == '\t')
            *
            cmd++ = '\0';
        else {
            argv[argc++] = cmd++;
            while ( * cmd != '\0' && * cmd != ' ' && * cmd != '\t')
                cmd++;
        }
    }
    argv[argc] = NULL;
    return argc;
}
