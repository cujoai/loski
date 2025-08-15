#include "luaosi/procmgr.h"


#include "luaosi/proctab.h"

#include <errno.h>
#include <signal.h>
#include <wait.h>
#include <stdlib.h>
#include <unistd.h>


static void *defallocf (void *ud, void *ptr, size_t osize, size_t nsize) {
	(void)ud;  (void)osize;  /* not used */
	if (nsize == 0) {
		free(ptr);
		return NULL;
	}
	return realloc(ptr, nsize);
}


static volatile char initialized = 0;
static losi_ProcTable proctab;
static struct sigaction childact;
static sigset_t childmsk;


#define whileintr(C)	while ((C) == -1 && errno == EINTR)

static void childhandler (int signo)
{
	pid_t pid;
	int status;

	int any_changed;
	do {
		if (!initialized)
			break;

		any_changed = 0;
		for (size_t i = 0; i < proctab.capacity; ++i) {
			losi_Process *proc = proctab.table[i];
			while (proc) {
				losi_Process *next = proc->next;
				do {
					pid = waitpid(proc->pid, &status, WNOHANG);
				} while (pid < 0 && errno == EINTR);
				if (pid > 0) {
					any_changed = 1;
					losiP_delproctab(&proctab, proc);
					proc->pid = 0;
					proc->status = status;
					if (proc->pipe[0] != -1) {
						whileintr(write(proc->pipe[0], &proc, sizeof(proc)));
						whileintr(close(proc->pipe[0]));
					}
				}
				proc = next;
			}
		}
	} while (any_changed);
}


int losiP_initprocmgr (losi_Alloc allocf, void *allocud)
{
	if (!initialized) {
		initialized = 1;
		/* setup process table */
		losiP_initproctab(&proctab, allocf ? allocf : defallocf,
		                             allocf ? allocud : NULL);
		/* setup signal action */
		childact.sa_handler = SIG_DFL;
		sigemptyset(&childact.sa_mask);
		childact.sa_flags = 0;
		/* setup signal block mask */
		sigemptyset(&childmsk);
		sigaddset(&childmsk, SIGCHLD);
		return 1;
	}
	return 0;
}

void losiP_lockprocmgr ()
{
	if (!losiP_emptyproctab(&proctab))
		sigprocmask(SIG_BLOCK, &childmsk, NULL);
}

void losiP_unlockprocmgr ()
{
	void (*handler)(int);
	if (losiP_emptyproctab(&proctab)) handler = SIG_DFL;
	else handler = childhandler;
	if (handler != childact.sa_handler) {
		childact.sa_handler = handler;
		sigaction(SIGCHLD, &childact, NULL);
		if (handler == SIG_DFL) sigprocmask(SIG_UNBLOCK, &childmsk, NULL);
	} else if (handler == childhandler) {
		sigprocmask(SIG_UNBLOCK, &childmsk, NULL);
	}
}

int losiP_incprocmgr ()
{
	return losiP_incproctab(&proctab);
}

void losiP_putprocmgr (losi_Process *proc)
{
	losiP_putproctab(&proctab, proc);
}

void losiP_delprocmgr (losi_Process *proc)
{
	losiP_delproctab(&proctab, proc);
}
