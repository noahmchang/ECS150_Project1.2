#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

#define CMDLINE_MAX 512
#define ARG_MAX 16
#define MAX_CMDS 4

enum RedirectType {
    NO_REDIRECTION,
    OUTPUT_REDIRECTION,
    INPUT_REDIRECTION,
    PIPING
};

struct Command {
    char raw_cmd[CMDLINE_MAX];
    char extracted_filename[CMDLINE_MAX];
    enum RedirectType redirect_type;
    char*** commands;
    char** args;
    bool background;
};

char *preprocess(char *cmdline) { //adding whitespace before we parse into tokens
    static char new_cmdline[CMDLINE_MAX * 2];
    int j = 0;
    for (int i = 0; cmdline[i] != '\0'; i++) {
        if (cmdline[i] == '>' || cmdline[i] == '<' || cmdline[i] == '|' || cmdline[i] == '&') {
            new_cmdline[j++] = ' ';
            new_cmdline[j++] = cmdline[i];
            new_cmdline[j++] = ' ';
        } else {
            new_cmdline[j++] = cmdline[i];
        }
    }
    new_cmdline[j] = '\0';
    return new_cmdline;
}

bool parse_command(struct Command* cmd_object) {
    static char* parsed_cmds[MAX_CMDS][ARG_MAX];
    int cmd_count = 0;
    int arg_index = 0;

    char *token;
    char *saveptr;
    char temp[CMDLINE_MAX];
    char *new_cmd = preprocess(cmd_object->raw_cmd);
    strncpy(temp, new_cmd, CMDLINE_MAX);
    bool seen_output_redirection = false;
    bool last_symbol_is_pipe = false;

    token = strtok_r(temp, " ", &saveptr);
    while (token != NULL) {
        if (strcmp(token, "&") == 0) {
            token = strtok_r(NULL, " ", &saveptr);
            if (arg_index == 0) {
                fprintf(stderr, "Error: missing command\n");
                return false;
            }
            if (token != NULL) {
                fprintf(stderr, "Error: mislocated background sign\n");
                return false;
            }
            cmd_object->background = true;
            break;
        } else if (strcmp(token, "|") == 0) { //PIPING
            last_symbol_is_pipe = true;
            if (arg_index == 0) {
                fprintf(stderr, "Error: missing command\n");
                return false;
            }
            if (seen_output_redirection) {
                fprintf(stderr, "Error: mislocated output redirection\n");
                return false;
            }
            parsed_cmds[cmd_count][arg_index] = NULL;
            cmd_count++; //increase cmd counter
            if (cmd_count >= MAX_CMDS) {
                fprintf(stderr, "Error: too many commands\n");
                return false;
            }
            arg_index = 0; //restart arg counter
            cmd_object->redirect_type = PIPING;
        } else if (strcmp(token, ">") == 0) { //OUTPUT REDIRECTION
            if (arg_index == 0) {
                fprintf(stderr, "Error: missing command\n");
                return false;
            }
            seen_output_redirection = true;
            token = strtok_r(NULL, " ", &saveptr);
            if (!token) {
                fprintf(stderr, "Error: no output file\n");
                return false;
            }
            strncpy(cmd_object->extracted_filename, token, CMDLINE_MAX);
            int fd = open(cmd_object->extracted_filename, O_CREAT | O_WRONLY | O_TRUNC, 0644);
            if (fd < 0) {
                fprintf(stderr, "Error: cannot open output file\n");
                return false;
            }
            close(fd);
            cmd_object->redirect_type = OUTPUT_REDIRECTION;
            //break;
        } else if (strcmp(token, "<") == 0) { //INPUT REDIRECTION
            if (arg_index == 0) {
                fprintf(stderr, "Error: missing command\n");
                return false;
            }
            if (cmd_object->redirect_type == PIPING) {
                fprintf(stderr, "Error: mislocated input redirection\n");
                return false;
            }
            token = strtok_r(NULL, " ", &saveptr);
            if (!token) {
                fprintf(stderr, "Error: no input file\n");
                return false;
            }
            strncpy(cmd_object->extracted_filename, token, CMDLINE_MAX);
            int fd = open(cmd_object->extracted_filename, O_RDONLY);
            if (fd < 0){
                fprintf(stderr, "Error: cannot open input file\n");
                return false;
            }
            close(fd);
            cmd_object->redirect_type = INPUT_REDIRECTION;
            continue;
        } else {
            last_symbol_is_pipe = false;
            if (arg_index >= ARG_MAX - 1) {
                fprintf(stderr, "Error: too many process arguments\n");
                return false;
            }
            parsed_cmds[cmd_count][arg_index++] = token; //put token into parsed command normally
        }
        token = strtok_r(NULL, " ", &saveptr);
        if (token == NULL && last_symbol_is_pipe) {
            fprintf(stderr, "Error: missing command\n");
            return false;
        }
    }

    parsed_cmds[cmd_count][arg_index] = NULL;
    cmd_count++;

    cmd_object->commands = malloc(sizeof(char**) * (cmd_count + 1)); //allocate memory for each new command
    for (int i = 0; i < cmd_count; i++) {
        cmd_object->commands[i] = parsed_cmds[i];
    }
    cmd_object->commands[cmd_count] = NULL;

    cmd_object->args = parsed_cmds[0];
    if (cmd_count > 1 && cmd_object->redirect_type != OUTPUT_REDIRECTION) {
        cmd_object->redirect_type = PIPING;
    }

    return true;
}

int main(void) {
    char cmd[CMDLINE_MAX];
    char *eof;
    pid_t bg_pid = -1;
    int bg_status;
    char bg_cmd[CMDLINE_MAX] = "";

    while (1) {
        char *nl;
        int retval;

        if (bg_pid > 0) { //check if background job completed
            pid_t result = waitpid(bg_pid, &bg_status, WNOHANG);
            if (result > 0) {
                fprintf(stderr, "+ completed '%s' [%d]\n", bg_cmd, WEXITSTATUS(bg_status));
                bg_pid = -1;
                bg_cmd[0] = '\0';
            }
        }

        printf("sshell@ucd$ ");
        fflush(stdout);

        eof = fgets(cmd, CMDLINE_MAX, stdin);
        if (!eof)
            strncpy(cmd, "exit\n", CMDLINE_MAX);

        if (!isatty(STDIN_FILENO)) {
            printf("%s", cmd);
            fflush(stdout);
        }

        nl = strchr(cmd, '\n');
        if (nl)
            *nl = '\0';

        if (!strcmp(cmd, "")) {
            continue;
        }

        struct Command cmd_object = {0}; //empty cmd_object
        strncpy(cmd_object.raw_cmd, cmd, CMDLINE_MAX);
        cmd_object.redirect_type = NO_REDIRECTION;
        cmd_object.background = false;

        if (!parse_command(&cmd_object)) {
            continue;
        }

        //builtin commands
        if (!strcmp(cmd_object.args[0], "exit")) {
            if (bg_pid > 0) {
                fprintf(stderr, "Error: active job still running\n");
                fprintf(stderr, "+ completed 'exit' [1]\n");
                continue;
            }
            fprintf(stderr, "Bye...\n");
            fprintf(stderr, "+ completed '%s' [%d]\n", cmd_object.raw_cmd, EXIT_SUCCESS);
            break;
        } else if (!strcmp(cmd_object.args[0], "pwd")) {
            char cwd[CMDLINE_MAX];
            getcwd(cwd, sizeof(cwd));
            printf("%s\n", cwd);
            fprintf(stderr, "+ completed '%s' [%d]\n", cmd_object.raw_cmd, 0);
            continue;
        } else if (!strcmp(cmd_object.args[0], "cd")) {
            if (cmd_object.args[1] && chdir(cmd_object.args[1]) == 0) {
                fprintf(stderr, "+ completed '%s' [%d]\n", cmd_object.raw_cmd, 0);
            } else {
                fprintf(stderr, "Error: cannot cd into directory\n");
                fprintf(stderr, "+ completed '%s' [%d]\n", cmd_object.raw_cmd, 1);
            }
            continue;
        }

        if (cmd_object.redirect_type == PIPING) { //PIPING
            int retval_list[MAX_CMDS];
            int num_cmds = 0;
            while (cmd_object.commands[num_cmds]) num_cmds++;

            int pipefd[2 * (num_cmds - 1)];
            for (int i = 0; i < num_cmds - 1; ++i) {
                if (pipe(pipefd + i * 2) < 0) {
                    exit(1);
                }
            }

            pid_t pids[MAX_CMDS];
            for (int i = 0; i < num_cmds; ++i) {
                pids[i] = fork(); //create fork for each cmd
                if (pids[i] == 0) {
                    if (i > 0) {
                        dup2(pipefd[(i - 1) * 2], STDIN_FILENO);
                    }
                    if (i < num_cmds - 1) {
                        dup2(pipefd[i * 2 + 1], STDOUT_FILENO);
                    }
                    for (int j = 0; j < 2 * (num_cmds - 1); ++j) {
                        close(pipefd[j]);
                    }
                    execvp(cmd_object.commands[i][0], cmd_object.commands[i]);
                    fprintf(stderr, "Error: command not found\n");
                    exit(1);
                }
            }

            for (int i = 0; i < 2 * (num_cmds - 1); ++i) {
                close(pipefd[i]);
            }

            for (int i = 0; i < num_cmds; ++i) {
                waitpid(pids[i], &retval_list[i], 0);
            }

            fprintf(stderr, "+ completed '%s' ", cmd_object.raw_cmd);
            for (int i = 0; i < num_cmds; ++i) {
                fprintf(stderr, "[%d]", WEXITSTATUS(retval_list[i]));
            }
            fprintf(stderr, "\n");
        } else { //normal execution
            pid_t pid = fork();
            if (pid == 0) {
                if (cmd_object.redirect_type == OUTPUT_REDIRECTION) {
                    int fd = open(cmd_object.extracted_filename, O_CREAT | O_WRONLY | O_TRUNC, 0644);
                    dup2(fd, STDOUT_FILENO);
                    close(fd);
                } else if (cmd_object.redirect_type == INPUT_REDIRECTION) {
                    int fd = open(cmd_object.extracted_filename, O_RDONLY);
                    dup2(fd, STDIN_FILENO);
                    close(fd);
                }
                execvp(cmd_object.args[0], cmd_object.args);
                fprintf(stderr, "Error: command not found\n");
                exit(1);
            } else if (pid > 0) {
                if (cmd_object.background) {
                    bg_pid = pid;
                    strncpy(bg_cmd, cmd_object.raw_cmd, CMDLINE_MAX);
                } else {
                    waitpid(pid, &retval, 0);
                    if (bg_pid > 0) {
                        pid_t result = waitpid(bg_pid, &bg_status, WNOHANG);
                        if (result > 0) {
                            fprintf(stderr, "+ completed '%s' [%d]\n", bg_cmd, WEXITSTATUS(bg_status));
                            bg_pid = -1;
                        }
                    }
                    fprintf(stderr, "+ completed '%s' [%d]\n", cmd_object.raw_cmd, WEXITSTATUS(retval));
                }
            } else {
                exit(1);
            }
        }

        free(cmd_object.commands);
    }

    return EXIT_SUCCESS;
}
