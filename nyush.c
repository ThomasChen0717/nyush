#include <stdio.h>
#include <unistd.h>
#include <libgen.h>
#include <string.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
//References: https://www.ibm.com/docs/en/zos/2.3.0?topic=functions-getcwd-get-path-name-working-directory, https://man7.org/linux/man-pages/man3/basename.3.html, ibm.com/docs/en/zos/2.4.0?topic=functions-waitpid-wait-specific-child-process-end, https://man7.org/linux/man-pages/man3/exec.3.html, https://www.tutorialspoint.com/c_standard_library/c_function_strchr.html, https://man7.org/linux/man-pages/man2/chdir.2.html, https://pubs.opengroup.org/onlinepubs/007904875/functions/open.html, https://stackoverflow.com/questions/21356795/exit0-in-c-only-works-occasionally, https://man7.org/linux/man-pages/man2/pipe.2.html, https://www.ibm.com/docs/en/zos/2.5.0?topic=functions-getline-read-entire-line-from-stream

char* buffer_pointer;
size_t buffer_size = 256;   
pid_t pid;
int status_pointer;
char** argHead;
char cwd[256];
char** suspended_list;
int suspended_listCount;
int i = 0;
int output_red_overwrite;
int output_red_append;
int input_red;
int pipe_num;
char* output_file;
char* input_file;
char* program_name;
int command_err;
ssize_t eof;
char command[256];
char* buffer;
pid_t* suspended_pid_list;
int fd[2];
int fd_old = 0;





void getcommand();
void shell();
void parser();
void sig_handler();
void cd(char* path);
void built_in();
void exitShell();
void jobs();
void fg(int index);


int main() {
    suspended_list = malloc(sizeof(char*) * 100);
    suspended_pid_list = malloc(sizeof(pid_t) * 100);
    shell();
}

void shell(){
    while(1){
        signal(SIGINT, sig_handler);
        signal(SIGQUIT, sig_handler);
        signal(SIGTSTP, sig_handler);
        output_red_overwrite = 0;
        output_red_append = 0;
        input_red = 0;  
        pipe_num = 0;
        command_err = 0;
        getcommand();
        if(eof == -1){
            exit(0);
        }
        buffer = malloc(sizeof(char) * 256);
        strcpy(buffer, buffer_pointer);
        if(strstr(buffer_pointer, "cd") || strstr(buffer_pointer, "exit") || strstr(buffer_pointer, "jobs") || strstr(buffer_pointer, "fg")){
            built_in();
            if(!command_err){
                char** argIterator = argHead;
                for(; *argIterator; argIterator++){
                    free(*argIterator);
                }
                free(argHead);
            }
            if(command_err == 1)
            {
                fprintf(stderr, "Error: invalid command\n"); 
            }
            continue;
        }
        parser();
        if(command_err == 1)
        {
            fprintf(stderr, "Error: invalid command\n");
            continue;   
        }
        if (strcmp(argHead[0], "") == 0) {
                continue;
        }  
        
        for(int j = 0; j < pipe_num + 1; j++){
            char** arguments = malloc(sizeof(char*) * 256);
            char** argumentHead = arguments;
            for(; *argHead; argHead++){
                if(strcmp(*argHead,"|") != 0){
                    *arguments = malloc(sizeof(char) * 256);
                    strcpy(*arguments, *argHead);
                    arguments++;
                }
                else{
                    argHead++;
                    break;
                }
            } 
            if(!strchr(argumentHead[0], '/')){
                strcpy(command, "/usr/bin/");
                strcat(command, argumentHead[0]);
            }
            else{
                strcpy(command, argumentHead[0]);
            }
            if (j != pipe_num) {
                if (pipe(fd) < 0) {
                    fprintf(stderr, "Error: pipe failed");
                    continue;
                }
            }
            pid = fork();
            if(pid < 0){
                fprintf(stderr, "Error: Fork failed\n");
                continue;
            }
            else if(pid == 0){
                if(output_red_overwrite == 1){
                    int fd = open(output_file, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
                    dup2(fd, 1);
                    close(fd);
                    free(output_file);
                }
                else if(output_red_append == 1){
                    int fd = open(output_file, O_CREAT | O_WRONLY | O_APPEND, S_IRUSR | S_IWUSR);
                    dup2(fd, 1);
                    close(fd);
                    free(output_file);
                }
                if(input_red == 1){
                    int fd2 = open(input_file, O_RDONLY, S_IRUSR | S_IWUSR);
                    if(fd2 == -1){
                        fprintf(stderr, "Error: invalid file\n");
                        exit(0);
                        continue;  
                    }
                    dup2(fd2,0);
                    close(fd2);
                    free(input_file);
                }
                if(pipe_num > 0){
                    dup2(fd_old, 0);
                    if(j != pipe_num){
                        dup2(fd[1],1);
                    }
                    close(fd[0]);
                }
                execv(command, argumentHead);
                fprintf(stderr, "Error: invalid program\n");
                exit(0);
            }
            else{
                if(pipe_num > 0){
                        close(fd[1]);
                    fd_old = fd[0];
                }
                pid_t p = waitpid(-1,&status_pointer,WUNTRACED);
                if(WIFSTOPPED(status_pointer)){
                suspended_list[i] = malloc(sizeof(char) * 256);
                suspended_pid_list[i] = p;
                strcpy(suspended_list[i], buffer);
                suspended_listCount++;
                i++;
                }
            }
        }
        free(buffer_pointer);
        free(buffer);
    }
}

void getcommand(){
    buffer_pointer = malloc(sizeof(char) * 256);
    char* path = basename(getcwd(cwd, sizeof(cwd)));
    printf("[nyush %s]$ ", path);
    fflush(stdout);
    eof = getline(&buffer_pointer, &buffer_size, stdin); 
}

void parser(){
    char* ptr = strtok(buffer_pointer, " ");
    if(strcmp(ptr, ">") == 0 || strcmp(ptr, ">>") == 0 || strcmp(ptr, "<") == 0 || strcmp(ptr, "|") == 0){
        command_err = 1;
        return;
    }
    char** arg = malloc((sizeof(char *) * 20) + 5);
    argHead = arg;
    for(; ptr;){
        if(ptr[strlen(ptr)-1] == '\n'){
        ptr[strlen(ptr)-1] = '\0';
        }
        if(strcmp(ptr, ">") == 0 || strcmp(ptr, ">>") == 0 || strcmp(ptr, "<") == 0 || strcmp(ptr, "<<") == 0 || strcmp(ptr, "|") == 0){
            if(strcmp(ptr, ">") == 0){
                output_red_overwrite += 1;
                if(output_red_overwrite > 1 || (output_red_overwrite == 1 && output_red_append == 1)){
                    command_err = 1;
                    break;
                }
                ptr = strtok(NULL, " ");
                if(ptr){
                    if(ptr[strlen(ptr)-1] == '\n'){
                    ptr[strlen(ptr)-1] = '\0';
                    }
                    output_file = malloc(strlen(ptr) * sizeof(char) + 5);
                    output_file = strcpy(output_file, ptr);
                    ptr = strtok(NULL, " ");
                     if(ptr){
                        if(strcmp(ptr, ">") != 0 && strcmp(ptr, ">>") != 0 && strcmp(ptr, "<") != 0 && strcmp(ptr, "<<") != 0 && strcmp(ptr, "|") != 0){
                        command_err = 1;
                        break;  
                        }
                    }
                    continue;
                }
                else{
                    command_err = 1;
                    break;
                }
            }
            else if(strcmp(ptr, ">>") == 0){
                output_red_append += 1;
                if(output_red_append > 1 || (output_red_overwrite == 1 && output_red_append == 1)){
                    command_err = 1;
                    break;
                }
                ptr = strtok(NULL, " ");
                if(ptr){
                    if(ptr[strlen(ptr)-1] == '\n'){
                    ptr[strlen(ptr)-1] = '\0';
                    }
                    output_file = malloc(strlen(ptr) * sizeof(char) + 5);
                    output_file = strcpy(output_file, ptr);
                    ptr = strtok(NULL, " ");
                    if(ptr){
                        if(strcmp(ptr, ">") != 0 && strcmp(ptr, ">>") != 0 && strcmp(ptr, "<") != 0 && strcmp(ptr, "<<") != 0 && strcmp(ptr, "|") != 0){
                            command_err = 1;
                            break;  
                        }
                    }
                    continue;
                }
                else{
                    command_err = 1;
                    break;
                }
            }
            else if(strcmp(ptr, "<") == 0){
                input_red += 1;
                if(input_red > 1){
                    command_err = 1;
                    break;
                }
                ptr = strtok(NULL, " ");
                if(ptr){
                    if(ptr[strlen(ptr)-1] == '\n'){
                    ptr[strlen(ptr)-1] = '\0';
                    }
                    input_file = malloc(strlen(ptr) * sizeof(char) + 5);
                    input_file = strcpy(input_file, ptr);
                    ptr = strtok(NULL, " ");
                     if(ptr){
                        if(strcmp(ptr, ">") != 0 && strcmp(ptr, ">>") != 0 && strcmp(ptr, "<") != 0 && strcmp(ptr, "<<") != 0 && strcmp(ptr, "|") != 0){
                            command_err = 1;
                            break;  
                        }
                    }
                    continue;
                }
                else{
                    command_err = 1;
                    break;
                }
            }
            else if(strcmp(ptr, "|") == 0){
                pipe_num += 1;
                *arg = malloc(strlen(ptr) * sizeof(char) + 5);
                strcpy(*arg,ptr);
                arg++;
                ptr = strtok(NULL, " ");
                if(!ptr){
                    command_err = 1;
                    break;
                }
                else{
                    continue;
                }
            }
            else{
                command_err = 1;
                break;
            }
        }
        *arg = malloc(strlen(ptr) * sizeof(char) + 5);
        strcpy(*arg,ptr);
        arg++;
        ptr = strtok(NULL, " ");
    }
}

void sig_handler(){

}

void built_in(){
    parser();
    if(!command_err){
        if(strcmp(argHead[0], "cd") == 0){
            if(argHead[2] != NULL || argHead[1] == NULL || output_red_append >= 1 || output_red_overwrite >=1 || input_red >= 1 || pipe_num >= 1){
                fprintf(stderr,"Error: invalid command\n");
            }
            else{
                cd(argHead[1]);
            }
        }
        if(strcmp(argHead[0], "exit") == 0){
            if(argHead[1] != NULL || output_red_append >= 1 || output_red_overwrite >=1 || input_red >= 1 || pipe_num >= 1){
                fprintf(stderr,"Error: invalid command\n");
            }
            else{
                exitShell();  
            }
            
        }
        if(strcmp(argHead[0], "jobs") == 0){
            if(argHead[1] != NULL || output_red_append >= 1 || output_red_overwrite >=1 || input_red >= 1 || pipe_num >= 1){
                fprintf(stderr,"Error: invalid command\n");
            }
            else{
                jobs();  
            }     
        }
        if(strcmp(argHead[0], "fg") == 0){
            if(argHead[2] != NULL || argHead[1] == NULL || output_red_append >= 1 || output_red_overwrite >=1 || input_red >= 1 || pipe_num >= 1){
                fprintf(stderr,"Error: invalid command\n");
            }
            else{
                fg(atoi(argHead[1]));
            }
        }
    }
}

void cd(char* path){
    if(chdir(path) == -1){
        fprintf(stderr, "Error: invalid directory\n");
    }
}

void exitShell(){
    if(suspended_listCount != 0){
        fprintf(stderr, "Error: there are suspended jobs\n");
    }
    else{
        free(suspended_list);
        free(suspended_pid_list);
        exit(0);
    }
}

void jobs(){    
    for(int j = 0; j < suspended_listCount; j++){
        printf("[%d] %s", j+1, suspended_list[j]);
        fflush(stdout);
    }
}

void fg(int index){
    if(index > suspended_listCount || index == 0){
        fprintf(stderr, "Error: invalid job\n");
    }
    else{
        char* process_to_kill = malloc(sizeof(char) * 256);
        pid_t pid_to_kill = suspended_pid_list[index-1];
        strcpy(process_to_kill,suspended_list[index - 1]);
        for(int j = index - 1; j < suspended_listCount -1; j++){
            suspended_list[j] = suspended_list[j+1];
            suspended_pid_list[j] = suspended_pid_list[j+1];
        }
        suspended_listCount--;
        i--;
        kill(pid_to_kill, SIGCONT);
        int status;
        pid_t p = waitpid(-1, &status, WUNTRACED);
        if(WIFSTOPPED(status)){
                suspended_list[i] = malloc(sizeof(char) * 256);
                suspended_pid_list[i] = p;
                strcpy(suspended_list[i], process_to_kill);
                suspended_listCount++;
                i++;
        }
        free(process_to_kill);
    }
}   