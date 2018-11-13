#ifndef SNPY_LOG_H
#define SNPY_LOG_H








struct snpy_log {
    char fn[1024];
    int fd;
    pthread_t writer_thr_id;
    pthread_mutex_t mutex;
    pthread_cont_t cond;
    char buf[8192];
};


int snpy_log_open(struct snpy_log *log, const char* fn) {
    

}

#endif
