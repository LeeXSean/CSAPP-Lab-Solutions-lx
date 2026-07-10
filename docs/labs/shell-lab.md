---
title: Shell Lab
description: A job-control Unix shell — signals, process groups, and the fork/addjob race.
---

# Shell Lab · Building a Shell

<p class="article-meta">Processes & signals <span class="dot">·</span> Keywords: fork, signals, process groups, reaping <span class="dot">·</span> <a href="https://github.com/LeeXSean/CSAPP-Lab-Solutions-lx/blob/main/Shell_Lab/shlab-handout/tsh.c">tsh.c</a></p>

!!! success "Verified locally"
    All **16 traces** match `tshref` (PID-normalized) — `sdriver.pl -s ./tsh` vs `-s ./tshref`.

`tsh` is a small but complete Unix shell: it launches programs, tracks them as **jobs**, forwards keyboard signals, and supports `fg` / `bg` / `jobs` / `quit`. The code is short. The hard part is that it is **concurrent** — a `SIGCHLD` can fire between any two instructions, and the correctness of the whole thing rests on controlling exactly *when* that is allowed to happen.

!!! abstract "The assignment"
    Fill in seven functions on top of a provided read/eval loop and job-list library:

    - `eval` — parse a line; run a built-in, or `fork`/`execve` an external command;
    - `builtin_cmd`, `do_bgfg` — the built-ins `quit`, `jobs`, `fg`, `bg`;
    - `waitfg` — block until the foreground job leaves the foreground;
    - `sigchld_handler`, `sigint_handler`, `sigtstp_handler` — reap children and forward Ctrl-C / Ctrl-Z.

    Every job is one of three states, and at most **one** job is in the foreground at a time:

## Job states

``` text
   (at most one job is FG at any moment)

   FG  -- Ctrl-Z (SIGTSTP) -->  ST
   ST  -- fg -->  FG
   ST  -- bg -->  BG
   BG  -- fg -->  FG

   - a new command starts a job as FG, or as BG if it ends in '&'
   - when any job exits or is killed, it leaves the job list
```

Only a foreground job blocks the prompt; background and stopped jobs live on in the job list until they finish.

---

## eval: fork, exec, and the masking dance { data-toc-label="eval" }

For an external command, `eval` forks a child and `execve`s the program. The subtlety is entirely in the **signal mask** around those calls:

``` c
sigfillset(&mask_all);
sigemptyset(&mask_one);
sigaddset(&mask_one, SIGCHLD);

if (!builtin_cmd(argv)) {
    sigprocmask(SIG_BLOCK, &mask_one, &prev_one);   // (1)
    if ((pid = fork()) == 0) {                       // child
        setpgid(0, 0);                               // (2)
        sigprocmask(SIG_SETMASK, &prev_one, NULL);   // (3)
        if (execve(argv[0], argv, environ) < 0) {
            printf("%s: Command not found\n", argv[0]);
            exit(1);
        }
    }
    sigprocmask(SIG_BLOCK, &mask_all, NULL);         // (4)
    if (!bg) {
        addjob(jobs, pid, FG, cmdline);
        waitfg(pid);
    } else {
        addjob(jobs, pid, BG, cmdline);
        job_pos = getjobpid(jobs, pid);
        printf("[%d] (%d) %s", job_pos->jid, job_pos->pid, job_pos->cmdline);
    }
    sigprocmask(SIG_SETMASK, &prev_one, NULL);       // (5)
}
```

1.  Block `SIGCHLD` **before** forking, saving the old mask in `prev_one`. From here until step 5, the parent cannot be interrupted by a dying child.
2.  Put the child in its **own process group** (`setpgid(0,0)`). Now `kill(-pid, ...)` targets exactly this job — so Ctrl-C at the keyboard hits the foreground job's group, not the shell.
3.  The child restores the original mask before `execve`, so the new program starts with a clean, unblocked signal state.
4.  Parent blocks **all** signals while it touches the shared job list — the handler also does this, so the two never corrupt `jobs` concurrently.
5.  The two paths reach this restore differently. A **background** job restores `prev_one` immediately after insertion and printing. A **foreground** job first enters `waitfg`, where `sigsuspend` temporarily opens the controlled window for `SIGCHLD`; only after that job exits or stops does `eval` reach this line. Either way, `SIGCHLD` stays blocked across the entire `fork` → `addjob` window.

### The race this prevents

Why block `SIGCHLD` before `fork` rather than after `addjob`? Because a short-lived child can **die before the parent gets scheduled again**. If `SIGCHLD` were deliverable then, the handler would run `deletejob(pid)` for a job that `addjob` has not yet inserted — the delete is a no-op, and the job leaks into the list forever.

Blocking `SIGCHLD` until after `addjob` forces the only correct order — *insert, then reap*:

``` text
   parent (tsh)                              child / kernel
   -------------                             --------------
   block SIGCHLD
   fork()  ---------------------> child: setpgid; restore mask
                                  execve; exit -> SIGCHLD pending
   SIGCHLD remains blocked                              |
   block all signals; addjob(pid)                       |
   allow SIGCHLD only after insertion:                  |
     FG: waitfg -> sigsuspend(empty)                    |
     BG: restore previous mask                          |
        <--------------- deliver SIGCHLD ---------------+
   sigchld_handler -> deletejob  (job already registered: no leak)
```

## waitfg: sigsuspend, not a spin loop { data-toc-label="waitfg" }

A foreground job must block the prompt until it exits or is stopped. The tempting `while (fg) ;` busy-loop burns a core; `while (fg) pause();` has a fatal race (the child can exit between the `fg` test and `pause`, and then `pause` sleeps forever). The correct primitive is `sigsuspend`, which **atomically** installs a mask and waits:

``` c
void waitfg(pid_t pid) {
    sigset_t mask_all;
    sigemptyset(&mask_all);                                   // (1)
    while ((job_cur = getjobpid(jobs, pid)) != NULL && job_cur->state == FG) {
        sigsuspend(&mask_all);                                // (2)
    }
}
```

1.  An **empty** mask: while suspended, *all* signals — including `SIGCHLD` — are unblocked.
2.  `sigsuspend` sets the mask, sleeps until any signal arrives, then restores the previous (all-blocked) mask as one atomic step. When `SIGCHLD` reaps the foreground child, the handler flips its state; the loop condition then fails and `waitfg` returns.

    Recall `eval` had blocked everything before calling `waitfg` — so this empty-mask `sigsuspend` is precisely the controlled window in which the foreground child's `SIGCHLD` is allowed through.

## Reaping children: the SIGCHLD handler { data-toc-label="SIGCHLD handler" }

One `SIGCHLD` may stand for **several** children, because standard signals do not queue. So the handler reaps in a loop with `WNOHANG | WUNTRACED`, draining every child that is ready without ever blocking:

``` c
void sigchld_handler(int sig) {
    int olderrno = errno;                                      // (1)
    sigset_t mask_all, prev_all;
    pid_t pid; int status;
    sigfillset(&mask_all);

    while ((pid = waitpid(-1, &status, WUNTRACED | WNOHANG)) > 0) { // (2)
        sigprocmask(SIG_BLOCK, &mask_all, &prev_all);          // (3)
        if (WIFEXITED(status)) {
            deletejob(jobs, pid);                              // normal exit
        } else if (WIFSIGNALED(status)) {
            /* sio_* : async-signal-safe printing */           // (4)
            sio_puts("Job ["); sio_putl(pid2jid(pid));
            sio_puts("] ("); sio_putl(pid);
            sio_puts(") terminated by signal ");
            sio_putl(WTERMSIG(status)); sio_puts("\n");
            deletejob(jobs, pid);
        } else if (WIFSTOPPED(status)) {
            /* ... "stopped by signal ..." ... */
            getjobpid(jobs, pid)->state = ST;                  // stop, don't delete
        }
        sigprocmask(SIG_SETMASK, &prev_all, NULL);
    }
    errno = olderrno;                                          // (1)
}
```

1.  A handler can run at any point in `main`, so it **saves and restores `errno`** — otherwise a `waitpid` here could silently clobber the `errno` some interrupted library call was about to read.
2.  `WNOHANG` → return `0` immediately if no more children are ready (don't block the shell). `WUNTRACED` → also report children that just **stopped**, not only those that exited. The loop keeps going until every ready child is handled.
3.  Guard every job-list mutation by blocking all signals, mirroring `eval`.
4.  `printf` is **not** async-signal-safe. The handler uses the provided `sio_*` routines, which are thin wrappers over `write(2)` — safe to call from a signal handler.

The three `waitpid` outcomes map cleanly onto the job model: **exited** or **killed** → `deletejob`; **stopped** → mark `ST` and keep it.

## Forwarding the keyboard: SIGINT & SIGTSTP { data-toc-label="SIGINT / SIGTSTP" }

The shell itself receives Ctrl-C and Ctrl-Z. Its job is not to die, but to **relay** them to the foreground job's entire process group:

``` c
void sigint_handler(int sig) {
    int olderrno = errno;
    pid_t pid;
    if ((pid = fgpid(jobs)) > 0)   // is there a foreground job?
        kill(-pid, SIGINT);         // (1)
    errno = olderrno;
}
```

1.  The **negative** pid means "send to the whole process group `pid`." Because every job got its own group back in `eval` (`setpgid`), this reaches the foreground job and its children — and no one else. `sigtstp_handler` is identical but sends `SIGTSTP`. If there is no foreground job, both handlers do nothing.

## do_bgfg: resuming stopped jobs { data-toc-label="do_bgfg" }

`bg %n` and `fg %n` (or by PID) resume a stopped job by sending `SIGCONT` to its group; the only difference is whether the shell then waits:

``` c
if (!strcmp(argv[0], "bg")) {
    if (job_cur->state == ST) {
        job_cur->state = BG;
        kill(-(job_cur->pid), SIGCONT);   // resume in background
        printf("[%d] (%d) %s", job_cur->jid, job_cur->pid, job_cur->cmdline);
    }
} else { /* fg */
    if (job_cur->state == BG || job_cur->state == ST) {
        job_cur->state = FG;
        kill(-(job_cur->pid), SIGCONT);   // resume ...
        waitfg(job_cur->pid);             // ... and block until it leaves the foreground
    }
}
```

`do_bgfg` also parses the two argument forms — a job id introduced by `%` (`getjobjid`) or a bare PID (`getjobpid`) — reporting a clean error for anything else.

!!! note "The three rules a signal handler must obey"
    Everything above follows from three constraints that make signal code correct rather than merely working-on-my-machine:

    1. **Save and restore `errno`** — a handler must be transparent to the code it interrupts.
    2. **Call only async-signal-safe functions** — `write`/`sio_*`, never `printf`/`malloc`.
    3. **Block signals around shared-state changes** — the job-list *mutations* that race (`addjob` in `eval`, the handler's `deletejob` and state updates) run under a full signal mask, so `main` and the handler can't corrupt the list at the same instant. (Reads on other paths, like `do_bgfg`, aren't masked here — a rougher edge of this particular solution.)
