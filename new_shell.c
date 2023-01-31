#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>
#include<sys/stat.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>

#define INPUT_MAX_LENGTH 500
#define PADDED_INPUT_MAX_LENGTH 1000
#define MAX_COMMAND_COUNT 50
#define NO_VALUE -1
#define MAX_BG_PROC_COUNT 10
#define NULL_POINTER 0x0
#define TERMINATING 1
#define NON_TERMINATING 0
#define NO_REDIRECTION 1
#define REDIRECTION_SUCCESSFUL 0
#define REDIRECTION_FILE_MISSING -1
#define REDIRECTION_TOO_MANY_FILES_OPEN -2
#define REDIRECT_OP_AR_LEN 4
#define MAX_OPEN_FILES 10

int open_files[MAX_OPEN_FILES]; // should be probably linked list of structs or smth. Also should be associated with background processes and contain owning process pid

struct Comms {
    int pid;
    int exit_code;
    int short_circuit;
    int stdin_fd;
    int stdout_fd;
    int stderr_fd;
};

void comms_reset(struct Comms *cms)
{
    cms->pid = NO_VALUE;
    cms->exit_code = NO_VALUE;
    cms->short_circuit = NO_VALUE;
    cms->stdin_fd = NO_VALUE;
    cms->stdout_fd = NO_VALUE;
    cms->stderr_fd = NO_VALUE;
}

struct Comms *comms_init(void)
{
    struct Comms *cms = malloc(sizeof(struct Comms));
    cms->pid = NO_VALUE;
    cms->exit_code = NO_VALUE;
    cms->short_circuit = NO_VALUE;
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

void command_reset(struct Command *cmd)
{
    cmd->length = 0;
    cmd->terminator = NULL_POINTER;
}

struct Command *cmd_init(void)
{
    struct Command *cmd = malloc(sizeof(struct Command));
    cmd->arguments = malloc(sizeof(char*) * MAX_COMMAND_COUNT);
    return cmd;
}

struct BGProc {
    int pid;
    int stdin_fd;
};

struct BGProcArray {
    struct BGProc* processes[MAX_BG_PROC_COUNT];
    int length;
};

struct BGProcArray background_processes;

struct Spec {
    char *pattern;
    int terminability;
    int (*function)(struct Comms*, struct Command*);
};

int execute_synchronously(struct Comms*, struct Command*);
int mypipe(struct Comms*, struct Command*);
int mybg(struct Comms*, struct Command*);
int logical_and(struct Comms*, struct Command*);
int logical_or(struct Comms*, struct Command*);

struct Spec special_characters[] = {
    {"||", NON_TERMINATING, logical_or},
    {"|", NON_TERMINATING, mypipe},
    {"&&", NON_TERMINATING, logical_and},
    {"&", TERMINATING, mybg},
    {";", TERMINATING, execute_synchronously}
};

struct Spec normal_terminator = (struct Spec){NULL_POINTER, TERMINATING, execute_synchronously};

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

int create_or_rewrite_file(char *path, int *file_descriptor){
    int file_index, return_value = REDIRECTION_SUCCESSFUL;

    for (file_index = 0;; file_index++) {
        if (file_index == MAX_OPEN_FILES) {
            return_value = REDIRECTION_TOO_MANY_FILES_OPEN;
            goto f_return;
        } else if (open_files[file_index] == 0) {
            break;
        }
    }

    if (!access(path, F_OK)) {
        remove(path);
    }

    *file_descriptor = open(path, O_CREAT | O_WRONLY, 0644);
    open_files[file_index] = *file_descriptor;

    f_return:
    return return_value;
}

struct RedirectionOperator {
    char *pattern;
    int (*function)(char *, struct Comms*);
};

int stdout_redirect(char *path, struct Comms *cms)
{
    int fd, return_value;

    return_value = create_or_rewrite_file(path, &fd);
    cms->stdout_fd = fd;

    return return_value;
}

int stderr_redirect(char *path, struct Comms *cms)
{
    int fd, return_value;

    return_value = create_or_rewrite_file(path, &fd);
    cms->stderr_fd = fd;

    return return_value;
}

int combined_redirect(char *path, struct Comms *cms)
{
    int fd, return_value;

    return_value = create_or_rewrite_file(path, &fd);
    cms->stdout_fd = fd;
    cms->stderr_fd = fd;

    return return_value;
}

int clear_previous_redirects(struct Comms *cms)
{
    int com_channel_count = 3;
    int * com_vals[] = {&cms->stdin_fd, &cms->stdout_fd, &cms->stderr_fd};

    for (int i = 0; i < com_channel_count; i++) {
        if (*com_vals[i] > 2) {
            *com_vals[i] = NO_VALUE;
        }
    }

    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (open_files[i] != 0) {
            close(open_files[i]);
            open_files[i] = 0;
        }
    }
    return 0;
}

struct RedirectionOperator redirect_operators[REDIRECT_OP_AR_LEN] = {
    //{"<", t},
    {">", stdout_redirect},
    {"1>", stdout_redirect},
    {"2>", stderr_redirect},
    {"&>", combined_redirect}
};

int trigger_redirect(char *path, struct RedirectionOperator *redirect_op, struct Comms *cms)
{
    int file_descriptor, file_index, return_value;

    if (path == NULL_POINTER) {
        return REDIRECTION_FILE_MISSING;
    }

    return_value = redirect_op->function(path, cms);
    return return_value;
}

int output_redirect(struct ParsedInput *parsed_input, int command_index, struct Comms *cms)
{
    char *path, *pattern;
    int file_descriptor, pattern_length, diff, return_val;
    struct RedirectionOperator *redirect_op;

    for (int i = 0; i < REDIRECT_OP_AR_LEN; i++) {
        redirect_op = &redirect_operators[i];
        pattern = redirect_op->pattern;
        pattern_length = strlen(pattern);

        diff = strncmp(parsed_input->word_ptrs[command_index], pattern, pattern_length);

        if (!diff) {
            path = parsed_input->word_ptrs[command_index + 1];

            return_val = trigger_redirect(path, redirect_op, cms);

            return return_val;
        }
    }

    return NO_REDIRECTION;
}

void get_raw_input(struct RawInput *raw_input)
{
    int i;
    char current_char;

    for(i = 0; i < INPUT_MAX_LENGTH; i++) {
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
    ++parsed_input->fill_length;
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

union SpecCharWrapper{
    struct Spec* controll_special;
    struct RedirectionOperator* redirection_special;
};

int compare_special(char struct_type, union SpecCharWrapper wrap, char *string, int *number_of_check_skips)
{
    char *current_special;
    int pattern_length, diff;
    
    current_special = struct_type == 'c' ? wrap.controll_special->pattern : wrap.redirection_special->pattern;
    pattern_length = strlen(current_special);

    diff = strncmp(string, current_special, pattern_length);
    if (!diff) {
        *number_of_check_skips = pattern_length;
        return 1;
    }

    return 0;
}

int isspecial(char *string, int *number_of_check_skips)
{
    int controll_specials_length, redirect_specials_length, return_val;
    union SpecCharWrapper wrapper;

    *number_of_check_skips = 0;

    redirect_specials_length = sizeof(redirect_operators) / sizeof(redirect_operators[0]);
    for(int i = 0; i < redirect_specials_length; i++){
        wrapper.redirection_special = redirect_operators + i;
        return_val = compare_special('r', wrapper, string, number_of_check_skips);
        if (return_val)
            goto f_return;
    }

    controll_specials_length = sizeof(special_characters) / sizeof(special_characters[0]);
    for (int i = 0; i < controll_specials_length; i++) {
        wrapper.controll_special = special_characters + i;
        return_val = compare_special('c', wrapper, string, number_of_check_skips);
        if (return_val)
            goto f_return;
    }
    
    f_return:
    return return_val;
}

void pad_input(struct RawInput *raw_input)
{
    char current_char;
    int special_length;
    int source_index = 0, destination_index = 0, source_fill_length = raw_input->fill_length;
    char *source = raw_input->buffer, *destination = malloc(sizeof(char) * PADDED_INPUT_MAX_LENGTH);

    for (; source_index <= source_fill_length; source_index++) {
        current_char = source[source_index];

        if (isspecial(source + source_index, &special_length)) {
            if (!isspace(destination[destination_index - 1])) {
                destination[destination_index++] = ' ';
            }

            while (special_length--) {
            destination[destination_index++] = source[source_index++];
            }
            source_index--;

            if (!isspace(destination[destination_index])) {
                destination[destination_index++] = ' ';
            }
        } else {
            destination[destination_index++] = current_char;
        }


        if (current_char == '\0') {
            destination[destination_index++];
            break;
        } else if (isspace(current_char)) {
            while (isspace(source[source_index+1])) {
                source_index++;
            }
        }
    }

    free(raw_input->buffer);
    raw_input->buffer = destination;
    raw_input->fill_length = destination_index;
}

void parse_input(struct RawInput *raw_input, struct ParsedInput *parsed_input)
{
    int input_fill_length;
    char *current_char;

    pad_input(raw_input);
    input_fill_length = raw_input->fill_length;

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

int execute_command(struct Command *cmd, struct Comms *cms) //gal reiketu tuos skaicius pakeis kanalu vardais (manau stdin yra prialiasintas 0)
{
    int exit_code = 0, child_pid = fork();

    if(!child_pid) {
        if (cms->stdin_fd != NO_VALUE) {
            dup2(cms->stdin_fd, 0);
            }
        if (cms->stdout_fd != NO_VALUE) {
            dup2(cms->stdout_fd, 1);
            }
        if (cms->stderr_fd != NO_VALUE) {
            dup2(cms->stderr_fd, 2);
            }

        int exec_return = execvp(cmd->arguments[0], cmd->arguments);
        fprintf(stderr, "command: '%s' not found\n", cmd->arguments[0]);
        exit(1);
    }

    cms->pid = child_pid;
    return exit_code;
}

int mybg(struct Comms *cms, struct Command *cmd)
{
    //basically just replacing stdin with read end of a pipe and then putting write end of the pipe to and array along with pid of the process
    if (background_processes.length == MAX_COMMAND_COUNT){
        fprintf(stderr, "MAXIMUM BACKGROUND PROCESSES REACHED\n");
        cms->short_circuit = 1;
        return 1;
    }

    int pipefd[2];
    pipe(pipefd);
    cms->stdin_fd = pipefd[0];

    execute_command(cmd, cms);
    safe_fd_close(&pipefd[0]);

    struct BGProc *proc_info = malloc(sizeof(struct BGProc));
    proc_info->pid = cms->pid;
    proc_info->stdin_fd = pipefd[1];

    background_processes.processes[background_processes.length++] = proc_info;

    return 0;
}

int logical_and(struct Comms *cms, struct Command *cmd)
{
    execute_synchronously(cms, cmd);
    if (cms->exit_code)
        cms->short_circuit = 1;

    return 0;
}

int logical_or(struct Comms *cms, struct Command *cmd)
{
    execute_synchronously(cms, cmd);
    if (!cms->exit_code)
        cms->short_circuit = 1;

    return 0;
}

int execute_synchronously(struct Comms *cms, struct Command *cmd)
{
    int wstatus, child_process_exit_status = 1;
    execute_command(cmd, cms);
    
    waitpid(cms->pid, &wstatus, 0); 
    
    if (WIFEXITED(wstatus)) {
        child_process_exit_status = WEXITSTATUS(wstatus);
    }

    cms->exit_code = child_process_exit_status;

    return 0;
}

int mypipe(struct Comms *cms, struct Command *cmd)
{
    int pipefd[2];
    pipe(pipefd);

    if (cms->stdout_fd == NO_VALUE) {
        cms->stdout_fd = pipefd[1];
    }

    execute_command(cmd, cms);

    safe_fd_close(&pipefd[1]);

    cms->stdin_fd = pipefd[0];
    cms->stdout_fd = NO_VALUE;

    return 0;
}

void buffer_fill_length_resets(struct RawInput *raw_input, struct ParsedInput *parsed_input)
{
    parsed_input->fill_length = 0;
    raw_input->fill_length = 0;
}

struct Spec* match_special(char *string) //retruns either pointer to Spec or null pointer
{
    int specials_length = sizeof(special_characters) / sizeof(special_characters[0]);
    struct Spec *spec_pointer = NULL_POINTER;

    if(!string) {
        spec_pointer = &normal_terminator;
        goto finish;
    }

    for(int i = 0; i < specials_length; i++) {
        if(!strcmp(string, special_characters[i].pattern)){
            spec_pointer = &special_characters[i];
            break;
        }
    }

    finish:
    return spec_pointer;
}

void run_command(struct Comms *cms, struct Command *cmd, struct Spec *current_special)
{
    cmd->arguments[cmd->length++] = NULL_POINTER;
    current_special->function(cms, cmd);
    cmd->length = 0;
}

int run_commands(struct ParsedInput *parsed_input, struct Command *cmd, struct Comms *cms)
{
    char *current_word;
    struct Spec *current_special, *next_is_special;
    int run_condition, non_preceeding_error_condition, non_succeeding_error_condition, redirect_status;

    comms_reset(cms);
    command_reset(cmd);
    //close_open_redirect_files();

    for(int i = 0; i < parsed_input->fill_length; i++) {
        current_word = parsed_input->word_ptrs[i];
        current_special = match_special(current_word);
        next_is_special = match_special(parsed_input->word_ptrs[i+1]);

        redirect_status = current_special ? NO_REDIRECTION : output_redirect(parsed_input, i, cms);

        run_condition = current_special && cmd->length;
        non_preceeding_error_condition = current_special && !cmd->length && current_special != &normal_terminator;
        non_succeeding_error_condition = current_special && !current_special->terminability && next_is_special;

        if (non_preceeding_error_condition) {
            fprintf(stderr, "error: symbol '%s' not preceded by any command\n", current_special->pattern);
            return 1;
        } else if (non_succeeding_error_condition) {
            fprintf(stderr, "error: symbol '%s' not succeeded by any other command\n", current_special->pattern);
            return 1;
        } else if (redirect_status == REDIRECTION_SUCCESSFUL) {
            i++;
            continue;
        } else if (redirect_status == REDIRECTION_FILE_MISSING){
            fprintf(stderr, "no path provided for redirection operator\n");
            return 1;
        } else if (redirect_status == REDIRECTION_TOO_MANY_FILES_OPEN) {
            fprintf(stderr, "too many files open currently");
            return 1;
        } else if (run_condition) {
            run_command(cms, cmd, current_special);
            clear_previous_redirects(cms);

            if (cms->short_circuit != NO_VALUE)
                break;
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
        //printf("RECEIVED INPUT: "); for (int i = 0; i < parsed_input->fill_length; i++) { printf("%s ", parsed_input->word_ptrs[i]);}; printf("\n");
        run_commands(parsed_input, cmd, cms);
        buffer_fill_length_resets(raw_input, parsed_input);
    }
}
