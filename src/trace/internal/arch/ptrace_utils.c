#include <elf.h>
#include <errno.h>
#include <sys/ptrace.h>
#include <sys/uio.h>

#include <common/error.h>
#include "../ptrace_utils.h"
#include "ptrace_utils.h"


/* -- Functions -- */
/* ----------------------- ----------------------- i386 / amd64 ----------------------- ----------------------- */
#if defined(__i386__) || defined(__x86_64__)

int ptrace_get_regs_content(pid_t tid, struct user_regs_struct_full *regs) {
    struct iovec iov = {
            .iov_base = regs,
            .iov_len = sizeof(struct user_regs_struct_full),
    };

    errno = 0;
    ptrace(PTRACE_GETREGSET, tid, NT_PRSTATUS, &iov);
    if (errno) {
        if (ESRCH == errno) { return -1; }
        LOG_ERROR_AND_DIE("Reading registers failed -- %s", strerror(errno));
    }

    return 0;
}


// /* ----------------------- -----------------------   arm64    ----------------------- ----------------------- */
// #elif defined(__aarch64__)
//
// int ptrace_get_regs_content(pid_t tid, struct user_regs_struct_full *regs) {
//   struct iovec iov = {
//     .iov_base = &(regs->user_regs),
//     .iov_len = sizeof(regs->user_regs),
//   };
//
//   /* 1. Get reg contents */
//   errno = 0;
//   ptrace(PTRACE_GETREGSET, tid, NT_PRSTATUS, &iov);
//   if (errno) {
//     if (ESRCH == errno) { return -1; }
//     LOG_ERROR_AND_DIE("Reading registers failed -- %s", strerror(errno));
//   }
//
//   /* 2. Get syscall-nr */
//   iov.iov_base = &(regs->syscallno);
//   iov.iov_len = sizeof(regs->syscallno);
//   ptrace(PTRACE_GETREGSET, tid, NT_ARM_SYSTEM_CALL, &iov);        // !!! TODO: Returns wrong syscall nr ?? !!!
//   if (errno) {
//     if (ESRCH == errno) { return -1; }
//     LOG_ERROR_AND_DIE("Reading registers failed -- %s", strerror(errno));
//   }
//
//   return 0;
// }


#else

#  error "Unsupported CPU arch"

#endif
