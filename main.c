/**************************************************************
 * File name: main.c
 * Author: Jennifer Briere
 * Assignment 3: smallsh
 * ***********************************************************/

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h> 

struct commandLine {
	char* comm_args[513];	// 1 command, 512 arguments
	char* inRedir;		// will hold filename of input redirection, if exists
	char* outRedir;		// will hold filename of output redirection, if exists
	int background;		// T/F (1/0) if to run in background (&)
};

// global variables
int fg_only = 0;

/*****************************************************************
* Description: Handler of SIGTSTP for parent process
* Ignores the request for background mode and treats all processes
* as foreground only
********************************************************************/
void handle_SIGTSTP(int signo) {
	if (fg_only == 0) {
		char* message = "\nEntering foreground-only mode (& is now ignored)\n";
		write(STDOUT_FILENO, message, 50);
		fg_only = 1;
	}
	else {
		char* message = "\nExiting foreground-only mode\n";
		write(STDOUT_FILENO, message, 30);
		fg_only = 0;
	}

}

/************************************************************************
* Description: Search a string and replace one set of characters with another.
* This is used for the variable expansion of $$ to the pid
* adapted from https://www.geeksforgeeks.org/c-program-replace-word-text-another-given-word/
*************************************************************************/
char* find_replace(char* input) {
	char* result;
	int i, cnt = 0;
	char old[] = "$$";
	char pid[6];
	int num = getpid();

	//make the pid into string using sprintf function
	sprintf(pid, "%d", num);

	int pidlen = strlen(pid);
	int oldlen = strlen(old);

	// Counting the number of times $$ occurs in the string 
	for (i = 0; input[i] != '\0'; i++) {
		if (strstr(&input[i], old) == &input[i]) {
			cnt++;

			// Jumping to index after the old word. 
			i += oldlen - 1;
		}
	}

	// Making new string of enough length 
	result = (char*)malloc(i + cnt * (pidlen - oldlen) + 1);

	i = 0;
	while (*input) {
		// compare the substring with the result 
		if (strstr(input, old) == input) {
			strcpy(&result[i], pid);
			i += pidlen;
			input += oldlen;
		}
		else
			result[i++] = *input++;
	}

	result[i] = '\0';
	return result;
}

/************************************************************
* Description: parses the input from the user into the
* commandLine struct, which separates into array with command
* and args, input, ouput, and background process directive
***********************************************************/
struct commandLine* parseCommand(char* input) {
	
	struct commandLine* currCommand = malloc(sizeof(struct commandLine));

	// for use with strtok_r
	char* saveptr;

	// the first token is the command
	char* token = strtok_r(input, " \n", &saveptr);
	currCommand->comm_args[0] = calloc(strlen(token) + 1, sizeof(char));
	strcpy(currCommand->comm_args[0], token);

	// the next tokens are the array of args
	// while next character/token is not < or >, token needs to be stored in arg array
	int i = 1;
	token = strtok_r(NULL, " \n", &saveptr);

	while ((token != NULL) && (strcmp(token, ">") != 0) && (strcmp(token, "<") != 0) && (strcmp(token, "&") != 0)) {
		currCommand->comm_args[i] = calloc(strlen(token) + 1, sizeof(char));
		strcpy(currCommand->comm_args[i], token);
		token = strtok_r(NULL, " \n", &saveptr);
		i++;
	}

	// if token is < or >, then the next values will
	// get assigned to inRedir or outRedir, accordingly
	while ((token != NULL) && (strcmp(token, "&") != 0)) {
		if (strcmp(token, ">") == 0) {

			token = strtok_r(NULL, " \n", &saveptr);
			currCommand->outRedir = calloc(strlen(token) + 1, sizeof(char));
			strcpy(currCommand->outRedir, token);
			token = strtok_r(NULL, " \n", &saveptr);

		}
		else {
			if (strcmp(token, "<") == 0) {
				token = strtok_r(NULL, " \n", &saveptr);
				currCommand->inRedir = calloc(strlen(token) + 1, sizeof(char));
				strcpy(currCommand->inRedir, token);
				token = strtok_r(NULL, " \n", &saveptr);
			}
		}
	}

	// the last token is if command to be run in background (&)
	if ((token != NULL) && strcmp(token, "&") == 0) {
		currCommand->background = 1;
	}

	return currCommand;

}

/************************************************************************
* Description: Print data for the given command
* ** used for testing **
*************************************************************************/
void printCommand(struct commandLine* aCommand)
{
	printf("command: %s, arg1: %s, arg2: %s, in: %s, out: %s,  bg: %d\n", aCommand->comm_args[0], aCommand->comm_args[1],
		aCommand->comm_args[2],
		aCommand->inRedir,
		aCommand->outRedir,
		aCommand->background);
}


/************************************************************************
* Description: Spawn child
* **adapted from 4_2_execv_fork_ls.c in the lectures
*************************************************************************/
int spawnChild(struct commandLine* userComm, int* childStatus, pid_t* bgKids, int* bgKidsCount) {

	// command is in userComm->comm_args[0]
	// argv[] already exists as userComm->comm_args[]


	// Fork a new process
	pid_t spawnPid = fork();
	int bgChildStatus;
	int fgChildStatus;

	// need to check for any > or < redirection first
	int targetIn = -5;
	int targetOut = -5;
	int i;

	switch (spawnPid) {
	case -1:
		perror("fork() failed!\n");
		exit(1);
		break;
	case 0:
		// In the child process

		// if user wants input redirection
		// adapted from https://repl.it/@cs344/54redirectc#main.c
		if (userComm->inRedir) {
			targetIn = open(userComm->inRedir, O_RDONLY);
			if (targetIn == -1) {
				printf("cannot open %s for input\n", userComm->inRedir);
				fflush(stdout);
				exit(1);
			}
		}
		else if (userComm->background) {
			// if user has not specified a redirect, and the process
			// is to run in the background redirect to /dev/null
			targetIn = open("/dev/null", O_RDONLY);
			if (targetIn == -1) {
				printf("cannot open /dev/null for input\n");
				fflush(stdout);
				exit(1);
			}
		}

		// if redirection has been done, implement with dup2()
		if (targetIn >= 0) {
			// Use dup2 to point FD 0 (stdin), i.e., standard input to targetIn
			int result = dup2(targetIn, 0);
			if (result == -1) {
				perror("dup2");
				exit(2);
			}
		}
		// Now whatever we write to standard in will be written to targetIn

		// if user wants output redirection
		// adapted from https://repl.it/@cs344/54redirectc#main.c
		if (userComm->outRedir) {
			targetOut = open(userComm->outRedir, O_WRONLY | O_CREAT | O_TRUNC, 0777);
			if (targetOut == -1) {
				printf("cannot open %s for output\n", userComm->outRedir);
				fflush(stdout);
				exit(1);
			}
		}
		else if (userComm->background) {
			// if user has not specified a redirect, and the process
			// is to run in the background redirect to /dev/null
			targetIn = open("/dev/null", O_WRONLY);
			if (targetOut == -1) {
				printf("cannot open /dev/null for input\n");
				fflush(stdout);
				exit(1);
			}
		}

		// if redirection has been done, implement with dup2()
		if (targetOut >= 0) {
			// Use dup2 to point FD 1 (stdout), i.e., standard output to targetOut
			int result = dup2(targetOut, 1);
			if (result == -1) {
				perror("dup2");
				exit(2);
			}
			// Now whatever we write to standard out will be written to targetOut
		}

		// all children should ignore SIGTSTP
		// The SIGTSTP_ignore struct as SIG_IGN as its signal handler
		// this will also pass through execvp
		struct sigaction SIGTSTP_ignore = { 0 };
		SIGTSTP_ignore.sa_handler = SIG_IGN;
		sigaction(SIGTSTP, &SIGTSTP_ignore, NULL);
		
		// SIGINT is being ignored inherently from the parent
		// but if this will be a foreground child, set SIGINT to terminate
		// otherwise, SIGINT will still be ignored by background child
		if (userComm->background != 1) {
			struct sigaction SIGINT_action = { 0 };
			SIGINT_action.sa_handler = SIG_DFL;
			sigaction(SIGINT, &SIGINT_action, NULL);
		}

		// Replace the current program with the command the user input
		// if a background process, need to print pid at beginning and end
		execvp(userComm->comm_args[0], userComm->comm_args);

		// close any files so they can be used later
		if (targetIn >= 0) {
			close(targetIn);
		}
		if (targetOut >= 0) {
			close(targetOut);
		}

		// exec only returns if there is an error
		perror(userComm->comm_args[0]);
		exit(1);
		break;
	default:
		// In the parent process

		// If this is a background process
		// If the child hasn't terminated, waitpid will immediately return with value 0
		if (userComm->background) {
			printf("background pid is %d\n", spawnPid);
			fflush(stdout);
			*bgKids = spawnPid;
			*bgKidsCount += 1;
			spawnPid = waitpid(spawnPid, &bgChildStatus, WNOHANG);
		}
		else {
			// If this is a foreground process
			// Wait for child's termination
			spawnPid = waitpid(spawnPid, childStatus, 0);

			// if foreground child terminated abnormally, parent needs
			// to print termination code - I couldn't get either of these
			// methods to work, just getting weird numbers returned
			//if (!WIFEXITED(childStatus)) {
			//		printf("terminated by signal %d\n", WTERMSIG(childStatus));
			//		fflush(stdout);
			//}

			//if (WIFSIGNALED(childStatus)) {
			//	fgChildStatus = WTERMSIG(childStatus);
			//	printf("terminated by signal %d\n", WTERMSIG(fgChildStatus));
			//	fflush(stdout);
			//}
		}
		break;
	}
}

/***************************************************************
* Description: The cd command changes the working directory of smallsh.
* By itself - with no arguments - it changes to the directory specified 
* in the HOME environment variable. This command can also take one
* argument : the path of a directory to change to.
****************************************************************/
int run_cd(char* chDir) {
	if (chDir == NULL) {
		char* home = getenv("HOME");
		chdir(home);
		char cwd[256];
		getcwd(cwd, sizeof(cwd));
	}
	else {
		char newDir[256];
		getcwd(newDir, sizeof(newDir));
		strcat(newDir, "/");
		strcat(newDir, chDir);
		chdir(newDir);
		char cwd[256];
		getcwd(cwd, sizeof(cwd));
	}

	return 0;

}

int main() {
	int leave = 0;
	char input[2048];
	int status = 0;
	int childStatus;
	pid_t bgKids[100];
	int bgKidsCount = 0;

	// parent process must ignore SIGINT
	// The ignore_action struct as SIG_IGN as its signal handler
	// this will also pass to the children
	struct sigaction ignore_SIGINT = { 0 }, SIGTSTP_action = { 0 };
	ignore_SIGINT.sa_handler = SIG_IGN;
	sigaction(SIGINT, &ignore_SIGINT, NULL);

	SIGTSTP_action.sa_handler = handle_SIGTSTP;
	SIGTSTP_action.sa_flags = 0;
	sigaction(SIGTSTP, &SIGTSTP_action, NULL);

	while (!leave) {
		if (bgKidsCount > 0) {
			for (int i = 0; i < bgKidsCount; i++) {
				if (bgKids[i] > 0) {
					// call waitpid/check status on all background children
					// if background child has completed, print message
					int bgStatus;
					pid_t kidDone = waitpid(bgKids[i], &bgStatus, WNOHANG);
					if (kidDone != 0) {
						if (WIFEXITED(bgStatus)) {
							printf("background pid %d is done: exit value %d\n", bgKids[i], WEXITSTATUS(bgStatus));
							fflush(stdout);
							bgKids[i] = -1;
						}
						else {
							printf("background pid %d is done: terminated by signal %d\n", bgKids[i], WTERMSIG(bgStatus));
							fflush(stdout);
							bgKids[i] = -1;
						}
					}
				}
			}
		}
		printf(": ");
		fflush(stdout);
		if (fgets(input, 2048, stdin)) {
			input[strcspn(input, "\n")] = 0;

			// if the first character is # or the line is blank, 
			// just prompt again
			if ((strlen(input) != 0) && (strncmp(input, "#", 1))) {

				// otherwise, expand any $$ to the pid
				char* expInput = find_replace(input);

				// then parse the expanded input into the commandLine struct
				struct commandLine* newCommand = parseCommand(expInput);

				// for testing
				// printCommand(newCommand);

				// parent process treats SIGTSTP as start/stop foreground only mode
				// toggle foreground mode on/off (using global variable) - if on...
				// ...input/command is already parsed, so just change value
				// of newCommand->background to 0, so not treated as bg process
				if (fg_only == 1) {
					newCommand->background = 0;
				}


				// check for built-in commands first
				// built-in exit
				if (!strcmp(newCommand->comm_args[0], "exit")) {
					// kill all the children first (eek!)
					// loop through bgKids[] and if any value > 0, kill it
					if (bgKidsCount > 0) {
						for (int i = 0; i < bgKidsCount; i++) {
							if (bgKids[i] > 0) {
								kill(bgKids[i], SIGTERM);
							}
						}
					}
					leave = 1;
				}

				// built-in cd
				else if (!strcmp(newCommand->comm_args[0], "cd")) {
					run_cd(newCommand->comm_args[1]);
				}

				//built-in status
				else if (!strcmp(newCommand->comm_args[0], "status")) {
					if (WIFEXITED(childStatus)) {
						status = WEXITSTATUS(childStatus);
						printf("exit value %d\n", status);
						fflush(stdout);
					}
					else {
						status = WTERMSIG(childStatus);
						printf("terminated by signal %d\n", status);
						fflush(stdout);
					}
					
				}
				else {
					// if not a built-in command then create a new process
					spawnChild(newCommand, &childStatus, &bgKids[bgKidsCount], &bgKidsCount);
				}

			}

		}

	}


	return 0;
}