#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>

#include "libParseArgs.h"
#include "libProcessControl.h"

/**
 * parallelDo -n NUM -o OUTPUT_DIR COMMAND_TEMPLATE ::: [ ARGUMENT_LIST ...]
 * build and execute shell command lines in parallel
 */

/**
 * create and return a newly malloced command from commandTemplate and argument
 * the new command replaces each occurrance of {} in commandTemplate with argument
 */
// null terminate
char *createCommand(char *commandTemplate, char *argument)
{
	char *command = NULL;

	command = malloc(strlen(commandTemplate) + 10 * strlen(argument) + 1);
	int i = 0;
	int k = 0;
	while (commandTemplate[i] != '\0')
	{
		if ((commandTemplate[i] == '{') && (commandTemplate[i + 1] == '}'))
		{
			int j = 0;
			while (argument[j] != '\0')
			{
				command[k + j] = argument[j];
				j++;
			}
			k += j;
			i += 2;
		}
		else
		{
			command[k] = commandTemplate[i];
			i += 1;
			k += 1;
		}
	}
	command[k] = '\0';
	return command;
}

typedef struct PROCESS_STRUCT
{
	int pid;
	int ifExited;
	int exitStatus;
	int status;
	char *command;
} PROCESS_STRUCT;

typedef struct PROCESS_CONTROL
{
	int numProcesses;
	int numRunning;
	int maxNumRunning;
	int numCompleted;
	PROCESS_STRUCT *process;
} PROCESS_CONTROL;

PROCESS_CONTROL processControl;

void printSummary()
{
	printf("%d %d %d\n", processControl.numProcesses, processControl.numCompleted, processControl.numRunning);
}
void printSummaryFull()
{
	printSummary();
	int i = 0, numPrinted = 0;
	while (numPrinted < processControl.numCompleted && i < processControl.numProcesses)
	{
		if (processControl.process[i].ifExited)
		{
			printf("%d %d %d %s\n",
				   processControl.process[i].pid,
				   processControl.process[i].ifExited,
				   processControl.process[i].exitStatus,
				   processControl.process[i].command);
			numPrinted++;
		}
		i++;
	}
}
/**
 * find the record for pid and update it based on status
 * status has information encoded in it, you will have to extract it
 */
void updateStatus(int pid, int status)
{
	int i = 0;
	while (pid != processControl.process[i].pid)
	{
		i++;
	}
	processControl.process[i].status = status;
	processControl.process[i].ifExited = WIFEXITED(status);
	processControl.process[i].exitStatus = WEXITSTATUS(status);
	// YOUR CODE GOES HERE
}

void handler(int signum)
{

	// YOUR CODE GOES HERE
	if (signum == SIGUSR1)
	{
		printSummary();
	}
	else if (signum == SIGUSR2)
	{
		printSummaryFull();
	}
}

void freeProcess()
{
	for (int i = 0; i < processControl.numProcesses; i++)
	{
		free((processControl.process[i]).command);
	}
	free(processControl.process);
}
/**
 * This function does the bulk of the work for parallelDo. This is called
 * after understanding the command line arguments. runParallel
 * uses pparams to generate the commands (createCommand),
 * forking, redirecting stdout and stderr, waiting for children, ...
 * Instead of passing around variables, we make use of globals pparams and
 * processControl.
 */

int runParallel()
{
	processControl.numRunning = 0;

	processControl.process = malloc(sizeof(PROCESS_STRUCT) * pparams.argumentListLen);

	if (processControl.process == NULL)
	{
		perror("memory error");
		exit(-1);
	}
	int i = 0;
	signal(SIGUSR1, &handler);
	signal(SIGUSR2, &handler);

	// the directory should be made by the open call but i did this as a precaution
	mkdir(pparams.outputDir, 0777);

	while (i < pparams.argumentListLen)
	{
		// this if block allows the processes to always be the max when possible, as we increment and decrease numrunning
		if (processControl.numRunning < pparams.maxNumRunning)
		{
			processControl.process[i].command = createCommand(pparams.commandTemplate, pparams.argumentList[i]);
			if ((processControl.process[i].pid = fork()) < 0)
			{
				perror("fork error");
				exit(-1);
			}
			else if (processControl.process[i].pid == 0)
			{
				int fd;
				char outname[128];
				char errname[128];
				pid_t pid = getpid();
				snprintf(outname, sizeof(outname), "%s/%i.stdout", pparams.outputDir, pid);
				snprintf(errname, sizeof(errname), "%s/%i.stderr", pparams.outputDir, pid);

				if ((fd = open(outname, O_CREAT | O_WRONLY, 0644)) == -1)
				{
					fprintf(stderr, "Could not open %s\n", outname);
					exit(-1);
				}
				dup2(fd, 1);
				close(fd);
				if ((fd = open(errname, O_CREAT | O_WRONLY, 0644)) == -1)
				{
					fprintf(stderr, "Could not open %s\n", errname);
					exit(-1);
				}
				dup2(fd, 2);
				close(fd);

				execl("/bin/bash", "bin/bash", "-c", processControl.process[i].command, (char *)NULL);
				perror("failed to execute command");
				exit(-1);
			}
			else
			{
				i++;
				processControl.numRunning++;
				processControl.numProcesses++;
			}
		}
		else
		{
			// here we stop creating children as the number is now the max running, and we wait for one of them to finish
			//  once one finishes we continue creating more children by decreasing num running by 1.
			int status;
			pid_t pid;
			pid = wait(&status);
			updateStatus(pid, status);
			processControl.numRunning--;
			processControl.numCompleted++;
		}
	}

	int status;
	pid_t pid;
	while (processControl.numRunning > 0)
	{
		pid = wait(&status);
		updateStatus(pid, status);
		processControl.numRunning--;
		processControl.numCompleted++;
	}

	printSummaryFull();
	freeProcess();
}
