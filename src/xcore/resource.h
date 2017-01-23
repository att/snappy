#ifndef SNPY_RESOURCE_H
#define SNPY_RESOURCE_H
#include <pthread.h>


struct snpy_res {
    int id;
    size_t disk;
    size_t ram;
};

#define TASK_LIMIT_NUM  64


struct snpy_res_mgr {
    int task_lim;
    int task_alloc;
    size_t disk_tot;
    size_t disk_use;
    size_t disk_alloc;
    size_t disk_free;
    size_t disk_avail;
    pthread_mutex_t lock;
};





#endif
