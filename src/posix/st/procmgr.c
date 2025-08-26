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
static struct sigaction prev_childact;
static sigset_t childmsk;
static volatile sig_atomic_t children_ready = 0;


#define whileintr(C)	while ((C) == -1 && errno == EINTR)

static void childhandler (int signo, siginfo_t *info, void *context)
{
	children_ready++;

	if (prev_childact.sa_flags & SA_SIGINFO)
		prev_childact.sa_sigaction(signo, info, context);
	else if (prev_childact.sa_handler != SIG_DFL && prev_childact.sa_handler != SIG_IGN)
		prev_childact.sa_handler(signo);
}

void losiP_drainchildren (void)
{
	pid_t pid;
	int status;

	sig_atomic_t expected_exits = children_ready;
	if (expected_exits == 0) return;
	children_ready = 0;

	sig_atomic_t found_exits = 0;
	int still_running;
	int any_changed;
	do {
		if (!initialized)
			break;

		any_changed = 0;
		still_running = 0;
		for (size_t i = 0; i < proctab.capacity; ++i) {
			losi_Process *proc = proctab.table[i];
			while (proc) {
				losi_Process *next = proc->next;
				still_running++;
				do {
					pid = waitpid(proc->pid, &status, WNOHANG);
				} while (pid < 0 && errno == EINTR);
				if (pid > 0) {
					any_changed = 1;
					found_exits++;
					still_running--;
					losiP_delproctab(&proctab, proc);
					proc->pid = 0;
					proc->status = status;
					if (proc->pipe[0] != -1) {
						whileintr(write(proc->pipe[0], &proc, sizeof(proc)));
						whileintr(close(proc->pipe[0]));
					}
					if (found_exits >= expected_exits)
						return;
				}
				proc = next;
			}
		}
	} while (any_changed && still_running > 0);
}


int losiP_initprocmgr (losi_Alloc allocf, void *allocud)
{
	if (!initialized) {
		initialized = 1;
		/* setup process table */
		losiP_initproctab(&proctab, allocf ? allocf : defallocf,
		                             allocf ? allocud : NULL);
		/* setup signal action */
		childact.sa_sigaction = childhandler;
		sigemptyset(&childact.sa_mask);
		childact.sa_flags = SA_RESTART | SA_SIGINFO;
		sigaction(SIGCHLD, &childact, &prev_childact);
		/* setup signal block mask */
		sigemptyset(&childmsk);
		sigaddset(&childmsk, SIGCHLD);
		return 1;
	}
	return 0;
}

void losiP_freeprocmgr (void)
{
	if (initialized && proctab.table != proctab.mintab)
		proctab.allocf(proctab.allocud, proctab.table, proctab.capacity * sizeof *proctab.table, 0);
}

void losiP_lockprocmgr ()
{
	losiP_drainchildren();
	if (!losiP_emptyproctab(&proctab))
		sigprocmask(SIG_BLOCK, &childmsk, NULL);
}

void losiP_unlockprocmgr ()
{
	losiP_drainchildren();
	sigprocmask(SIG_UNBLOCK, &childmsk, NULL);
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
