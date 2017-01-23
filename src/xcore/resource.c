#include <errno.h>


#include "conf.h"
#include "resource.h"
#include "error.h"

#include "snpy_util.h"


static struct snpy_res_mgr res_mgr; 

static struct snpy_res snpy_res_pool[TASK_LIMIT_NUM];


void snpy_res_mgr_init(struct snpy_res_mgr *mgr) {

    if (!mgr)
        return;
    mgr->task_lim = TASK_LIMIT_NUM;
    mgr->task_alloc = 0;
    mgr->disk_tot = snpy_get_free_spc(conf_get_run());
    mgr->disk_use = 0;
    mgr->disk_alloc = 0;
    mgr->disk_free = mgr->disk_tot;
    mgr->disk_avail = mgr->disk_free;
    pthread_mutex_init(&mgr->lock, NULL);
    return;
}

int snpy_res_mgr_add(struct snpy_res *res) {
    int i = 0;
    int rc = 0;
    int status = 0;
    if (!(rc = pthread_mutex_lock(&res_mgr.lock))) {
        return -rc;
    }
    for (i = 0; i < ARRAY_SIZE(snpy_res_pool); i ++) {
        if (snpy_res_pool[i].id == 0) {
            /* TODO: */
            memcpy(&snpy_res_pool[i], res, sizeof *res);
            return 0;
        }
    }

unlock_reg_mgr_lock:
    pthread_mutex_unlock(&res_mgr.lock);
    return -SNPY_ERESPOOLFUL;
}
