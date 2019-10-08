#include "snpy_log.h"
#include "snpy_util.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>


int snpy_log_open(struct snpy_log *log, const char *log_fname, int flag) {
    int fd = open(log_fname, O_CREAT|O_WRONLY|O_TRUNC, 0600);
    if (fd < 0) 
        return -errno;

    log->fd = fd;
    pthread_mutex_init(&log->mutex, NULL);

    return 0;
}



int snpy_log(struct snpy_log *log, int priority, const char *fmt, ...) {

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

    int rc = vsnprintf(buf+len, sizeof buf - len - 1, fmt, ap);
    if (rc >= (sizeof buf) - len)
        return -EMSGSIZE;
    va_end(ap);

    len += rc;
    buf[len] = '\n'; len++; buf[len] = 0;
    pthread_mutex_lock(&log->mutex);
    write(log->fd, buf, len);
    pthread_mutex_unlock(&log->mutex);
    return 0;
}

void snpy_log_close(struct snpy_log *log) {
    close(log->fd);
}


