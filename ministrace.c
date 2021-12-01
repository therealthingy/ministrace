#include <sys/ptrace.h>             /* ptrace, PTRACE_TRACEME, PTRACE_SETOPTIONS, PTRACE_O_TRACESYSGOOD, PTRACE_SYSCALL */
#include <bits/types.h>             /* eax, orig_eax, ebx, ecx, edx, esi, edi, ebp, rax, orig_rax, rdi, rsi, rdx, r10, r8, r9 */
#include <sys/user.h>               /* struct user, struct user_regs_struct regs */
#include <sys/wait.h>               /* waitpid, WIFSTOPPED, WSTOPSIG, WIFEXITED */
#include <sys/types.h>              /* kill, SIGSTOP, SIGTRAP */
#include <unistd.h>                 /* fork, pid_t, execvp, getpid */
#include <stdlib.h>                 /* NULL, abort, exit, strtol, malloc, realloc, free */
#include <stdio.h>                  /* perror, fprintf, stderr */
#include <errno.h>                  /* errno, ERANGE */
#include <string.h>                 /* strcmp, memcpy, memchr */
#include <stdbool.h>                /* bool, true, false */

#include <ctype.h>                  /* isprint */
#include <locale.h>                 /* setlocale, LC_ALL */

#include "syscalls.h"               /* ARG_INT, ARG_PTR, ARG_STR, syscall_entry */
#include "syscallents.h"            /* MAX_SYSCALL_NUM, syscalls; NOTE: May be generated by "gen_tables.py" */



/* -- Global Macros -- */
#define CLI_ARG_PAUSE_ON_SYSCALL_NR "--pause-snr"
#define CLI_ARG_PAUSE_ON_SYSCALL_NAME "--pause-sname"

/*
 * ELUCIDATION:
 *  - `ORIG_RAX` = Value of RAX BEFORE syscall (syscall nr)
 *  - `RAX`      = Return value of syscall
 */
#ifdef __amd64__
#  define REG_SYSCALL_NR orig_rax
#  define REG_SYSCALL_RTN_VAL rax
#else
#  define REG_SYSCALL_NR orig_eax
#  define REG_SYSCALL_RTN_VAL eax
#endif


#define PTRACE_TRAP_INDICATOR_BIT (1 << 7)



/* -- Function prototypes -- */
int run_parent_tracer(pid_t pid, int pause_on_syscall_nr);
int run_child_tracee(int argc, char **argv);

bool wait_for_syscall_or_check_child_exited(pid_t pid);
long __get_reg_content(pid_t pid, size_t off_user_struct);

#define offsetof(a, b) __builtin_offsetof(a, b)
#define get_reg_content(pid, reg_name) __get_reg_content(pid, offsetof(struct user, regs.reg_name))

void pause_until_user_input(void);

void print_syscall(pid_t pid, int syscall_nr);
const char *get_syscall_name(int syscall_nr);
void print_syscall_args(pid_t pid, int syscall_nr);

long get_syscall_arg(pid_t pid, int which);
char *read_string(pid_t pid, unsigned long addr);

void fprint_str_esc(FILE* restrict stream, char* str);




void usage(char **argv) {
    fprintf(stderr, "Usage: %s ["CLI_ARG_PAUSE_ON_SYSCALL_NR" <syscall nr>|"CLI_ARG_PAUSE_ON_SYSCALL_NAME" <syscall name>] <program> [<args> ...]\n", argv[0]);
    exit(1);
}

int main(int argc, char **argv) {
/* -- CLI args -- */
    if (argc < 2) {
        usage(argv);
    }

  /* - Check whether ONLY a specific syscall shall be traced - */
    int child_args_offset = 1;       /* executable itself */
    int pause_on_syscall_nr = -1;

    /* Passed using syscall nr */
    if (!strcmp(argv[1], CLI_ARG_PAUSE_ON_SYSCALL_NR)) {
        if (argc < 4) {              /* E.g., ministrace, -s, execve, whoami */
            usage(argv);
        }

        char *pause_on_syscall_nr_str = NULL;
        if (NULL != (pause_on_syscall_nr_str = argv[2])) {
            char *p_end_ptr = NULL;
            pause_on_syscall_nr = (int)strtol(pause_on_syscall_nr_str, &p_end_ptr, 10);
            if (pause_on_syscall_nr_str == p_end_ptr || ERANGE == errno) {
                fprintf(stderr, "Err: Couldn't parse value \"%s\" as number\n", pause_on_syscall_nr_str);
                return 1;
            }
        }

        if (pause_on_syscall_nr > MAX_SYSCALL_NUM || pause_on_syscall_nr < 0) {
            fprintf(stderr, "Err: %s is not a valid syscall\n", argv[2]);
            exit(1);
        }
        child_args_offset += 2;      /* "--pause-snr", "<int>" */

    /* Passed using syscall name; WARNING/ISSUE: x32 may have same name as x64 syscalls */
    } else if (!strcmp(argv[1], CLI_ARG_PAUSE_ON_SYSCALL_NAME)) {
        if (argc < 4) {              /* E.g., ministrace, -n, 59, whoami */
            usage(argv);
        }
        const char* const syscall_name = argv[2];

        for (int i = 0; i < SYSCALLS_ARR_SIZE; i++) {
            const syscall_entry* const ent = &syscalls[i];
            if (NULL != ent->name && !strcmp(syscall_name, ent->name)) {  /* NOTE: Syscall-nrs may be non-consecutive (i.e., array has empty slots) */
                pause_on_syscall_nr = i;
                break;
            }
        }

        if (-1 == pause_on_syscall_nr) {
            fprintf(stderr, "Err: Syscall w/ name \"%s\" doesn't exist\n", syscall_name);
            exit(1);
        }
        child_args_offset += 2;      /* E.g., "-n", "<name>" */
    }


/* (0.) Fork child (gets args passed) */
    pid_t pid;
    if ((pid = fork()) < 0) {
        perror("fork() failed");
        return 1;
    }

    return (!pid) ?
         (run_child_tracee(argc - child_args_offset, argv + child_args_offset)) :
         (run_parent_tracer(pid, pause_on_syscall_nr));
}



/* ----------------------------------------- ----------------------------------------- ----------------------------------------- ----------------------------------------- */
int run_parent_tracer(pid_t pid, int pause_on_syscall_nr) {
/* (0) Set ptrace options */
    int child_proc_status;
    if (waitpid(pid, &child_proc_status, 0) < 0) {   /* Wait for child (should stop itself by sending `SIGSTOP` to itself) */
        perror("`waitpid` failed");
        abort();
    }
    /*
     * ELUCIDATION:
     *  - `WIFSTOPPED`: Returns nonzero value if child process is stopped
     */
    if (!WIFSTOPPED(child_proc_status)) {
        fprintf(stderr, "Couldn't stop child process\n");
        abort();
    }
    /*
     * ELUCIDATION:
     *  - `PTRACE_O_TRACESYSGOOD`: When delivering syscall traps, set bit 7 in the signal number (i.e., deliver SIGTRAP|0x80) (see `PTRACE_TRAP_INDICATOR_BIT`)
     *    -> Makes it easier (for tracer) to distinguish b/w normal- & from syscalls caused traps
     */
    ptrace(PTRACE_SETOPTIONS, pid, 0, PTRACE_O_TRACESYSGOOD);

/* (1) Trace */
    while(1) {
    /* Syscall ENTER: Print syscall (based on retrieved syscall nr) */
        if (wait_for_syscall_or_check_child_exited(pid)) break;
        const long syscall_nr = get_reg_content(pid, REG_SYSCALL_NR);
        print_syscall(pid, syscall_nr);


    /* Stop (i.e., single step) if requested */
        if (pause_on_syscall_nr <= MAX_SYSCALL_NUM && syscall_nr == pause_on_syscall_nr) {
            pause_until_user_input();
        }


    /* Syscall EXIT (syscall return value) */
        if (wait_for_syscall_or_check_child_exited(pid)) break;
        const long syscall_rtn_val = get_reg_content(pid, REG_SYSCALL_RTN_VAL);
        fprintf(stderr, "%ld\n", syscall_rtn_val);
    }

    return 0;
}

int run_child_tracee(int argc, char **argv) {
/* exec setup: Create new array for argv of to be exec'd command */
    char *child_exec_argv[argc + 1 /* NULL terminator */];
    memcpy(child_exec_argv, argv, (argc * sizeof(argv[0])));
    child_exec_argv[argc] = NULL;
    const char* child_exec = child_exec_argv[0];

/* `PTRACE_TRACEME` starts tracing + causes next signal (sent to this process) to stop it & notify the parent (via `wait`), so that the parent knows to start tracing */
    ptrace(PTRACE_TRACEME);
/* Stop oneself so parent can set tracing option + Parent will see exec syscall */
    kill(getpid(), SIGSTOP);
/* Execute actual program */
    return execvp(child_exec, child_exec_argv);

/* Error handling (in case exec failed) */
    fprintf(stderr, "Couldn't exec \"%s\"\n", child_exec);
    abort();
}
/* ----------------------------------------- ----------------------------------------- ----------------------------------------- ----------------------------------------- */


/* -- Helper functions -- */
void pause_until_user_input(void) {
    do {
        char buf[2];
        fgets(buf, sizeof(buf), stdin); // wait until user presses enter to continue
    } while (0);
}


bool wait_for_syscall_or_check_child_exited(pid_t pid) {
    int child_last_and_next_sig_nr = 0;

    while (1) {
    /* (0) Set next breakpoint (on next syscall) & wait (i.e., block) until hit */
        /*
         * ELUCIDATION:
         *   - `PTRACE_SYSCALL`: Restarts stopped tracee (similar to `PTRACE_CONT`), but sets breakpoint at next syscall entry or exit
         *        (Tracee will, as usual, be stopped upon receipt of a signal)
         *        From the tracer's perspective, the tracee will appear to have been stopped by receipt of a `SIGTRAP`
         */
        ptrace(PTRACE_SYSCALL, pid, 0, child_last_and_next_sig_nr);               /* Set breakpoint on next syscall AND pass last sig nr to child */

        int child_proc_status;
        if (waitpid(pid, &child_proc_status, 0) < 0) {   /* Wait (i.e., block) until child changes state (e.g., hits breakpoint) */
            perror("`waitpid` failed");
            abort();
        }

    /* (1) Check child process's status */
        /*
         * ELUCIDATION - Process Completion Status:
         *  - `int WIFEXITED (int status)`: Returns nonzero value if child process terminated normally w/ `exit` or `_exit`
         *    - `int WEXITSTATUS (int status)`: Returns the low-order 8 bits of child's exit status value if `WIFEXITED` (passed in as `status` arg) is true
         *  - `int WIFSTOPPED (int status)`: Returns nonzero value if child is stopped
         *    - `int WSTOPSIG (int status)`: Returns signal number of signal that caused child to stop if `WIFSTOPPED` (passed in as `status` arg) is true
         *  - `int WIFSIGNALED (int status)`: Returns nonzero value if child terminated b/c it received a signal that wasn't handled
         *    - `int WTERMSIG (int status)`: Returns the signal number of the signal that terminated the child if `WIFSIGNALED` (passed in as `status` arg) is true
         *  - `int WCOREDUMP (int status)`: Returns nonzero value if child terminated and produced a core dump
         *  - `int WIFSTOPPED (int status)`: Returns nonzero value if child is stopped
         *    - `int WSTOPSIG (int status)`: Returns signal number of signal that caused child to stop if `WIFSTOPPED` (passed in as `status` arg) is true
         */
    /* -- Child has exited -> STOP tracer -- */
        if (WIFEXITED(child_proc_status)) {
            // fprintf(stderr, "DEBUG: Child exited w/ return code %d\n", WEXITSTATUS(child_proc_status));
            return true;       /* Child has `exit`ed */

        } else if (WIFSIGNALED(child_proc_status)) {
            // fprintf(stderr, "DEBUG: Child terminated w/ signal %d\n", WTERMSIG(child_proc_status));
            return true;

    /* -- Child was stopped -> Options: (a) signal (b) breakpoint was hit -- */
        } else if (WIFSTOPPED(child_proc_status)) {
            const int signal_nr = WSTOPSIG(child_proc_status);

            if (signal_nr & PTRACE_TRAP_INDICATOR_BIT) {
                return false;       /* Child was stopped (due to syscall breakpoint) -> extract syscall info */
            }

            /* Child was stopped by sent signal -> continue spinning ... */
            // fprintf(stderr, "DEBUG: [stopped %d (%x)]\n", child_proc_status, signal_nr);       // Stopped by signal which wasn't caused by breakpoint
            child_last_and_next_sig_nr = (signal_nr != SIGTRAP) ? (signal_nr) : (0);
        }
    }
}


long __get_reg_content(pid_t pid, size_t off_user_struct) {
    /*
     * ELUCIDATION:
     *  - `PTRACE_PEEKTEXT`, `PTRACE_PEEKDATA`: Reads & returns a word at the `addr`ess in tracee's memory
     *     (are equivalent since Linux doesn't have separate text- & data address spaces)
     *  - `PTRACE_PEEKUSER`: Read & return a word at offset `addr` (must be word-aligned) in the
     *     tracee's USER area (see <sys/user.h>), which holds the registers & other process information
     *  - `PTRACE_GETREGS`, `PTRACE_GETFPREGS`: Copy tracee's general-purpose or floating-point
     *     registers, respectively, to the address `data` in the tracer
     */
    long reg_val = ptrace(PTRACE_PEEKUSER, pid, off_user_struct);
    if (errno) {
        perror("ptrace(PTRACE_PEEKUSER, pid, off)");
        abort();
    }
    return reg_val;
}


void print_syscall(pid_t pid, int syscall_nr) {
    fprintf(stderr, "%s(", get_syscall_name(syscall_nr));
    print_syscall_args(pid, syscall_nr);
    fprintf(stderr, ") = ");
}

const char *get_syscall_name(int syscall_nr) {
    if (syscall_nr <= MAX_SYSCALL_NUM) {
        const syscall_entry* const ent = &syscalls[syscall_nr];
        if (ent->name) {
            return ent->name;
        }
    }

    static char fallback_generic_syscall_name[128];
    snprintf(fallback_generic_syscall_name, sizeof(fallback_generic_syscall_name), "sys_%d", syscall_nr);
    return fallback_generic_syscall_name;
}

void print_syscall_args(pid_t pid, int syscall_nr) {
    const syscall_entry* ent = NULL;
    int nargs = SYSCALL_MAX_ARGS;

    if (syscall_nr <= MAX_SYSCALL_NUM && syscalls[syscall_nr].name) {
        ent = &syscalls[syscall_nr];
        nargs = ent->nargs;
    }
    for (int arg_nr = 0; arg_nr < nargs; arg_nr++) {
        long arg = get_syscall_arg(pid, arg_nr);
        long type = ent ? ent->args[arg_nr] : ARG_PTR;      /* Default to `ARG_PTR` */
        switch (type) {
            case ARG_INT:
                fprintf(stderr, "%ld", arg);
                break;
            case ARG_STR: {
                char * strval = read_string(pid, arg);

                // fprintf(stderr, "\"%s\"", strval);
                fprintf(stderr, "\""); fprint_str_esc(stderr, strval); fprintf(stderr, "\"");

                free(strval);
                break;
            }
            default:    /* e.g., ARG_PTR */
                fprintf(stderr, "0x%lx", arg);
                break;
        }
        if (arg_nr != nargs -1)
            fprintf(stderr, ", ");
    }
}

/*
 * ELUCIDATION:
 *   Syscall args (up to 6) are passed on
 *      amd64 in rdi, rsi, rdx, r10, r8, and r9
 */
long get_syscall_arg(pid_t pid, int which) {
    switch (which) {
#ifdef __amd64__
        case 0: return get_reg_content(pid, rdi);
        case 1: return get_reg_content(pid, rsi);
        case 2: return get_reg_content(pid, rdx);
        case 3: return get_reg_content(pid, r10);
        case 4: return get_reg_content(pid, r8);
        case 5: return get_reg_content(pid, r9);
#else
        case 0: return get_reg_content(pid, ebx);
        case 1: return get_reg_content(pid, ecx);
        case 2: return get_reg_content(pid, edx);
        case 3: return get_reg_content(pid, esi);
        case 4: return get_reg_content(pid, edi);
        case 5: return get_reg_content(pid, ebp);
#endif
        default: return -1L;
    }
}


char *read_string(pid_t pid, unsigned long addr) {
    size_t read_str_size_bytes = 2048;
    char *read_str;
/* Allocate memory as buffer for string to be read */
    if (!(read_str = malloc(read_str_size_bytes))) {
        fprintf(stderr, "malloc: Failed to allocate %zu bytes\n", read_str_size_bytes);
        abort();
    }
    size_t read_bytes = 0;
    unsigned long ptrace_read_word;
    while (1) {
    /* Increase buffer size of too small */
        if (read_bytes + sizeof(ptrace_read_word) > read_str_size_bytes) {
            read_str_size_bytes *= 2;
            if (!(read_str = realloc(read_str, read_str_size_bytes))) {
                fprintf(stderr, "realloc: Failed to allocate %zu bytes\n", read_str_size_bytes);
                abort();
            }
        }
    /* Read from tracee (each time one word) */
        ptrace_read_word = ptrace(PTRACE_PEEKDATA, pid, addr + read_bytes);
        if (errno) {
            read_str[read_bytes] = '\0';
            break;
        }
    /* Append read word to buffer */
        memcpy(read_str + read_bytes, &ptrace_read_word, sizeof(ptrace_read_word));
    /* Read end of string ? */
        if (memchr(&ptrace_read_word, 0, sizeof(ptrace_read_word)) != NULL)
            break;
    /* Update read_bytes counter */
        read_bytes += sizeof(ptrace_read_word);
    }
    return read_str;
}


/*
 * Prints ASCII control chars in `str` using a hex representation
 */
void fprint_str_esc(FILE* stream, char* str) {
    setlocale(LC_ALL, "C");

    for (int i = 0; '\0' != str[i]; i++) {
        const char c = str[i];
        if (isprint(c) && c != '\\') {
            fputc(c, stream);
        } else {
            fprintf(stream, "\\x%02x", c);
        }
    }
}
