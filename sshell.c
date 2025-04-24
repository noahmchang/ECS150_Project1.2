#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

#define CMDLINE_MAX 512
#define ARG_MAX 16 //adjustable max num of command args
#define TOKEN_MAX 32 //max length of each token
#define CMD_MAX 4

enum RedirectType {
        OUTPUT_REDIRECTION,
        INPUT_REDIRECTION,
        PIPING
};

struct Command {
        char** args; //tokenized command
        char raw_cmd[CMDLINE_MAX]; //raw command for output
        char extracted_filename[CMDLINE_MAX];
        enum RedirectType redirect_type;
        char commands
};

bool parse_command(struct Command* cmd_object) {
        static char* parsed_cmd[ARG_MAX];

        char cmd[CMDLINE_MAX];
        strncpy(cmd, (*cmd_object).raw_cmd, CMDLINE_MAX);

        char *token = strtok(cmd, " "); //split string into tokens
        int i = 0;
        bool redirection_found = false;
        bool pipe_found = false;

        while (token != NULL) {
                //OUTPUT REDIRECTION
                char *redirection_position = strchr(token, '>');
                char *pipe_position = strchr(token, '|');
                if (redirection_position && !redirection_found) {
                        redirection_found = true;
                        *redirection_position = '\0';
                        char *filename = redirection_position + 1;

                        if (token[0] != '\0') {
                                parsed_cmd[i++] = token;
                                if (i > ARG_MAX) {
                                        fprintf(stderr, "Error: too many process arguments\n");
                                        return false;
                                }
                        } else if (i == 0) {
                                fprintf(stderr, "Error: missing command\n");
                                return false;
                        }

                        while (*filename == ' ') {
                                filename++;
                        }

                        if (*filename =='\0') {
                                token = strtok(NULL, " ");
                                if (token == NULL) {
                                        fprintf(stderr, "Error: no output file\n");
                                        return false;
                                }
                                filename = token;
                        }
                        
                        strncpy((*cmd_object).extracted_filename, filename, CMDLINE_MAX);
                        (*cmd_object).redirect_type = OUTPUT_REDIRECTION;
                        break;
                } else if (pipe_position && !pipe_found) {
                        pipe_found = true;
                        *pipe_position = '\0';
                        char *filename = pipe_position + 1;

                        if (token[0] != '\0') {
                                parsed_cmd[i++] = token;
                                if (i > ARG_MAX) {
                                        fprintf(stderr, "Error: too many process arguments\n");
                                        return false;
                                }
                        } else if (i == 0) {
                                fprintf(stderr, "Error: missing command\n");
                                return false;
                        }

                        while (*filename == ' ') {
                                filename++;
                        }

                        if (*filename =='\0') {
                                token = strtok(NULL, " ");
                                if (token == NULL) {
                                        fprintf(stderr, "Error: no output file\n");
                                        return false;
                                }
                                filename = token;
                        }
                        
                        strncpy((*cmd_object).extracted_filename, filename, CMDLINE_MAX);
                        (*cmd_object).redirect_type = OUTPUT_REDIRECTION;
                        break;
                }
                //OUTPUT REDIRECTION (assuming it only happens once)
                // else if (!strcmp(token, ">")){
                //         if (redirection_found) {
                //                 fprintf(stderr, "Error: multiple output redirection\n");
                //                 return false;
                //         }
                //         redirection_found = true;
                //         token = strtok(NULL, " ");
                //         if (token == NULL) {
                //                 fprintf(stderr, "Error: no output file specified\n");
                //                 return false;
                //         }
                //         strncpy((*cmd_object).extracted_filename, token, CMDLINE_MAX);
                //         (*cmd_object).redirect_type = OUTPUT_REDIRECTION;
                //         break;
                // }
        
        // } else if (!strcmp(token, "<")) {
        //         if (redirection_found) {
        //                 fprintf(stderr, "Error: multiple output redirection\n");
        //                 return;
        //         }
        //         redirection_found = true;
        //         token = strtok(NULL, " ");
        //         if (token == NULL) {
        //                 fprintf(stderr, "Error: no output file specified\n");
        //                 return;
        //         }
        //         strncpy((*cmd_object).extracted_filename, token, CMDLINE_MAX);
        //         (*cmd_object).redirect_type = INPUT_REDIRECTION;
        //         break;
        // }

                else {
                        //TOKENIZATION
                        if (i >= ARG_MAX) {
                                fprintf(stderr, "Error: too many process arguments\n");
                                return false; //exit process if null
                        }
                        parsed_cmd[i] = token;
                        i++;
                        token = strtok(NULL, " ");
                }
        }
        parsed_cmd[i] = NULL; //last token has to be null for execvp
        (*cmd_object).args = parsed_cmd;
        return true;
}

int main(void)
{
        char cmd[CMDLINE_MAX];
        char *eof;

        while (1) {
                char *nl;
                int retval;

                /* Print prompt */
                printf("sshell@ucd$ ");
                fflush(stdout);

                /* Get command line */
                eof = fgets(cmd, CMDLINE_MAX, stdin);
                if (!eof)
                        /* Make EOF equate to exit */
                        strncpy(cmd, "exit\n", CMDLINE_MAX);

                /* Print command line if stdin is not provided by terminal */
                if (!isatty(STDIN_FILENO)) {
                        printf("%s", cmd);
                        fflush(stdout);
                }

                /* Remove trailing newline from command line */
                nl = strchr(cmd, '\n');
                if (nl)
                        *nl = '\0';

                if (!strcmp(cmd, "")) {
                        continue; //empty command
                }

                struct Command cmd_object;
                strncpy(cmd_object.raw_cmd, cmd, CMDLINE_MAX);
                bool parse_success = parse_command(&cmd_object); //parse into tokens
                if (!parse_success) {
                        continue;
                }
                // for(int i=0; i < 2; i++){
                //         printf("ALLMYTOKENS::%s\n", cmd_object.args[i]);
                // }
                // printf("EXTRACTEDFILNAME::%s\n\n\n\n\n", cmd_object.extracted_filename);

                /* Builtin commands */
                if (!strcmp(cmd_object.args[0], "exit")) {
                        fprintf(stderr, "Bye...\n");
                        fprintf(stderr, "+ completed '%s' [%d]\n", cmd_object.raw_cmd, EXIT_SUCCESS);
                        break;
                } else if (!strcmp(cmd_object.args[0], "pwd")) {
                        char cwd[CMDLINE_MAX]; 
                        getcwd(cwd, sizeof(cwd)); //assume cwd always executes successfully
                        printf("%s\n", cwd);
                        fprintf(stderr, "+ completed '%s' [%d]\n", cmd_object.raw_cmd, 0);
                        continue;
                } else if (!strcmp(cmd_object.args[0], "cd")) { //assume chdir always executes successfully
                        if (chdir(cmd_object.args[1]) == 0){
                                fprintf(stderr, "+ completed '%s' [%d]\n", cmd_object.raw_cmd, 0);
                        }
                        else {
                                fprintf(stderr, "Error: cannot cd into directory\n");
                                fprintf(stderr, "+ completed '%s' [%d]\n", cmd_object.raw_cmd, 1);
                        }
                        
                        continue;
                }
                
                // int pipefd[2];
                // pipe(pipefd);
                int fd;

                pid_t pid = fork();
                if (pid == 0) {
                        if (cmd_object.redirect_type == OUTPUT_REDIRECTION) {
                                fd = open(cmd_object.extracted_filename, O_CREAT | O_WRONLY | O_TRUNC, 0644);
                                dup2(fd, STDOUT_FILENO);
                                close(fd);
                                execvp(cmd_object.args[0], cmd_object.args);
                                fprintf(stderr, "Error: command not found\n");
                                exit(1);
                        } 
                        // else if (cmd_object.redirection_type == INPUT_REDIRECTION) {
                                
                        // }
                        
                        execvp(cmd_object.args[0], cmd_object.args);
                        fprintf(stderr, "Error: command not found\n");
                        exit(1);
                } else if (pid > 0) {
                        waitpid(pid, &retval, 0);
                } else {
                        exit(1); //fork fails
                }

                fprintf(stderr, "+ completed '%s' [%d]\n", cmd_object.raw_cmd, WEXITSTATUS(retval));
        }

        return EXIT_SUCCESS;
}
