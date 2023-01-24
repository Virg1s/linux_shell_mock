#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

#define MAX_INPUT_LENGTH 5000
#define MAX_WORD_LENGTH 500
#define MAX_COMMAND_COUNT 50
#define MAX_BG_PROC_COUNT 10
#define SYNC 0
#define ASYNC 1
#define NO_VALUE -1

struct Comms {
    int pid;
    int stdin_fd;
    int stdout_fd;
    int stderr_fd;
};

struct BGProc {
    int pid;
    int stdin_fd;
};

struct BGProc bg_processes[MAX_BG_PROC_COUNT];

struct spec {
    char *pattern;
    void (*function)();
};

void t() {
}

struct spec special_characters[] = {
    {"||", t},
    {"|", t},
    {"&&", t},
    {"&", t},
    {"<", t},
    {">", t}
};

char *spec_char_indicators = "&|";

int spec_char_arr_len = sizeof(special_characters) / sizeof(special_characters[0]);

void safe_fd_close(int *file_descriptor) {
    close(*file_descriptor);
    *file_descriptor = NO_VALUE; 
}

void safe_pointer_release(void **pointer_address) {
    if(*pointer_address == NULL)
        return;

    free(*pointer_address);
    *pointer_address = NULL;
}

void safe_pipe(int pipe_fds[]) {
    if(pipe(pipe_fds)) {
    fprintf(stderr, "ERROR WHILE BUILDING PIPE\n");
    exit(1);
    }
}

int safe_fork() {
    int child_pid = fork();
    if (child_pid == -1) {
        fprintf(stderr, "ERROR WHILE FORKING\n");
        exit(1);
    }
    return child_pid;
}

void report_exec_error(char **args) {
        fprintf(stderr, "Command ");

        for (int i=0; args[i] != NULL; i++) {
            if (i > 0) {
                fprintf(stderr, " ");
            }
            fprintf(stderr, "%s", args[i]);
        }
        fprintf(stderr, " failed: %s\n", strerror(errno));
}

int execute_command(char **args, int stdin_fd, int stdout_fd, int stderr_fd) {
    int child_pid = safe_fork();

    if(!child_pid) {
        if (stdin_fd != NO_VALUE) {
            dup2(stdin_fd, 0);
            }
        if (stdout_fd != NO_VALUE) {
            dup2(stdout_fd, 1);
            }
        if (stderr_fd != NO_VALUE) {
            dup2(stderr_fd, 2);
            }

        int exec_return = execvp(args[0], args);
        report_exec_error(args);
        exit(1);
    }
    return child_pid;
}

struct Comms mypipe(char **args, struct Comms input) {
    int pipefd[2];
    safe_pipe(pipefd);

    int child_pid = execute_command(args, input.stdin_fd, pipefd[1], -1);
    safe_fd_close(&pipefd[1]);
    struct Comms result = {child_pid, pipefd[0], NO_VALUE, NO_VALUE};
    return result;
}

struct Comms mybg(char **args, struct Comms input, struct BGProc bg_processes[]) {
    int pipefd[2];
    int child_pid = execute_command(args, input.stdin_fd, NO_VALUE, NO_VALUE);
    struct BGProc proc_info = {child_pid, input.stdin_fd};

    safe_pipe(pipefd);

    for(int i=0;; i++) {
        if (i >= MAX_BG_PROC_COUNT){
            fprintf(stderr, "MAXIMUM BACKGROUND PROCESSES REACHED");
            exit(1);
        }
            
        if(bg_processes[i].pid == 0){
            bg_processes[i] = proc_info; 
            break;
        }
    }

    struct Comms result = {NO_VALUE, NO_VALUE, NO_VALUE, NO_VALUE};
    return result;
}

int execute_and_return_exit_code(char **args, struct Comms input) {
    int child_pid = execute_command(args, input.stdin_fd, NO_VALUE, NO_VALUE);
    int wstatus, child_process_exit_status = 1;

    waitpid(child_pid, &wstatus, 0);

    if (WIFEXITED(wstatus)) {
        child_process_exit_status = WEXITSTATUS(wstatus);
    }

    return child_process_exit_status;
}

void *safe_malloc(int size) {
    void *pointer = malloc(size);

    if (pointer == NULL) {
        fprintf(stderr, "NULL RETURNED BY MALLOC\n");
        exit(1);
    }   

    return pointer;
}

int save_word_if_word_length_not_null(char **command, int *word_position, char *word_buffer, int *letter_position) {
    if (!*letter_position)
        return 0;

    char *current_word = safe_malloc(sizeof(char) * *letter_position);
    word_buffer[*letter_position] = '\0';
    memcpy(current_word, word_buffer, *letter_position);
    command[(*word_position)++] = current_word;

    *letter_position = 0; 

    return 1;
}

int isspecial(char current_char) {
    for (int i = 0; spec_char_indicators[i] != '\0'; i++) {
        if(spec_char_indicators[i] == current_char)
            return 1;
    }
    return 0;
}

void get_base_input(char base_input[]) {
    char current_char;

    for (int i = 0;; i++) {
        current_char = getchar();
        base_input[i] = current_char;
        if (current_char == '\n') {
            base_input[++i] = '\0';
            break;
        }
    }
}

void save_special(char base_input[], int *iterator, char ***user_input, int user_input_size[], int *command_position_pointer) {
    char *pattern;
    for(int i=0; i <= spec_char_arr_len; i++){
        pattern = special_characters[i].pattern;
        for(int a=0;; a++){
            if(pattern[a] == '\0') {
                char *special_char_copy = safe_malloc(sizeof(char) * (a + 2));
                memcpy(special_char_copy, pattern, a + 1);
                int special_container_size = 2;
                char **special_command_container = safe_malloc(sizeof(char *) * special_container_size);
                special_command_container[0] = special_char_copy;
                special_command_container[1] = NULL;

                user_input_size[*command_position_pointer] = special_container_size;
                user_input[(*command_position_pointer)++] = special_command_container;

                *iterator = *iterator + a - 1;
                return;
            }
            else if (pattern[a] != base_input[*iterator + a])
                break;
        }
    }
}

void save_command(char **command_buffer, int *word_position_pointer, char ***user_input, int user_input_size[], int *command_position_pointer) {
    command_buffer[(*word_position_pointer)++] = NULL;

    char **command = safe_malloc(sizeof(char *) * *word_position_pointer);
    memcpy(command, command_buffer, *word_position_pointer * sizeof(char*));
    user_input_size[*command_position_pointer] = *word_position_pointer;
    user_input[(*command_position_pointer)++] = command;

    *word_position_pointer = 0;
}

struct Commands {
    char ***commands;
    int *size_info;
};

struct Commands get_user_input() {
    int *usr_input_size = malloc(sizeof(int) * MAX_COMMAND_COUNT);
    char ***usr_input = safe_malloc(sizeof(char *) * MAX_COMMAND_COUNT);
    char **command_buffer = safe_malloc(sizeof(char *) * MAX_COMMAND_COUNT);
    char *word_buffer = safe_malloc(sizeof(char) * MAX_INPUT_LENGTH);
    int letter_position = 0, word_position = 0, command_position = 0;
    char current_char;

    char base_input[MAX_INPUT_LENGTH];
    get_base_input(base_input);

    for (int i = 0; (current_char = base_input[i]) != '\0'; i++) {
        if (isspace(current_char)) {
            save_word_if_word_length_not_null(command_buffer, &word_position, word_buffer, &letter_position);

            if (current_char == '\n') {
                save_command(command_buffer, &word_position, usr_input, usr_input_size, &command_position);
                save_command(command_buffer, &word_position, usr_input, usr_input_size, &command_position);
                break;
            }
        } else if (isspecial(current_char)) {
            save_word_if_word_length_not_null(command_buffer, &word_position, word_buffer, &letter_position);
            save_command(command_buffer, &word_position, usr_input, usr_input_size, &command_position);
            save_special(base_input, &i, usr_input, usr_input_size, &command_position);
        } 
        else {
            word_buffer[letter_position++] = current_char;
        }
    }
    save_command(command_buffer, &word_position, usr_input, usr_input_size, &command_position);

    struct Commands return_val = {usr_input, usr_input_size};
    return return_val;
}

struct ExecutionResult {
    int pid;
    int stdin_fd;
    int stdout_fd;
    int stderr_fd;
}; 

void release_commands_struct_resources(struct Commands cmds) {
    for(int sz ,i=0; (sz = cmds.size_info[i]) != 0; i++) {
        for(int a=0; a<sz; a++) {
            safe_pointer_release((void *) &cmds.commands[i][a]);
        }
        safe_pointer_release((void *) &cmds.commands[i]);
    }
    safe_pointer_release((void *) &cmds.commands);
    safe_pointer_release((void *) &cmds.size_info);
}



void prompt() {
    struct Comms cms = {NO_VALUE, NO_VALUE, NO_VALUE, NO_VALUE};
    struct Comms blank_cms = {NO_VALUE, NO_VALUE, NO_VALUE, NO_VALUE};
    struct Commands cmds;
    char *pipe_sym = "|", *ampersand_sym = "&", *double_pipe = "||", *double_ampersand ="&&";
    char **command, *special;
    int child_exit_status = 0;
    while (1) {
        printf("$");
        cmds = get_user_input();

        for(int i = 0; ; i++) {
            command = cmds.commands[i];
            special = cmds.commands[++i][0];

            if (special == NULL) {
                cms = mypipe(command, cms);
                break;
            } else if (!strcmp(pipe_sym, special)) {
                cms = mypipe(command, cms);
            } else if (!strcmp(ampersand_sym, special)) {
                cms = mybg(command, cms, bg_processes);
            } else if (!strcmp(double_ampersand, special)){
                child_exit_status = execute_and_return_exit_code(command, cms);
                cms = blank_cms;

                if (child_exit_status) {
                    break;
                } else {
                    continue;
                }
            } else if (!strcmp(double_pipe, special)) {
                child_exit_status = execute_and_return_exit_code(command, cms);
                cms = blank_cms;

                if (child_exit_status) {
                    continue;
                } else {
                    break;
                }
            } else {
                fprintf(stderr, "NOT IMPLEMENTED YET\n");
                exit(1);
            }
        }
     
        release_commands_struct_resources(cmds);

        int ppid = getpid(); int bufflen = 5000; char buff[bufflen]; int rl = read(cms.stdin_fd, buff, bufflen); buff[rl] = '\0';
        printf("PROGRAM END, pid: %d; buffer:\n%s\n", ppid, buff);
    }
}

int main() {
    prompt();
}

