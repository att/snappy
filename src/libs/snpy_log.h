#ifndef SNPY_LOG_H
#define SNPY_LOG_H


struct snpy_log {
    char fn[256];
    int fd;
    pthread_t writer_thr_id;
    pthread_mutex_t mutex;
    pthread_cont_t cond;
    char buf_a[8192];
    char buf_b[8192];
};

int snpy_log_open(struct snpy_log *log, const char* fn) {
    
    return 0;
}

#endif
