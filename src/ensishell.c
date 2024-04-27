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
struct cmdline *initialise_cmd(char *line);
void execute_cmd(struct cmdline *l);
void jobs();
void addBgProcess(int pid, char* command);


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
	execute_cmd(l);

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

void execute_cmd(struct cmdline *l) {
	// Création d'un processus fils pour exécuter les commandes
	int pid_child = fork(); 
	
	if (pid_child == 0) { // Processus fils qui exécute les commandes une par une 
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
				if (l->in) { // Entrée
					in = open(l->in, O_RDONLY);
					dup2(in, STDIN_FILENO);
					close(in);
				}
				if (l->out) { // Sortie
					out = open(l->out, O_WRONLY | O_CREAT | O_TRUNC, 0644);
					ftruncate(out, 0);
					dup2(out, STDOUT_FILENO);
					close(out);
				}

				// Redirection des pipes
				if (i != 0) { // If it's not the first command
					dup2(prevPipefd[0], STDIN_FILENO);
				}
				if (isNextPipe) { // If there is a next command (pipe) 
					dup2(pipefd[1], STDOUT_FILENO);
				}

				if (i!=0){
					close(prevPipefd[0]);
					close(prevPipefd[1]);
				}
				close(pipefd[0]); 
				if (!isNextPipe) {
					close(pipefd[1]);
				}
				if ( strcmp(cmd[0],"jobs") == 0 ){ // Fonctions builtin 
					jobs();
					exit(0);
				}
				execvp(cmd[0], cmd);
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

				int status;
				waitpid(pid_child_command, &status, 0); // Attente du processus fils
			}

		}
		exit(0);
	} else { // Processus père
		if (!l->bg){
			int status;
			waitpid(pid_child, &status, 0);
		} else {
			addBgProcess(pid_child, l->seq[0][0]);
		}

	}
}

// Historique des processus en arrière plan
typedef struct bgProcess {
	int pid; // PID du processus
	char* command; // Commande exécutée
	struct bgProcess *next; // Pointeur vers le prochain processus en arrière-plan
} bgProcess;

bgProcess *bgList = NULL;

void addBgProcess(int pid, char* command) {
	bgProcess *newProcess = malloc(sizeof(bgProcess));
	if (newProcess == 0){
		
		return;
	}
	newProcess->pid = pid;
	newProcess->command = strdup(command); // duplique la commande
	newProcess->next = bgList;
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
			free(current->command);
			free(current);
			return;
		}
		previous = current;
		current = current->next;
	}
}

// Fonction pour afficher les processus en arrière plan et retirer ceux qui sont terminés
void jobs() {
	bgProcess *next = bgList;
	if (next == NULL) {
		printf("No background process\n");
		return;
	}

	int status;
	while (next) {
		bgProcess *current = next;
		next = current->next;
		
        if (waitpid(current->pid, &status, WNOHANG) != 0) {

			printf("Process with PID %d exited with status %d\n", current->pid, WEXITSTATUS(status));
			removeBgProcess(current->pid);
        } else {
            printf("PID: %d, Command: %s\n", current->pid, current->command);
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


		l = initialise_cmd(line);
		execute_cmd(l);
	}

}
