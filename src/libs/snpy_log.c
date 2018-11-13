#include "snpy_log.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>


static void* log_writer(void *arg) {
    struct snpy_log *log = arg;
    
    while (1) {
        pthread_mutex_lock(&log->m);
        
        usleep(2000);
    }
    return;
}

struct snpy_log* snpy_log_open(const char *log_fn, int flag) {
    struct snpy_log * p = malloc(sizeof *p);
    if (!p) 
        return -ENOMEM;
    
    int fd = open(log_fn, O_CREAT|O_WRONLY|O_TRUNC, 0600);
    if (fd < 0) {
        free(p);
        return errno;
    }
    logger.fd = fd;

    int r = pthread_create(&p->writer_thr_id, NULL, log_writer, p);
    if (r) {
        close(fd);
        free(p);
        return r;
    }
    
    return 0;
}



int snpy_log(int priority, const char *fmt, ...) {

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

    write(logger.fd, buf, len);
    return 0;
}

void snpy_log_close(int flag) {
    close(logger.fd);
}


