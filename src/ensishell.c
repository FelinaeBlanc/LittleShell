/*****************************************************
 * Copyright Grégory Mounié 2008-2015                *
 *           Simon Nieuviarts 2002-2009              *
 * This code is distributed under the GLPv3 licence. *
 * Ce code est distribué sous la licence GPLv3+.     *
 *****************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>

#include "variante.h"
#include "readcmd.h"

#ifndef VARIANTE
#error "Variante non défini !!"
#endif

// Déclaration fonctions
struct bgProcess;
struct cmdline *initialise_cmd(char *line);
void execute_cmd(struct cmdline *l, char *line);
void jobs();
void addBgProcess(int pid, char *line);


/* Guile (1.8 and 2.0) is auto-detected by cmake */
/* To disable Scheme interpreter (Guile support), comment the
 * following lines.  You may also have to comment related pkg-config
 * lines in CMakeLists.txt.
 */

#if USE_GUILE == 1
#include <libguile.h>

int question6_executer(char *line)
{
	/* Question 6: Insert your code to execute the command line
	 * identically to the standard execution scheme:
	 * parsecmd, then fork+execvp, for a single command.
	 * pipe and i/o redirection are not required.
	 */

	struct cmdline* l = initialise_cmd(line);
	execute_cmd(l, line);

	return 0;
}

SCM executer_wrapper(SCM x)
{
        return scm_from_int(question6_executer(scm_to_locale_stringn(x, 0)));
}
#endif


void terminate(char *line) {
#if USE_GNU_READLINE == 1
	/* rl_clear_history() does not exist yet in centOS 6 */
	clear_history();
#endif
	if (line)
	  free(line);
	printf("exit\n");
	exit(0);
}


// Historique des processus en arrière plan
typedef struct bgProcess {
	int pid; // PID du processus
	struct timeval *start; // Temps de début d'exécution
	char * line; // Commande exécutée
	struct bgProcess *next; // Pointeur vers le prochain processus en arrière-plan
} bgProcess;

bgProcess *bgList = NULL;

void addBgProcess(int pid, char * line) {
	bgProcess *newProcess = malloc(sizeof(bgProcess));
	if (newProcess == 0){
		return;
	}
    newProcess->start = malloc(sizeof(struct timeval)); // Allouer de la mémoire pour start
    if (newProcess->start == 0){
        free(newProcess);
        return;
    }

	newProcess->pid = pid;
	newProcess->next = bgList;
	newProcess->line = strdup(line); // copie la commande
	gettimeofday(newProcess->start, NULL); // copy la date de début d'exécution

	bgList = newProcess;
}
void removeBgProcess(int pid) {
	bgProcess *current = bgList;
	bgProcess *previous = NULL;
	while (current) {
		if (current->pid == pid) {
			if (previous) {
				previous->next = current->next;
			} else {
				bgList = current->next;
			}
			free(current->start);
			free(current->line);
			free(current);
			return;
		}
		previous = current;
		current = current->next;
	}
}
bgProcess * getBgProcess(int pid){
	bgProcess *current = bgList;
	while (current) {
		if (current->pid == pid) {
			return current;
		}
		current = current->next;
	}
	return NULL;
}

// Fonction pour afficher les processus en arrière plan et retirer ceux qui sont terminés
void jobs() {

	bgProcess *next = bgList;
	if (next == NULL) {
		printf("No background process\n");
		return;
	}
	int status;
	while (next != NULL) {

		bgProcess *current = next;
		next = current->next;

        if (waitpid(current->pid, &status, WNOHANG) == 0) {
				printf("PID: %d Command: %s\n", current->pid, current->line);
        } else {
			printf("Process with PID %d exited with status %d\n", current->pid, WEXITSTATUS(status));
			removeBgProcess(current->pid);
		}

	}
}

struct cmdline *initialise_cmd(char *line) {
	/* parsecmd free line and set it up to 0 */
	struct cmdline* l = parsecmd(&line);

	/* If input stream closed, normal termination */
	if (!l) {
		return l;
	}
	
	if (l->err) {
		/* Syntax error, read another command */
		printf("error: %s\n", l->err);
		return l;
	}

	if (l->in) printf("in: %s\n", l->in);
	if (l->out) printf("out: %s\n", l->out);
	if (l->bg) printf("background (&)\n");

	/* Display each command of the pipe */
	for (int i=0; l->seq[i]!=0; i++) {
		char **cmd = l->seq[i];
		printf("seq[%d]: ", i);
					for (int j=0; cmd[j]!=0; j++) {
							printf("'%s' ", cmd[j]);
					}
		printf("\n");
	}

	return l;
}

// Calcul du temps d'exécution à la fin d'une commande line
void sigchld_handler(int) {

    int status;
    pid_t pid;
	struct timeval start, end;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
		bgProcess *process = getBgProcess(pid);
		if (process == NULL) {
			continue;
		}
		
		start = *(process->start);
        gettimeofday(&end, NULL);
        double elapsed = (end.tv_sec - start.tv_sec) + ((end.tv_usec - start.tv_usec)/1000000.0);
        printf("Process with PID %d exited with status %d. Elapsed time: %.6f seconds\n", pid, WEXITSTATUS(status), elapsed);
		removeBgProcess(pid);
    }
}

void execute_cmd(struct cmdline *l, char *line) {
	// Vérifie si on utilise une commande interne
	if (l->seq[0] && !strcmp(l->seq[0][0], "jobs")) {
		jobs();
		return;
	}

	// Création d'un processus fils pour exécuter les commandes
	int pid_child_cmd_line = fork(); 
	
	if (pid_child_cmd_line == 0) { // Processus fils qui exécute les commandes une par une 
		int pipefd[2];
		int prevPipefd[2];

		for (int i=0; l->seq[i]!=0; i++) {
			char **cmd = l->seq[i]; // Command to execute
			
			
			int isNextPipe = l->seq[i+1]!=0; // Si la commande suivante est un pipe
			pipe(pipefd);
			

			int pid_child_command = fork(); 
			if(pid_child_command == 0) { // Processus fils

				// Redirection de l'entrée et de la sortie
				int in, out;
				if (l->in && i == 0) { // Input redirection for the first command
					if( (in = open(l->in, O_RDONLY)) < 0) {
						perror("open");
						exit(1);
					}
					dup2(in, STDIN_FILENO);
					close(in);
				}
				if (l->out && !isNextPipe) { // Output redirection for the last command
					if ((out = open(l->out, O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0) {
						perror("open");
						exit(1);
					}
					ftruncate(out, 0);
					dup2(out, STDOUT_FILENO);
					close(out);
				}

				// Redirection des pipes
				if (i != 0) {
					dup2(prevPipefd[0], STDIN_FILENO);
				}
				if (isNextPipe) {
					dup2(pipefd[1], STDOUT_FILENO);
				}

				if (i!=0){
					close(prevPipefd[0]);
					close(prevPipefd[1]);
				}
				close(pipefd[0]);
				close(pipefd[1]);

				execvp(cmd[0], cmd);
			//	printf("erreur ???? \n");
				exit(1);
			} else { // Processus père
				close(prevPipefd[0]);
				close(prevPipefd[1]);

				prevPipefd[0] = pipefd[0];
				prevPipefd[1] = pipefd[1];

				if (!isNextPipe) {
					close(pipefd[0]);
					close(pipefd[1]);
				}

			}

		}

		// Attendre la fin des processus fils
		int status;
		while ((wait(&status)) > 0);
		exit(0); // Fin du processus de commande

	} else { // Processus père
		if (!l->bg){
			int status;
			waitpid(pid_child_cmd_line, &status, 0);
		} else {
			printf("add process %d \n", pid_child_cmd_line);
			addBgProcess(pid_child_cmd_line, line );
		}

	}
}


int main() {
        printf("Variante %d: %s\n", VARIANTE, VARIANTE_STRING);

#if USE_GUILE == 1
        scm_init_guile();
        /* register "executer" function in scheme */
        scm_c_define_gsubr("executer", 1, 0, 0, executer_wrapper);
#endif

	// Initialisation sigchld_handler
	struct sigaction sa;
	sa.sa_handler = sigchld_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	sigaction(SIGCHLD, &sa, 0);

	while (1) {
		struct cmdline *l;
		char *line=0;
		char *prompt = "ensishell>";

		/* Readline use some internal memory structure that
		   can not be cleaned at the end of the program. Thus
		   one memory leak per command seems unavoidable yet */
		line = readline(prompt);
		if (line == 0 || ! strncmp(line,"exit", 4)) {
			terminate(line);
		}

#if USE_GNU_READLINE == 1
		add_history(line);
#endif


#if USE_GUILE == 1
		/* The line is a scheme command */
		if (line[0] == '(') {
			char catchligne[strlen(line) + 256];
			sprintf(catchligne, "(catch #t (lambda () %s) (lambda (key . parameters) (display \"mauvaise expression/bug en scheme\n\")))", line);
			scm_eval_string(scm_from_locale_string(catchligne));
			free(line);
			continue;
		}
#endif

		char * lineCopy = strdup(line);
		l = initialise_cmd(line);
		execute_cmd(l, lineCopy);
		free(lineCopy);	
	}

}
