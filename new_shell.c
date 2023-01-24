#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

#define INPUT_MAX_LENGTH 500
#define MAX_COMMAND_COUNT 50
#define NO_VALUE -1
#define MAX_BG_PROC_COUNT 10
#define NULL_POINTER 0x0

struct Comms {
    int pid;
    int stdin_fd;
    int stdout_fd;
    int stderr_fd;
};

struct Comms *comms_init(void)
{
    struct Comms *cms = malloc(sizeof(struct Comms));
    cms->pid = NO_VALUE;
    cms->stdin_fd = NO_VALUE;
    cms->stdout_fd = NO_VALUE;
    cms->stderr_fd = NO_VALUE;

    return cms;
}

struct Command {
    char **arguments;
    char *terminator;
    int length;
};

struct Command *cmd_init(void)
{
    struct Command *cmd = malloc(sizeof(struct Command));
    return cmd;
}

struct BGProc {
    int pid;
    int stdin_fd;
};

struct BGProc bg_processes[MAX_BG_PROC_COUNT];

struct Spec {
    char *pattern;
    void (*function)();
};
    
void t(void) {
}

struct Spec special_characters[] = {
    {"||", t},
    {"|", t},
    {"&&", t},
    {"&", t},
    {"<", t},
    {">", t}
};

struct Spec command_terminator = (struct Spec){ NULL_POINTER, t };
    
void safe_fd_close(int *file_descriptor)
{
    close(*file_descriptor);
    *file_descriptor = NO_VALUE;
}

struct RawInput {
    int buffer_length;
    int fill_length;
    char *buffer;
};

struct RawInput* input_init(void)
{
    struct RawInput *raw_input = malloc(sizeof(struct RawInput));
    raw_input->buffer_length = INPUT_MAX_LENGTH;
    raw_input->buffer = malloc(raw_input->buffer_length * sizeof(char));
    raw_input->fill_length = 0;

    return raw_input;
}

struct ParsedInput {
    int buffer_length;
    int fill_length;
    char **word_ptrs;

};

struct ParsedInput* parsed_input_init(void)
{
    struct ParsedInput *parsed_input = malloc(sizeof(struct ParsedInput));
    parsed_input->buffer_length = MAX_COMMAND_COUNT;
    parsed_input->word_ptrs = malloc(parsed_input->buffer_length * sizeof(char*));
    parsed_input->fill_length = 0;

    return parsed_input;
}

void get_raw_input(struct RawInput *raw_input)
{
    int i;
    char current_char;
    int buffer_length = raw_input->buffer_length;

    for(i = 0; i < buffer_length; i++) {
        current_char = getchar();
        raw_input->buffer[i] = current_char;

        if ( current_char == '\n') {
            break;
        }
    }

    raw_input->buffer[++i] = '\0';
    raw_input->fill_length = i;
}

void save_word_pointer(char *word_pointer, struct ParsedInput *parsed_input)
{
    int word_index = parsed_input->fill_length;
    parsed_input->word_ptrs[word_index] = word_pointer;
    ++*&parsed_input->fill_length; //cheesy
}

void skip_subsequent_nonspace_printables(int *letter_index, struct RawInput *raw_input)
{
    char next_char;

    while (1) {
        next_char = raw_input->buffer[*letter_index + 1];

        if (!isspace(next_char) && isprint(next_char))
            ++*letter_index;
        else
            break;
    }
}

void parse_input(struct RawInput *raw_input, struct ParsedInput *parsed_input)
{
    int input_fill_length = raw_input->fill_length;
    char *current_char;

    for (int letter_index = 0; letter_index < input_fill_length + 1; letter_index++) {
        current_char = raw_input->buffer + letter_index;

        if (isspace(*current_char)){
            *current_char = '\0';
        } else if (isprint(*current_char)) {
            save_word_pointer(current_char, parsed_input);
            skip_subsequent_nonspace_printables(&letter_index, raw_input);
        } else {
            save_word_pointer(NULL_POINTER, parsed_input);
            break;
        } 
    }
}

int execute_command(char **args, int stdin_fd, int stdout_fd, int stderr_fd)
{
    int child_pid = fork();

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
        exit(1);
    }
    return child_pid;
}

     
struct Comms mybg(char **args, struct Comms input, struct BGProc bg_processes[])
{
    int pipefd[2];
    int child_pid = execute_command(args, input.stdin_fd, NO_VALUE, NO_VALUE);
    struct BGProc proc_info = {child_pid, input.stdin_fd};

    pipe(pipefd);

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

int execute_and_return_exit_code(char **args, struct Comms *input)
{
    int child_pid = execute_command(args, input->stdin_fd, NO_VALUE, NO_VALUE);
    int wstatus, child_process_exit_status = 1;
    
    waitpid(child_pid, &wstatus, 0); 
    
    if (WIFEXITED(wstatus)) {
        child_process_exit_status = WEXITSTATUS(wstatus);
    }

    return child_process_exit_status;
}


void mypipe(char **args, struct Comms *cms)
{
    int pipefd[2];
    pipe(pipefd);

    int child_pid = execute_command(args, cms->stdin_fd, pipefd[1], NO_VALUE);
    safe_fd_close(&pipefd[1]);
    struct Comms result = {child_pid, pipefd[0], NO_VALUE, NO_VALUE};
    cms->pid = child_pid;
    cms->stdin_fd = pipefd[0];
    cms->stdout_fd = NO_VALUE;
    cms->stderr_fd = NO_VALUE;
}

void fill_length_resets(struct RawInput *raw_input, struct ParsedInput *parsed_input)
{
    parsed_input->fill_length = 0;
    raw_input->fill_length =0;
}

struct Spec* match_special(char *string)
{
    int specials_length = sizeof(special_characters) / sizeof(special_characters[0]);
    struct Spec *spec_pointer = NULL_POINTER;

    for(int i = 0; i < specials_length; i++) {
        if(strcmp(string, special_characters[i].pattern)){
            spec_pointer = &special_characters[i];
            break;
        }
    }

    return spec_pointer;
}

int terminate_command(char *special_symbol, struct Command *cmd)/// cia viduriai yra bullshit, tiesiog reikia uzsettint null pointeri ant galiako
{
    if(cmd->length) {
        cmd->terminator = special_symbol; 
    } else {
        fprintf(stderr, "cia idet prasminga komanda kai randam specialu simboli be pries tai enancios komandos");
        return 1;
    }
    return 0;
}

int run_commands(struct ParsedInput *parsed_input, struct Command *cmd, struct Comms *cms)
{
    char *current_word;
    struct Spec *current_special;
    int run_condition, error_condition;

    cmd->length = 0;

    for(int i=0;; i++) {
        current_word = parsed_input->word_ptrs[i];
        current_special = match_special(current_word);

        run_condition = current_special && cmd->length;
        error_condition = current_special && !cmd->length;

        if (run_condition) {
            terminate_command(cmd);
            run_command(current_special, cmd);
            cmd->length = 0;
        } else if (error_condition) {
            fprintf(stderr, "lia lia lia");
            return 1;
        } else {
            cmd->arguments[cmd->length++] = current_word;
        }

        if(current_special) {

            terminate_command(current_special, cmd);
            run_command(cmd);

            cmd->length = 0;
        } else {
            cmd->arguments[cmd->length++] = current_word;
        }
    }
}

int main()
{
    struct RawInput *raw_input = input_init();
    struct ParsedInput *parsed_input = parsed_input_init();
    struct Comms *cms = comms_init();
    struct Command *cmd = cmd_init();

    while(1) {
        printf("$ ");
        get_raw_input(raw_input);
        parse_input(raw_input, parsed_input);
        for(int i=0;i<parsed_input->fill_length; i++){
            printf("'%s' ",parsed_input->word_ptrs[i]);
        }
        execute_and_return_exit_code(parsed_input->word_ptrs, cms);
        fill_length_resets(raw_input, parsed_input);
    }
}
