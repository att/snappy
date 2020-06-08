#ifndef SNPY_LOG_H
#define SNPY_LOG_H

#include <pthread.h>

struct snpy_log {
    int fd;
    pthread_mutex_t mutex;
};

int snpy_log_open(struct snpy_log *log, const char* fn, int flag);
void snpy_log_setfd(struct snpy_log *log, int fd);
    
int snpy_log(struct snpy_log *log, int priority, const char *fmt, ...);
void snpy_log_close(struct snpy_log *log);

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

enum {
    SNPY_LOG_NONE,
    SNPY_LOG_INFO,
    SNPY_LOG_WARN,
    SNPY_LOG_DEBUG,
    SNPY_LOG_ERR,
    SNPY_LOG_FATAL,
    SNPY_LOG_PANIC
};


#endif
