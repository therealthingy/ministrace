#ifndef COMMON_ERROR_H_
#define COMMON_ERROR_H_

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compiler.h"


/* - Error handling marcos - */
#ifndef NDEBUG
#  define LOG_DEBUG(FMT, ...) \
  do { \
    fprintf(stdout, "[DEBUG] `%s` (%s:%d): " FMT ".\n", __func__, __FILE__, __LINE__, ##__VA_ARGS__); \
  } while(0)
#else
#  define LOG_DEBUG(FMT, ...) do { } while(0)
#endif /* NDEBUG */

#define LOG_WARN(FMT, ...) \
  do { \
    fprintf(stderr, "[WARN] `%s` (%s:%d): " FMT ".\n", __func__, __FILE__, __LINE__, ##__VA_ARGS__); \
  } while(0)

#define LOG_ERROR_AND_DIE(FMT, ...) \
  do { \
    fprintf(stderr, "[ERROR] `%s` (%s:%d): " FMT ".\n", __func__, __FILE__, __LINE__, ##__VA_ARGS__); \
    exit(EXIT_FAILURE); \
  } while(0)


#define DIE_WHEN_ERRNO(FUNC) __extension__({ ({\
    __typeof__(FUNC) __val = (FUNC);\
    (BRANCH_UNLIKELY(-1 == __val) ? ({ LOG_ERROR_AND_DIE("%s", strerror(errno)); -1; }) : __val);\
  }); })

#define DIE_WHEN_ERRNO_VPTR(FUNC) __extension__({ ({\
    __typeof__(FUNC) __val = (FUNC);\
    (BRANCH_UNLIKELY(NULL == __val) ? ({ LOG_ERROR_AND_DIE("%s", strerror(errno)); (__typeof__(FUNC))NULL; }) : __val);\
  }); })

#endif /* COMMON_ERROR_H_ */
