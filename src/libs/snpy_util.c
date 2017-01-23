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

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/statvfs.h>
#include <fcntl.h>
#include <fts.h>
#include <unistd.h>
#include <pthread.h>

#include "snpy_util.h"
#include "json.h"

/* mempcpy() - same as GNU mempcpy function
*/
void *
mempcpy (void *dest, const void *src, size_t n)
{
  return (char *) memcpy (dest, src, n) + n;
}

int kv_get_sval(const char *key, char *val, int val_size, const char *wd) 
{
    int fd;
    int status = 0;
    struct stat sb;

    char key_path[PATH_MAX];
    if (!wd) {
        strncpy(key_path, key, sizeof key_path);
    } else if (wd && 
               snprintf(key_path, sizeof key_path, "%s/%s", wd, key) 
               >= sizeof key_path) {
        return -ENAMETOOLONG;
    }

    if ((fd = open(key_path, O_RDONLY)) == -1)
        return -errno;
    if (fstat(fd, &sb)) {
        status = errno;
        goto close_fd;
    }
    if (sb.st_size >= val_size) {
        status = ERANGE;
        goto close_fd;
    }

    int nbyte = read(fd, val, sb.st_size);
    if (nbyte != sb.st_size) {
        status = errno;
        goto close_fd;
    }
    val[nbyte] = 0;
    status = 0;

close_fd:
    close(fd);
    return -status;
}


int kv_get_ival(const char *key, int *val, const char *wd) {
    char val_buf[32]="";
    int rc;
    rc = kv_get_sval(key, val_buf, sizeof val_buf, wd);

    if (rc) 
        return rc;
    char *endptr;
    long int tmp;
    errno = 0;
    tmp = strtol(val_buf, &endptr, 0);
    if (((tmp == LONG_MIN || tmp == LONG_MAX) && errno == ERANGE) || 
        (tmp == 0 && errno != 0))  {
        return -errno;
    }
    if (*endptr) 
        return -EINVAL;
    *val = tmp;
    return 0;
}

int kv_put_sval(const char *key, const char *val, int val_size, const char *wd) 
{
    int status = 0;
    int val_len = strnlen(val, val_size);
    int fd;
    char key_path[PATH_MAX] = "";

    if (!key || !val) 
        return -EINVAL;

    if (!wd) {
        strncpy(key_path, key, sizeof key_path);
    } else if (wd && 
        snprintf(key_path, sizeof key_path, "%s/%s", wd, key) 
        >= sizeof key_path) {
        return -ENAMETOOLONG;
    }
    
    if (val_len >= val_size) 
        return -ERANGE;
    
    if ((fd = open(key_path, O_WRONLY|O_CREAT|O_TRUNC, 0600)) < 0) 
        return -errno;
    
    if (write(fd, val, val_len) != val_len) {
        close(fd);
        return -errno;
    }
        
    close(fd);
    return 0;
}

int kv_put_bval(const char *key, 
                const void *val, int val_size, 
                const char *wd) 
{
    int status = 0;
    int fd;
    char key_path[PATH_MAX] = ""; 

    if (!key || !val) 
        return -EINVAL;

    if (!wd) {
        strncpy(key_path, key, sizeof key_path);
    } else if (wd && 
               snprintf(key_path, sizeof key_path, "%s/%s", wd, key) 
               >= sizeof key_path) {
        return -ENAMETOOLONG;
    }
    
    if ((fd = open(key_path, O_WRONLY|O_CREAT|O_TRUNC, 0600)) == -1) 
        return -errno;
    
    if (write(fd, val, val_size) != val_size) {
        close(fd);
        return -errno;
    }
    close(fd);
    return 0;
}



int kv_put_ival(const char *key, int val, const char *wd) {
    char val_buf[32] = "";
    snprintf(val_buf, sizeof val_buf, "%d", val);
    return kv_put_sval(key, val_buf, sizeof val_buf, wd);
}


#if !defined(HAVE_STRLCPY)
/*
 * Appends src to string dst of size siz (unlike strncat, siz is the
 * full size of dst, not space left).  At most siz-1 characters
 * will be copied.  Always NUL terminates (unless siz <= strlen(dst)).
 * Returns strlen(src) + MIN(siz, strlen(initial dst)).
 * If retval >= siz, truncation occurred.
 */
size_t
strlcat(char *dst, const char *src, size_t siz)
{
    register char *d = dst;
    register const char *s = src;
    register size_t n = siz;
    size_t dlen;

    /* Find the end of dst and adjust bytes left but don't go past end */
    while (n-- != 0 && *d != '\0')
        d++;
    dlen = d - dst;
    n = siz - dlen;

    if (n == 0)
        return(dlen + strlen(s));
    while (*s != '\0') {
        if (n != 1) {
            *d++ = *s;
            n--;
        }
        s++;
    }
    *d = '\0';

    return(dlen + (s - src));   /* count does not include NUL */
}
#endif

#if !defined(HAVE_STRLCAT)
/*
 * Copy src to string dst of size siz.  At most siz-1 characters
 * will be copied.  Always NUL terminates (unless siz == 0).
 * Returns strlen(src); if retval >= siz, truncation occurred.
 */
size_t
strlcpy(char *dst, const char *src, size_t siz)
{
    register char *d = dst;
    register const char *s = src;
    register size_t n = siz;

    /* Copy as many bytes as will fit */
    if (n != 0 && --n != 0) {
        do {
            if ((*d++ = *s++) == 0)
                break;
        } while (--n != 0);
    }

    /* Not enough room in dst, add NUL and traverse rest of src */
    if (n == 0) {
        if (siz != 0)
            *d = '\0';      /* NUL-terminate dst */
        while (*s++)
            ;
    }

    return(s - src - 1);    /* count does not include NUL */
}
#endif

int rmdir_recurs(const char *dir)
{
    int ret = 0;
    FTS *ftsp = NULL;
    FTSENT *curr;

    // Cast needed (in C) because fts_open() takes a "char * const *", instead
    // of a "const char * const *", which is only allowed in C++. fts_open()
    // does not modify the argument.
    char *files[] = { (char *) dir, NULL };

    // FTS_NOCHDIR  - Avoid changing cwd, which could cause unexpected behavior
    //                in multithreaded programs
    // FTS_PHYSICAL - Don't follow symlinks. Prevents deletion of files outside
    //                of the specified directory
    // FTS_XDEV     - Don't cross filesystem boundaries
    ftsp = fts_open(files, FTS_NOCHDIR | FTS_PHYSICAL | FTS_XDEV, NULL);
    if (!ftsp) {
        ret = -errno;
        goto finish;
    }

    while ((curr = fts_read(ftsp))) {
        switch (curr->fts_info) {
        case FTS_NS:
        case FTS_DNR:
        case FTS_ERR:
            ret = -curr->fts_errno;
            break;

        case FTS_DC:
        case FTS_DOT:
        case FTS_NSOK:
            // Not reached unless FTS_LOGICAL, FTS_SEEDOT, or FTS_NOSTAT were
            // passed to fts_open()
            break;

        case FTS_D:
            // Do nothing. Need depth-first search, so directories are deleted
            // in FTS_DP
            break;

        case FTS_DP:
        case FTS_F:
        case FTS_SL:
        case FTS_SLNONE:
        case FTS_DEFAULT:
            if (remove(curr->fts_accpath) < 0) {
                ret = -errno;
            }
            break;
        }
    }

finish:
    if (ftsp) {
        fts_close(ftsp);
    }

    return ret;
}



/* recursive mkdir */
int mkdir_p(const char *dir, const mode_t mode) {
    char tmp[PATH_MAX];
    char *p = NULL;
    struct stat sb;
    size_t len;

    /* copy path */
    strncpy(tmp, dir, sizeof(tmp));
    len = strlen(tmp);
    if (len >= sizeof(tmp)) {
        return -ENAMETOOLONG;
    }

    /* remove trailing slash */
    if(tmp[len - 1] == '/') {
        tmp[len - 1] = 0;
    }

    /* recursive mkdir */
    for(p = tmp + 1; *p; p++) {
        if(*p == '/') {
            *p = 0;
            /* test path */
            if (stat(tmp, &sb) != 0) {
                /* path does not exist - create directory */
                if (mkdir(tmp, mode) < 0) {
                    return -errno;
                }
            } else if (!S_ISDIR(sb.st_mode)) {
                /* not a directory */
                return -EINVAL;
            }
            *p = '/';
        }
    }
    /* test path */
    if (stat(tmp, &sb) != 0) {
        /* path does not exist - create directory */
        if (mkdir(tmp, mode) < 0) {
            return -errno;
        }
    } else if (!S_ISDIR(sb.st_mode)) {
        /* not a directory */
        return -EINVAL;
    }
    return 0;
}

int mkdir_argv(const char *fmt, ...) {
    char buf[PATH_MAX] = "";
    int rc;
    va_list ap;

    va_start(ap, fmt);
    rc = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    
    if (rc < 0 || rc == sizeof buf) 
        return -ENAMETOOLONG;
    
    if ((rc = mkdir(buf, 0600)))
        return -errno;

    return 0;
}



int snpy_get_json_val(const char *buf, int buf_size,
                      const char *path,
                      void *val, int val_size) {
    if (!buf || !path || !val)
        return -EINVAL;

    int i;
    int status = 0;
    struct jsonxs xs;
    struct json *js = json_open(JSON_F_NONE, &status);

    if (!js)
        return -status;

    int rc = json_loadstring(js, buf);
    if (rc) {
        status = rc;
        goto close_js;
    }

    if (!json_exists(js, path)) {
        status = EINVAL;
        goto close_js;
    }

    if (json_type(js, path) == JSON_T_STRING) {
        strlcpy(val, json_string(js, path), val_size);
    } else if (json_type(js,path) == JSON_T_NUMBER) {
        if (val_size == sizeof(double))
            *(double *)val = json_number(js, path);
        else
            status = EINVAL;
    } else {
        status = EINVAL;
    }

 close_js:
    json_close(js);
    return -status;
    
}

static const char *snpy_logger_pri_strlist[] = 
{
    "NONE",
    "INFO",
    "WARN",
    "DEBUG",
    "ERROR",
    "FATAL",
    "PANIC"
};


struct snpy_logger {
    int fd;
    pthread_mutex_t lock;
};

static struct snpy_logger logger = 
{   
    .fd = 1,
    .lock = PTHREAD_MUTEX_INITIALIZER,
};

int snpy_logger_open(const char *log_fn, int flag) {
    int fd = open(log_fn, O_CREAT|O_WRONLY|O_TRUNC, 0600);
    if (fd < 0) 
        return errno;
    logger.fd = fd;
    return 0;
}

int snpy_logger(int priority, const char *fmt, ...) {

    char buf[4096]="";
    if (priority < SNPY_LOG_NONE || priority > SNPY_LOG_PANIC)
        return -EINVAL;
    
    struct timeval tv;
    gettimeofday(&tv, NULL);
    int len = snprintf(buf, sizeof buf,
                       "[%lld.%lld] %s ", 
                       (long long)tv.tv_sec, (long long)tv.tv_usec, 
                       snpy_logger_pri_strlist[priority]);
    va_list ap;

    va_start(ap, fmt);

    int rc = vsnprintf (buf+len, sizeof buf - len - 1, fmt, ap);
    if (rc >= (sizeof buf) - len)
        return -EMSGSIZE;
    va_end(ap);

    len += rc;
    buf[len] = '\n'; len++; buf[len] = 0;

    write(logger.fd, buf, len);
    return 0;
}

void snpy_logger_close(int flag) {
    close(logger.fd);
}


ssize_t snpy_get_free_spc(const char *path) {
    struct statvfs sv;
    if (statvfs(path, &sv) == -1) {
        return -errno;
    }

    return sv.f_bsize * sv.f_bfree;
}

ssize_t snpy_get_free_mem(void) {
    long page_size = sysconf(_SC_PAGESIZE);
    long tot_pages = sysconf(_SC_PHYS_PAGES);
    long avail_pages = sysconf(_SC_AVPHYS_PAGES);

    return page_size * avail_pages;
}
