/*
 *  Copyright (c) 2016 AT&T Labs Research
 *  All rights reservered.
 *  
 *  Licensed under the GNU Lesser General Public License, version 2.1; you may
 *  not use this file except in compliance with the License. You may obtain a
 *  copy of the License at:
 *  
 *  https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt
 *  
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 *  WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 *  License for the specific language governing permissions and limitations
 *  under the License.
 *  
 *
 *  Author: Pingkai Liu (pingkai@research.att.com)
 */

#ifndef SNAPPY_UTIL_H
#define SNAPPY_UTIL_H 
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <stddef.h>
#include <stdint.h>

/* short alias for integer types */
typedef uint8_t u8;
typedef int8_t s8;
typedef uint32_t u32;
typedef int32_t s32;
typedef uint16_t u16;
typedef int16_t s16;
typedef uint64_t u64;
typedef int64_t s64;


#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MIN(x, y) (((x) < (y)) ? (x) : (y))

/**
 * BUILD_ASSERT - assert a build-time dependency.
 * @cond: the compile-time condition which must be true.
 *
 * Your compile will fail if the condition isn't true, or can't be evaluated
 * by the compiler.  This can only be used within a function.
 *
 * Example:
 *  #include <stddef.h>
 *  ...
 *  static char *foo_to_char(struct foo *foo)
 *  {
 *      // This code needs string to be at start of foo.
 *      BUILD_ASSERT(offsetof(struct foo, string) == 0);
 *      return (char *)foo;
 *  }
 */
#define BUILD_ASSERT(cond) \
    do { (void) sizeof(char [1 - 2*!(cond)]); } while(0)

/**
 * BUILD_ASSERT_OR_ZERO - assert a build-time dependency, as an expression.
 * @cond: the compile-time condition which must be true.
 *
 * Your compile will fail if the condition isn't true, or can't be evaluated
 * by the compiler.  This can be used in an expression: its value is "0".
 *
 * Example:
 *  #define foo_to_char(foo)                    \
 *       ((char *)(foo)                     \
 *        + BUILD_ASSERT_OR_ZERO(offsetof(struct foo, string) == 0))
 */
#define BUILD_ASSERT_OR_ZERO(cond) \
    (sizeof(char [1 - 2*!(cond)]) - 1)

/**
 * ARRAY_SIZE - get the number of elements in a visible array
 * @arr: the array whose size you want.
 *
 * This does not work on pointers, or arrays declared as [], or
 * function parameters.  With correct compiler support, such usage
 * will cause a build error (see build_assert).
 */
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]) + _array_size_chk(arr))

#if defined(__GNUC__)
/* Two gcc extensions.
 * &a[0] degrades to a pointer: a different type from an array */
#define _array_size_chk(arr)                        \
    BUILD_ASSERT_OR_ZERO(!__builtin_types_compatible_p(typeof(arr), \
                            typeof(&(arr)[0])))
#else
#define _array_size_chk(arr) 0
#endif

#define SNPY_PRINT_ARRAY(array, size, fmt, delim) do {  \
    int i;                                              \
    for (i = 0; i < size; i++) {                        \
        printf(fmt, array[i]);                          \
        if (i == size - 1) {                            \
            printf("\n");                               \
        } else {                                        \
            printf(delim);                              \
        }                                               \
    }                                                   \
} while (0)

int kv_get_sval(const char *key, char *val, int val_size, const char *wd);
int kv_get_ival(const char *key, int *val, const char *wd);
int kv_put_sval(const char *key, const char *val, int val_size, const char *wd);
int kv_put_bval(const char *key, const void *val, int val_size, const char *wd);
int kv_put_ival(const char *key, int val, const char *wd);

size_t strlcat(char *dst, const char *src, size_t siz);
size_t strlcpy(char *dst, const char *src, size_t siz);

int rmdir_recurs(const char *dir);
int mkdir_p(const char *dir_path, const mode_t mode) ;
int mkdir_argv(const char *fmt, ...) ;


int snpy_get_json_val(const char *buf, int buf_size,
                      const char *path,
                      void *val, int val_size);
ssize_t snpy_get_free_spc(const char *path);
ssize_t snpy_get_free_mem(void);
ssize_t snpy_get_loadavg(void);

enum {
    SNPY_LOG_NONE,
    SNPY_LOG_INFO,
    SNPY_LOG_WARN,
    SNPY_LOG_DEBUG,
    SNPY_LOG_ERR,
    SNPY_LOG_FATAL,
    SNPY_LOG_PANIC
};

int snpy_logger_open(const char *log_fn, int flag);
int snpy_logger(int priority, const char *fmt, ...);
void snpy_logger_close(int flag);
#endif

