/* ************************************************************************** */
/* Author: Hye Yeon Park                                                      */
/* Date: 2/7/2022                                                             */
/* Project: Smallsh                                                           */
/*                                                                            */
/* ************************************************************************** */
#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>

#define MAX_LEN 2048	// Maximum length of characters
#define MAX_ARGS 512	// Maximum length of arguments

int status;				// Exit status
int input_flag;			// Input redirection flag
int output_flag;		// Output redirection flag
char* input_file;		// Input filename
char* output_file;		// Output filename
int fg_flag = 0;		// Foreground only flag
int bg_flag = 0;		// Background process flag
int bg_array[256];		// Array to hold background pids
int bg_count = 0;		// Background process count

// Initialize sigaction structs to be empty
struct sigaction SIGINT_action = { {0} }, SIGTSTP_action = { {0} };


/* ************************************************************************** */
/* Function: handler_SIGTSTP()                                                */
/*           Enter and exit foreground-only mode upon SIGTSTP                 */
/*                                                                            */
/*                                                                            */
/* ************************************************************************** */
void handler_SIGTSTP(int signo) {

	char* message;
	int message_size = 0;

	if (!fg_flag) {
		message = "\nEntering foreground-only mode (& is now ignored)\n";
		message_size = 50;
		fg_flag = 1;
	}
	else {
		message = "\nExiting foreground-only mode\n";
		message_size = 30;
		fg_flag = 0;
	}
	write(STDOUT_FILENO, message, message_size);

	// Add prompt symbol right after calling foreground mode
	char* prompt = ": ";
	message_size = 2;
	write(STDOUT_FILENO, prompt, message_size);
}

/* ************************************************************************** */
/* Function: check_bg_process()                                               */
/*           Checks background process and print exit status                  */
/*                                                                            */
/*                                                                            */
/* ************************************************************************** */
void check_bg_process() {

	// Non-blocking wait; Return 0 immediately if no process has terminated
	pid_t pid = waitpid(-1, &status, WNOHANG);

	while (pid > 0) {

		// Remove terminated processes from background array
		for (int i = 0; i < bg_count; i++) {
			if (bg_array[i] == pid) {
				while (i < bg_count - 1) {
					bg_array[i] = bg_array[i + 1];
					i++;
				}
				bg_array[bg_count - 1] = '\0';
				bg_count--;
				break;
			}
		}
		// Print exit status or termination signal
		if (WIFEXITED(status)) {
			printf("background pid %d is done. exit value %d\n", pid, WEXITSTATUS(status));
			fflush(stdout);
		}
		else if (WIFSIGNALED(status)) {
			printf("background pid %d is done. terminated by signal %d\n", pid, WTERMSIG(status));
			fflush(stdout);
		}
		// Repeat checking
		pid = waitpid(-1, &status, WNOHANG);
	}
}

/* ************************************************************************** */
/* Function: run_others()                                                     */
/*           Execute non built-in commands by using fork(), exec(), waitpid() */
/* Reference: CS344 Exploration: Processes and I/O                            */
/*                                                                            */
/* ************************************************************************** */
int run_others(char** args) {

	// Fork a child process
	pid_t spawn_pid = fork();
	switch (spawn_pid) {

	case -1:
		perror("fork() failed!");
		exit(1);
		break;

	case 0:
		// Child process
		// Input Redirection
		if (input_flag) {
			// Open source file for reading only
			int input_FD = open(input_file, O_RDONLY);
			if (input_FD == -1) {
				printf("cannot open %s for input\n", input_file);
				fflush(stdout);
				exit(1);
			}
			else {
				// Redirect stdin to source file
				int result = dup2(input_FD, 0);
				if (result == -1) {
					perror("dup2");
				}
				close(input_FD);
			}
		}

		// Output Redirection
		if (output_flag) {
			int output_FD = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
			if (output_FD == -1) {
				printf("cannot create %s for output\n", output_file);
				fflush(stdout);
				exit(1);
			}
			else {
				int result = dup2(output_FD, 1);
				if (result == -1) {
					perror("dup2");
				}
				close(output_FD);
			}
		}

		// Background commands
		if (bg_flag) {

			// Ctrl-C; Ignore SIGINT signal
			SIGINT_action.sa_handler = SIG_IGN;
			sigaction(SIGINT, &SIGINT_action, NULL);

			// Redirect input to dev/null if not specified
			if (!input_flag) {
				int dev_null = open("/dev/null", O_RDONLY);
				if (dev_null == -1) {
					printf("cannot set /dev/null to input\n");
					fflush(stdout);
					exit(1);
				}
				else {
					int result = dup2(dev_null, 0);
					if (result == -1) {
						perror("dup2");
					}
					close(dev_null);
				}
			}
			// Redirect output to dev/null if not specified
			if (!output_flag) {
				int dev_null = open("/dev/null", O_WRONLY);
				if (dev_null == -1) {
					printf("cannot set /dev/null to output\n");
					fflush(stdout);
					exit(1);
				}
				else {
					int result = dup2(dev_null, 1);
					if (result == -1) {
						perror("dup2");
					}
					close(dev_null);
				}
			}
		}

		// Foreground commands
		if (!bg_flag) {
			// Register SIG_DFL to default SIGINT; Terminate process
			SIGINT_action.sa_handler = SIG_DFL;
			SIGINT_action.sa_flags = 0;
			sigaction(SIGINT, &SIGINT_action, NULL);
		}

		// Run the commands
		if (execvp(args[0], args)) {
			// exec only returns if there is an error
			perror(args[0]);
			fflush(stdout);
			exit(1);
		}
		break;

	default:
		// Parent process
		if (bg_flag) {
			// Don't wait for a command completion
			printf("background pid is %d\n", spawn_pid);
			fflush(stdout);

			// Add background process to background array
			bg_array[bg_count] = spawn_pid;
			bg_count++;

			// Reset background flag
			bg_flag = 0;
		}

		else {
			do {
				// Wait for foreground child processes
				pid_t child_pid = waitpid(spawn_pid, &status, 0);

				if (child_pid == -1) {
					perror("waitpid");
					exit(1);
				}
				// If child process is terminated by signal
				if (WIFSIGNALED(status)) {
					printf("terminated by signal %d\n", WTERMSIG(status));
					fflush(stdout);
				}
			} while (!WIFEXITED(status) && !WIFSIGNALED(status));
		}
		break;
	}
	return 0;
}

/* ************************************************************************** */
/* Function: run_command()                                                    */
/*           Run built in commands (exit, cd, status) and call run_others     */
/*                                                                            */
/*                                                                            */
/* ************************************************************************** */
void run_command(char** args) {

	// Ignore blank lines or comments begin with #
	if ((args[0] == NULL) || (strchr(args[0], '#'))) {
	}

	// exit command
	else if (strcmp(args[0], "exit") == 0) {
		// Kill all background processes
		for (int i = 0; i < bg_count; i++) {
			kill(bg_array[i], SIGTERM);
		}
		exit(0);
	}

	// cd command
	else if (strcmp(args[0], "cd") == 0) {
		if (args[1] == NULL) {
			// If no argument, move to HOME directory
			if (chdir(getenv("HOME")) != 0) {
				perror("chdir");
			}
		}
		else {
			// Move to the argument directory
			if (chdir(args[1]) != 0) {
				perror("chdir");
			}
		}
	}

	// status command
	else if (strcmp(args[0], "status") == 0) {

		// Normal termination; Return status the child passed to exit()
		if (WIFEXITED(status)) {
			printf("exit value %d\n", WEXITSTATUS(status));
		}
		// Abnormal termination; Return terminatation signal number
		else if (WIFSIGNALED(status)) {
			printf("terminated by signal %d\n", WTERMSIG(status));
		}
		fflush(stdout);
	}

	// Run any other commands
	else {
		run_others(args);
	}
}

/* ************************************************************************** */
/* Function: create_args()                                                    */
/*           Get tokens and create arguments array                            */
/*                                                                            */
/*                                                                            */
/* ************************************************************************** */
char** create_args(char* line) {

	// Set an argument array to hold tokens
	char* token;
	char** args_array = malloc(MAX_ARGS * sizeof(char*));

	int i = 0;

	// Tokenize the string
	token = strtok(line, " \n");
	while (token != NULL) {

		// Check for input redirection
		if (strcmp(token, "<") == 0) {

			// Set input flag and save filename
			input_flag = 1;
			input_file = strtok(NULL, " \n");
			token = strtok(NULL, " \n");
			// Add NULL to the end of array
			args_array[i] = NULL;
			i++;
			continue;
		}
		// Check for output redirection
		if (strcmp(token, ">") == 0) {

			// Set output flag and save filename
			output_flag = 1;
			output_file = strtok(NULL, " \n");
			token = strtok(NULL, " \n");
			args_array[i] = NULL;
			i++;
			continue;
		}
		// Check for background process
		if (strcmp(token, "&") == 0 && strtok(NULL, " \n") == NULL) {

			// Ignore & for built-in commands
			if (strcmp(args_array[0], "exit") == 0 || strcmp(args_array[0], "cd") == 0
				|| strcmp(args_array[0], "status") == 0) {
				bg_flag = 0;
			}
			else if (fg_flag) {
				bg_flag = 0;
			}
			else if (!fg_flag) {
				bg_flag = 1;
			}
			break;
		}
		// Add token to argument array
		args_array[i] = token;
		i++;

		// Get the next token
		token = strtok(NULL, " \n");
	}
	// Add NULL to the end of array
	args_array[i] = NULL;
	return args_array;
}

/* ************************************************************************** */
/* Function: replace_pid()                                                    */
/*           Search for $$ and replace it with pid                            */
/* Reference: stackoverflow.com/questions/44688310                            */
/*                                                                            */
/* ************************************************************************** */
char* replace_pid(char* line, const char* find, const char* pid_str) {

	int pid_len = strlen(pid_str);
	int find_len = strlen(find);

	// Set a buffer to hold revised string
	char buffer[MAX_LEN];
	char* buffer_ptr = buffer;
	char* line_ptr = line;

	while (1) {
		// Find $$ from line
		char* temp = strstr(line_ptr, find);

		// If no match, copy rest of the string
		if (temp == NULL) {
			strcpy(buffer_ptr, line_ptr);
			break;
		}

		// Copy substring before $$ and move pointer
		int substr_len = temp - line_ptr;
		memcpy(buffer_ptr, line_ptr, substr_len);
		buffer_ptr += temp - line_ptr;

		// Add pid and move pointer
		memcpy(buffer_ptr, pid_str, pid_len);
		buffer_ptr += pid_len;

		// Move line pointer
		line_ptr = temp + find_len;
	}
	strcpy(line, buffer);
	return line;
}

/* ************************************************************************** */
/* Function: get_command()                                                    */
/*           Prompt command line, get user input                              */
/*                                                                            */
/*                                                                            */
/* ************************************************************************** */
char* get_command() {

	char* user_input = NULL;
	size_t len = 0;
	ssize_t nread;

	// Prompt user for input 
	printf(": ");
	fflush(stdout);

	// Get line of input
	nread = getline(&user_input, &len, stdin);

	// Remove the newline char
	user_input[nread - 1] = '\0';

	// Search for $$ and replace with pid
	char pid_str[16];
	sprintf(pid_str, "%d", getpid());
	replace_pid(user_input, "$$", pid_str);

	return user_input;
}

/* ************************************************************************** */
/* Function: main()                                                           */
/* Reference: CS344 Exploration: Signal Handling API                          */
/*                                                                            */
/*                                                                            */
/* ************************************************************************** */
int main(int argc, char* argv[]) {

	// Ctrl-C; Ignore SIGINT signal
	SIGINT_action.sa_handler = SIG_IGN;
	sigaction(SIGINT, &SIGINT_action, NULL);

	// Ctrl-Z; Register SIGTSTP handler
	SIGTSTP_action.sa_handler = handler_SIGTSTP;
	// Block all catchable signals while handler_SIGTSTP is running
	sigfillset(&SIGTSTP_action.sa_mask);
	// Automatic restart to handle signal interrupt
	SIGTSTP_action.sa_flags = SA_RESTART;
	sigaction(SIGTSTP, &SIGTSTP_action, NULL);

	char* user_input;
	char** args;

	while (1) {
		// Check background process
		if (bg_count > 0) {
			check_bg_process();
		}

		// Reset variables
		input_flag = 0;
		output_flag = 0;
		input_file = NULL;
		output_file = NULL;

		// Prompt and handle user input
		user_input = get_command();
		args = create_args(user_input);
		run_command(args);
	}
	return 0;
}
