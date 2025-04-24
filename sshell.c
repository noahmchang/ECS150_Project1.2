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
    PIPING
};

struct Command {
    char raw_cmd[CMDLINE_MAX];
    char extracted_filename[CMDLINE_MAX];
    enum RedirectType redirect_type;
    char*** commands;
    char** args;
};

bool parse_command(struct Command* cmd_object) {
    static char* parsed_cmds[MAX_CMDS][ARG_MAX];
    int cmd_count = 0;
    int arg_index = 0;

    char *token;
    char *saveptr;
    char temp[CMDLINE_MAX];
    strncpy(temp, cmd_object->raw_cmd, CMDLINE_MAX);

    token = strtok_r(temp, " ", &saveptr);
    while (token != NULL) {
        if (strcmp(token, "|") == 0) {
            parsed_cmds[cmd_count][arg_index] = NULL;
            cmd_count++;
            if (cmd_count >= MAX_CMDS) {
                fprintf(stderr, "Error: too many commands\n");
                return false;
            }
            arg_index = 0;
        } else if (strcmp(token, ">") == 0) {
            token = strtok_r(NULL, " ", &saveptr);
            if (!token) {
                fprintf(stderr, "Error: no output file\n");
                return false;
            }
            strncpy(cmd_object->extracted_filename, token, CMDLINE_MAX);
            cmd_object->redirect_type = OUTPUT_REDIRECTION;
            break;
        } else {
            if (arg_index >= ARG_MAX - 1) {
                fprintf(stderr, "Error: too many process arguments\n");
                return false;
            }
            parsed_cmds[cmd_count][arg_index++] = token;
        }
        token = strtok_r(NULL, " ", &saveptr);
    }

    parsed_cmds[cmd_count][arg_index] = NULL;
    cmd_count++;

    cmd_object->commands = malloc(sizeof(char**) * (cmd_count + 1));
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

    while (1) {
        char *nl;
        int retval;

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

        if (!strcmp(cmd, ""))
            continue;

        struct Command cmd_object = {0};
        strncpy(cmd_object.raw_cmd, cmd, CMDLINE_MAX);
        cmd_object.redirect_type = NO_REDIRECTION;

        if (!parse_command(&cmd_object))
            continue;

        // Builtin commands
        if (!strcmp(cmd_object.args[0], "exit")) {
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

        if (cmd_object.redirect_type == PIPING) {
            int num_cmds = 0;
            while (cmd_object.commands[num_cmds]) num_cmds++;

            int pipefd[2 * (num_cmds - 1)];
            for (int i = 0; i < num_cmds - 1; ++i) {
                if (pipe(pipefd + i * 2) < 0) {
                    perror("pipe");
                    exit(1);
                }
            }

            pid_t pids[MAX_CMDS];
            for (int i = 0; i < num_cmds; ++i) {
                pids[i] = fork();
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
                waitpid(pids[i], &retval, 0);
            }

            fprintf(stderr, "+ completed '%s' [%d]\n", cmd_object.raw_cmd, WEXITSTATUS(retval));
        } else {
            pid_t pid = fork();
            if (pid == 0) {
                if (cmd_object.redirect_type == OUTPUT_REDIRECTION) {
                    int fd = open(cmd_object.extracted_filename, O_CREAT | O_WRONLY | O_TRUNC, 0644);
                    dup2(fd, STDOUT_FILENO);
                    close(fd);
                }
                execvp(cmd_object.args[0], cmd_object.args);
                fprintf(stderr, "Error: command not found\n");
                exit(1);
            } else if (pid > 0) {
                waitpid(pid, &retval, 0);
            } else {
                exit(1);
            }

            fprintf(stderr, "+ completed '%s' [%d]\n", cmd_object.raw_cmd, WEXITSTATUS(retval));
        }

        free(cmd_object.commands);
    }

    return EXIT_SUCCESS;
}

// #include <stdio.h>
// #include <stdlib.h>
// #include <stdbool.h>
// #include <string.h>
// #include <unistd.h>
// #include <sys/wait.h>
// #include <fcntl.h>

// #define CMDLINE_MAX 512
// #define ARG_MAX 16 //adjustable max num of command args
// #define TOKEN_MAX 32 //max length of each token
// #define MAX_CMDS 4

// enum RedirectType {
//         OUTPUT_REDIRECTION,
//         INPUT_REDIRECTION,
//         PIPING
// };

// struct Command {
//         char** args; //tokenized command
//         char raw_cmd[CMDLINE_MAX]; //raw command for output
//         char extracted_filename[CMDLINE_MAX];
//         enum RedirectType redirect_type;
//         char*** commands;
// };

// bool parse_command(struct Command* cmd_object) {
//         static char* parsed_cmd[ARG_MAX];

//         char cmd[CMDLINE_MAX];
//         strncpy(cmd, (*cmd_object).raw_cmd, CMDLINE_MAX);

//         char *token = strtok(cmd, " "); //split string into tokens
//         int i = 0;
//         int j = 0;
//         bool redirection_found = false;
//         //bool pipe_found = false;

//         while (token != NULL) {
//                 if (j >= MAX_CMDS) {
//                         break;
//                 }
//                 //OUTPUT REDIRECTION
//                 char *redirection_position = strchr(token, '>');
//                 char *pipe_position = strchr(token, '|');
//                 if (redirection_position && !redirection_found) {
//                         redirection_found = true;
//                         *redirection_position = '\0';
//                         char *filename = redirection_position + 1;

//                         if (token[0] != '\0') {
//                                 parsed_cmd[i++] = token;
//                                 if (i > ARG_MAX) {
//                                         fprintf(stderr, "Error: too many process arguments\n");
//                                         return false;
//                                 }
//                         } else if (i == 0) {
//                                 fprintf(stderr, "Error: missing command\n");
//                                 return false;
//                         }

//                         while (*filename == ' ') {
//                                 filename++;
//                         }

//                         if (*filename =='\0') {
//                                 token = strtok(NULL, " ");
//                                 if (token == NULL) {
//                                         fprintf(stderr, "Error: no output file\n");
//                                         return false;
//                                 }
//                                 filename = token;
//                         }
                        
//                         strncpy((*cmd_object).extracted_filename, filename, CMDLINE_MAX);
//                         (*cmd_object).redirect_type = OUTPUT_REDIRECTION;
//                         break;
//                 } else if (pipe_position) { //PIPING
//                         pipe_found = true;
//                         *pipe_position = '\0';

//                         if (token[0] != '\0') {
//                                 parsed_cmd[i++] = token;
//                                 if (i > ARG_MAX) {
//                                         fprintf(stderr, "Error: too many process arguments\n");
//                                         return false;
//                                 }
//                         } else if (i == 0) {
//                                 fprintf(stderr, "Error: missing command\n");
//                                 return false;
//                         }

//                         if (*filename =='\0') {
//                                 token = strtok(NULL, " "); // RESTART TOKEN PARSING BUT PUT IN SECOND CMD_OBJECT->COMMAND[1]
//                                 if (token == NULL) {
//                                         fprintf(stderr, "Error: no pipe input command\n");
//                                         return false;
//                                 }
//                                 filename = token;
//                         }
//                         (*cmd_object).redirect_type = PIPING;
//                         i = 0;
//                         (*cmd_object).commands[j] = parsed_cmd;
//                         j++;
//                 }
//                 else {
//                         //TOKENIZATION
//                         if (i >= ARG_MAX) {
//                                 fprintf(stderr, "Error: too many process arguments\n");
//                                 return false; //exit process if null
//                         }
//                         parsed_cmd[i] = token;
//                         i++;
//                         token = strtok(NULL, " ");
//                 }
//         }
//         parsed_cmd[i] = NULL; //last token has to be null for execvp
//         (*cmd_object).args = parsed_cmd;
        
//         return true;
// }

// int main(void)
// {
//         char cmd[CMDLINE_MAX];
//         char *eof;

//         while (1) {
//                 char *nl;
//                 int retval;

//                 /* Print prompt */
//                 printf("sshell@ucd$ ");
//                 fflush(stdout);

//                 /* Get command line */
//                 eof = fgets(cmd, CMDLINE_MAX, stdin);
//                 if (!eof)
//                         /* Make EOF equate to exit */
//                         strncpy(cmd, "exit\n", CMDLINE_MAX);

//                 /* Print command line if stdin is not provided by terminal */
//                 if (!isatty(STDIN_FILENO)) {
//                         printf("%s", cmd);
//                         fflush(stdout);
//                 }

//                 /* Remove trailing newline from command line */
//                 nl = strchr(cmd, '\n');
//                 if (nl)
//                         *nl = '\0';

//                 if (!strcmp(cmd, "")) {
//                         continue; //empty command
//                 }

//                 struct Command cmd_object;
//                 strncpy(cmd_object.raw_cmd, cmd, CMDLINE_MAX);
//                 //PARSING
//                 bool parse_success = parse_command(&cmd_object); //parse into tokens
//                 if (!parse_success) {
//                         continue;
//                 } 
                
//                 // for(int i=0; i < 2; i++){
//                 //         printf("ALLMYTOKENS::%s\n", cmd_object.args[i]);
//                 // }
//                 // printf("EXTRACTEDFILNAME::%s\n\n\n\n\n", cmd_object.extracted_filename);

//                 /* Builtin commands */
//                 if (!strcmp(cmd_object.args[0], "exit")) {
//                         fprintf(stderr, "Bye...\n");
//                         fprintf(stderr, "+ completed '%s' [%d]\n", cmd_object.raw_cmd, EXIT_SUCCESS);
//                         break;
//                 } else if (!strcmp(cmd_object.args[0], "pwd")) {
//                         char cwd[CMDLINE_MAX]; 
//                         getcwd(cwd, sizeof(cwd)); //assume cwd always executes successfully
//                         printf("%s\n", cwd);
//                         fprintf(stderr, "+ completed '%s' [%d]\n", cmd_object.raw_cmd, 0);
//                         continue;
//                 } else if (!strcmp(cmd_object.args[0], "cd")) { //assume chdir always executes successfully
//                         if (chdir(cmd_object.args[1]) == 0){
//                                 fprintf(stderr, "+ completed '%s' [%d]\n", cmd_object.raw_cmd, 0);
//                         }
//                         else {
//                                 fprintf(stderr, "Error: cannot cd into directory\n");
//                                 fprintf(stderr, "+ completed '%s' [%d]\n", cmd_object.raw_cmd, 1);
//                         }
                        
//                         continue;
//                 }
                
//                 // int pipefd[2];
//                 // pipe(pipefd);
//                 int fd;

//                 pid_t pid = fork();
//                 if (pid == 0) {
//                         if (cmd_object.redirect_type == OUTPUT_REDIRECTION) {
//                                 fd = open(cmd_object.extracted_filename, O_CREAT | O_WRONLY | O_TRUNC, 0644);
//                                 dup2(fd, STDOUT_FILENO);
//                                 close(fd);
//                                 execvp(cmd_object.args[0], cmd_object.args);
//                                 fprintf(stderr, "Error: command not found\n");
//                                 exit(1);
//                         } 
//                         // else if (cmd_object.redirection_type == INPUT_REDIRECTION) {
                                
//                         // }
                        
//                         execvp(cmd_object.args[0], cmd_object.args);
//                         fprintf(stderr, "Error: command not found\n");
//                         exit(1);
//                 } else if (pid > 0) {
//                         waitpid(pid, &retval, 0);
//                 } else {
//                         exit(1); //fork fails
//                 }

//                 fprintf(stderr, "+ completed '%s' [%d]\n", cmd_object.raw_cmd, WEXITSTATUS(retval));
//         }

//         return EXIT_SUCCESS;
// }


// #include <stdio.h>
// #include <stdlib.h>
// #include <stdbool.h>
// #include <string.h>
// #include <unistd.h>
// #include <sys/wait.h>
// #include <fcntl.h>

// #define CMDLINE_MAX 512
// #define ARG_MAX 16 //adjustable max num of command args
// #define TOKEN_MAX 32 //max length of each token
// #define MAX_CMDS 4

// enum RedirectType {
//         OUTPUT_REDIRECTION,
//         INPUT_REDIRECTION,
//         PIPING
// };

// struct Command {
//         char** args; //tokenized command
//         char raw_cmd[CMDLINE_MAX]; //raw command for output
//         char extracted_filename[CMDLINE_MAX];
//         enum RedirectType redirect_type;
//         char*** commands;
// };

// bool parse_command(struct Command* cmd_object) {
//         static char* parsed_cmd[ARG_MAX];

//         char cmd[CMDLINE_MAX];
//         strncpy(cmd, (*cmd_object).raw_cmd, CMDLINE_MAX);

//         char *token = strtok(cmd, " "); //split string into tokens
//         int i = 0;
//         bool redirection_found = false;
//         bool pipe_found = false;

//         while (token != NULL) {
//                 //OUTPUT REDIRECTION
//                 char *redirection_position = strchr(token, '>');
//                 char *pipe_position = strchr(token, '|');
//                 if (redirection_position && !redirection_found) {
//                         redirection_found = true;
//                         *redirection_position = '\0';
//                         char *filename = redirection_position + 1;

//                         if (token[0] != '\0') {
//                                 parsed_cmd[i++] = token;
//                                 if (i > ARG_MAX) {
//                                         fprintf(stderr, "Error: too many process arguments\n");
//                                         return false;
//                                 }
//                         } else if (i == 0) {
//                                 fprintf(stderr, "Error: missing command\n");
//                                 return false;
//                         }

//                         while (*filename == ' ') {
//                                 filename++;
//                         }

//                         if (*filename =='\0') {
//                                 token = strtok(NULL, " ");
//                                 if (token == NULL) {
//                                         fprintf(stderr, "Error: no output file\n");
//                                         return false;
//                                 }
//                                 filename = token;
//                         }
                        
//                         strncpy((*cmd_object).extracted_filename, filename, CMDLINE_MAX);
//                         (*cmd_object).redirect_type = OUTPUT_REDIRECTION;
//                         break;
//                 } else if (pipe_position && !pipe_found) {
//                         pipe_found = true;
//                         *pipe_position = '\0';
//                         char *filename = pipe_position + 1;

//                         if (token[0] != '\0') {
//                                 parsed_cmd[i++] = token;
//                                 if (i > ARG_MAX) {
//                                         fprintf(stderr, "Error: too many process arguments\n");
//                                         return false;
//                                 }
//                         } else if (i == 0) {
//                                 fprintf(stderr, "Error: missing command\n");
//                                 return false;
//                         }

//                         while (*filename == ' ') {
//                                 filename++;
//                         }

//                         if (*filename =='\0') {
//                                 token = strtok(NULL, " "); //   RESTART BUT PUT IN SECOND COMMAND 
//                                 if (token == NULL) {
//                                         fprintf(stderr, "Error: no pipe input command\n");
//                                         return false;
//                                 }
//                                 filename = token;
//                         }
                        
//                         strncpy((*cmd_object).extracted_filename, filename, CMDLINE_MAX);
//                         (*cmd_object).redirect_type = OUTPUT_REDIRECTION;
//                         break;
//                 }
//                 //OUTPUT REDIRECTION (assuming it only happens once)
//                 // else if (!strcmp(token, ">")){
//                 //         if (redirection_found) {
//                 //                 fprintf(stderr, "Error: multiple output redirection\n");
//                 //                 return false;
//                 //         }
//                 //         redirection_found = true;
//                 //         token = strtok(NULL, " ");
//                 //         if (token == NULL) {
//                 //                 fprintf(stderr, "Error: no output file specified\n");
//                 //                 return false;
//                 //         }
//                 //         strncpy((*cmd_object).extracted_filename, token, CMDLINE_MAX);
//                 //         (*cmd_object).redirect_type = OUTPUT_REDIRECTION;
//                 //         break;
//                 // }
        
//         // } else if (!strcmp(token, "<")) {
//         //         if (redirection_found) {
//         //                 fprintf(stderr, "Error: multiple output redirection\n");
//         //                 return;
//         //         }
//         //         redirection_found = true;
//         //         token = strtok(NULL, " ");
//         //         if (token == NULL) {
//         //                 fprintf(stderr, "Error: no output file specified\n");
//         //                 return;
//         //         }
//         //         strncpy((*cmd_object).extracted_filename, token, CMDLINE_MAX);
//         //         (*cmd_object).redirect_type = INPUT_REDIRECTION;
//         //         break;
//         // }

//                 else {
//                         //TOKENIZATION
//                         if (i >= ARG_MAX) {
//                                 fprintf(stderr, "Error: too many process arguments\n");
//                                 return false; //exit process if null
//                         }
//                         parsed_cmd[i] = token;
//                         i++;
//                         token = strtok(NULL, " ");
//                 }
//         }
//         parsed_cmd[i] = NULL; //last token has to be null for execvp
//         (*cmd_object).args = parsed_cmd;
        
//         return true;
// }

// int main(void)
// {
//         char cmd[CMDLINE_MAX];
//         char *eof;

//         while (1) {
//                 char *nl;
//                 int retval;

//                 /* Print prompt */
//                 printf("sshell@ucd$ ");
//                 fflush(stdout);

//                 /* Get command line */
//                 eof = fgets(cmd, CMDLINE_MAX, stdin);
//                 if (!eof)
//                         /* Make EOF equate to exit */
//                         strncpy(cmd, "exit\n", CMDLINE_MAX);

//                 /* Print command line if stdin is not provided by terminal */
//                 if (!isatty(STDIN_FILENO)) {
//                         printf("%s", cmd);
//                         fflush(stdout);
//                 }

//                 /* Remove trailing newline from command line */
//                 nl = strchr(cmd, '\n');
//                 if (nl)
//                         *nl = '\0';

//                 if (!strcmp(cmd, "")) {
//                         continue; //empty command
//                 }

//                 struct Command cmd_object;
//                 strncpy(cmd_object.raw_cmd, cmd, CMDLINE_MAX);
//                 bool parse_success = parse_command(&cmd_object); //parse into tokens
//                 if (!parse_success) {
//                         continue;
//                 }
//                 // for(int i=0; i < 2; i++){
//                 //         printf("ALLMYTOKENS::%s\n", cmd_object.args[i]);
//                 // }
//                 // printf("EXTRACTEDFILNAME::%s\n\n\n\n\n", cmd_object.extracted_filename);

//                 /* Builtin commands */
//                 if (!strcmp(cmd_object.args[0], "exit")) {
//                         fprintf(stderr, "Bye...\n");
//                         fprintf(stderr, "+ completed '%s' [%d]\n", cmd_object.raw_cmd, EXIT_SUCCESS);
//                         break;
//                 } else if (!strcmp(cmd_object.args[0], "pwd")) {
//                         char cwd[CMDLINE_MAX]; 
//                         getcwd(cwd, sizeof(cwd)); //assume cwd always executes successfully
//                         printf("%s\n", cwd);
//                         fprintf(stderr, "+ completed '%s' [%d]\n", cmd_object.raw_cmd, 0);
//                         continue;
//                 } else if (!strcmp(cmd_object.args[0], "cd")) { //assume chdir always executes successfully
//                         if (chdir(cmd_object.args[1]) == 0){
//                                 fprintf(stderr, "+ completed '%s' [%d]\n", cmd_object.raw_cmd, 0);
//                         }
//                         else {
//                                 fprintf(stderr, "Error: cannot cd into directory\n");
//                                 fprintf(stderr, "+ completed '%s' [%d]\n", cmd_object.raw_cmd, 1);
//                         }
                        
//                         continue;
//                 }
                
//                 // int pipefd[2];
//                 // pipe(pipefd);
//                 int fd;

//                 pid_t pid = fork();
//                 if (pid == 0) {
//                         if (cmd_object.redirect_type == OUTPUT_REDIRECTION) {
//                                 fd = open(cmd_object.extracted_filename, O_CREAT | O_WRONLY | O_TRUNC, 0644);
//                                 dup2(fd, STDOUT_FILENO);
//                                 close(fd);
//                                 execvp(cmd_object.args[0], cmd_object.args);
//                                 fprintf(stderr, "Error: command not found\n");
//                                 exit(1);
//                         } 
//                         // else if (cmd_object.redirection_type == INPUT_REDIRECTION) {
                                
//                         // }
                        
//                         execvp(cmd_object.args[0], cmd_object.args);
//                         fprintf(stderr, "Error: command not found\n");
//                         exit(1);
//                 } else if (pid > 0) {
//                         waitpid(pid, &retval, 0);
//                 } else {
//                         exit(1); //fork fails
//                 }

//                 fprintf(stderr, "+ completed '%s' [%d]\n", cmd_object.raw_cmd, WEXITSTATUS(retval));
//         }

//         return EXIT_SUCCESS;
// }
