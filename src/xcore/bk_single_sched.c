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

#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#include <errno.h>

#include "snappy.h"
#include "db.h"
#include "arg.h"
#include "log.h"
#include "job.h"
#include "snpy_util.h"
#include "json.h"

#include "bk_single_sched.h"

struct bk_single_sched_conf {
    time_t sched_time;
    time_t full_bk_intvl;
    time_t incr_bk_intvl;
    int count;
};

static int proc_created(MYSQL *db_conn, const snpy_job_t *job);
static int proc_done(MYSQL *db_conn, snpy_job_t *job);
static int proc_ready(MYSQL *db_conn, snpy_job_t *job);
static int proc_blocked(MYSQL *db_conn, snpy_job_t *job);
static int proc_term(MYSQL *db_conn, snpy_job_t *job);

static int sched_conf_init(struct bk_single_sched_conf *conf, 
                           const char *arg, int arg_size);
static int do_sched(struct bk_single_sched_conf *conf);

/* sched_arg_init() - get the sched parameter from arg 
 *
 *
 * return: 0 - success, 1 - error
 */
static int sched_conf_init(struct bk_single_sched_conf *conf,
                          const char *arg, int arg_size) {
    int status = 0;
    if (!conf || !arg)
        return -EINVAL;
    int error;
    struct json *js = json_open(JSON_F_NONE, &error);
    if (!js)
        return -SNPY_EARG;
    int rc = json_loadstring(js, arg);
    if (rc) {
        status = SNPY_EARG;
        goto close_js;
    }
    memset(conf, 0, sizeof *conf); /* this essentialy disables the schedule */
    if (json_exists(js, ".sched_time")) 
        conf->sched_time = json_number(js, ".sched_time");
    if (json_exists(js, ".count")) 
        conf->count = json_number(js, ".count");
    if (json_exists(js, ".full_bk_intvl")) 
        conf->full_bk_intvl = json_number(js, ".full_bk_intvl");
    if (json_exists(js, ".incr_bk_intvl")) 
        conf->incr_bk_intvl = json_number(js, ".incr_bk_intvl");
    
close_js:
    json_close(js);
    return -status;

}


static int proc_created(MYSQL *db_conn, const snpy_job_t *job) {
    int new_state = SNPY_UPDATE_SCHED_STATE(job->state,
                                            SNPY_SCHED_STATE_READY);
    return  snpy_job_update_state(db_conn, job,
                                  job->id, job->argv[0],
                                  job->state, new_state,
                                  0,
                                  NULL);
}

static int proc_done(MYSQL *db_conn, snpy_job_t *job) {
    /* TODO: adding event handling mechanism here for related jobs */
    
    return 0;
}

static const char* choose_sub_proc_name(void) {
    return "bk_single_full";      
}

static int add_job_instance(MYSQL *db_conn, snpy_job_t *job) {
    int rc;
    rc = db_insert_new_job(db_conn);
    if (rc < 0) 
        return rc;
    
    snpy_job_t sub_job;
    memset(&sub_job, 0, sizeof sub_job);
    sub_job.id = rc;
    sub_job.sub = 0; sub_job.next = 0; sub_job.parent = job->id;
    sub_job.grp = sub_job.id; sub_job.root = job->root;
    sub_job.state = SNPY_SCHED_STATE_CREATED;
    sub_job.result = 0;
    sub_job.policy = BIT(0) | BIT(2); /* arg0, arg2 */
    
    /* setting instance full or incr */
    const char *sub_proc_name = choose_sub_proc_name();
    if (sub_proc_name == NULL) 
        return  -SNPY_ENOPROC;
    
   
    /* update sub job */
    rc = db_update_job_partial(db_conn, &sub_job) ||
         db_update_str_val(db_conn, "feid", sub_job.id, job->feid) ||
         db_update_str_val(db_conn, "arg0", sub_job.id, sub_proc_name) ||
         db_update_str_val(db_conn, "arg2", sub_job.id, job->argv[2]) ||
         db_update_int_val(db_conn, "sub", job->id, sub_job.id) ||
         snpy_job_update_state(db_conn, &sub_job,
                               job->id, job->argv[0],
                               0, SNPY_SCHED_STATE_CREATED,
                               0, NULL);
    return rc;

}

static int make_sched_arg(struct bk_single_sched_conf *sched_conf, char *arg, int arg_size)  {
    if (!arg || !sched_conf) 
        return -EINVAL;
    snprintf(arg, arg_size, 
             "{\"full_bk_intvl\":%llu, \"incr_bk_intvl\":%llu,\"count\":%llu,\"sched_time\":%llu}",
             (unsigned long long)sched_conf->full_bk_intvl, 
             (unsigned long long)sched_conf->incr_bk_intvl, 
             (unsigned long long)sched_conf->count, 
             (unsigned long long)sched_conf->sched_time);

    return 0;
}



static int get_next_sched_time(MYSQL *db_conn, snpy_job_t *job, 
                               struct bk_single_sched_conf *conf,
                               time_t *sched_time) {
    
    int rc;
    char log_buf[SNPY_LOG_SIZE]="";
    rc = db_get_val(db_conn, "log", job->sub, log_buf, sizeof log_buf);
    if (rc) 
        return rc;

    
    double val;
    rc = log_get_val_by_path(log_buf, sizeof log_buf, 
                             "[0][4]", &val, sizeof val);
    if (rc) 
        return rc;

    time_t cur_job_start = val;
    
    *sched_time  = cur_job_start + MIN(conf->full_bk_intvl, conf->incr_bk_intvl);
    return 0;

}

static int add_next_sched(MYSQL *db_conn, snpy_job_t *job) {
    int rc, status;
    struct bk_single_sched_conf sched_conf, next_sched_conf;
    if ((rc = sched_conf_init(&sched_conf, job->argv[1], job->argv_size[1])))
        return -SNPY_EARG;
    
    memcpy(&next_sched_conf, &sched_conf, sizeof next_sched_conf);
    
    /* set new scheduling configuration */
    /* set job count */
    if (sched_conf.count == 1) { 
        return 0;
    } else if (sched_conf.count != 0) {
        next_sched_conf.count --;  
    }
    /* set time scheduled to run */
    rc = get_next_sched_time(db_conn, job, 
                             &sched_conf, &(next_sched_conf.sched_time));


    if ((rc = db_insert_new_job(db_conn)) < 0)  
        return rc;
    snpy_job_t next_sched;
    //memcpy(&next_sched, job, sizeof next_sched);
    memset(&next_sched, 0, sizeof next_sched);
    next_sched.id = rc;
    next_sched.sub = 0;
    next_sched.next = 0;
    next_sched.next = 0;
    next_sched.parent = job->parent;
    next_sched.grp = job->grp;
    next_sched.root = job->root;
    next_sched.state = SNPY_SCHED_STATE_CREATED;
    next_sched.result = 0;
    next_sched.policy = BIT(0) | BIT(1) | BIT(2);
    char sched_arg[4096];

    rc = make_sched_arg(&next_sched_conf, sched_arg, sizeof sched_arg);
    if (rc) 
        return rc;
     /* update next schedule */
    if((rc = db_update_job_partial(db_conn, &next_sched)) || 
       (rc = db_update_str_val(db_conn, "feid", next_sched.id, job->feid)) ||
       (rc = db_update_str_val(db_conn, "arg1", next_sched.id, sched_arg)) ||
       (rc = db_update_str_val(db_conn, "arg0", next_sched.id, job->argv[0])) ||
       (rc = db_update_str_val(db_conn, "arg2", next_sched.id, job->argv[2])))
        return rc;

    /* set snap as the first sub job  */
    if ((rc = db_update_int_val(db_conn, "next", job->id, next_sched.id)))
        return rc;
 
    rc = snpy_job_update_state(db_conn, &next_sched,
                               job->id, job->argv[0],
                               0, SNPY_SCHED_STATE_CREATED,
                               0, NULL);
    return rc;
   
}

/* proc_ready() - handle ready state 
 *
 */
static int proc_ready(MYSQL *db_conn, snpy_job_t *job) {
    int rc;
    int status = 0;
    int new_state = -1;
    struct bk_single_sched_conf sched_conf;

    if (!db_conn || !job) {
        return -EINVAL;
    }
    /* should I run it now? */
    if ((rc = sched_conf_init(&sched_conf, job->argv[1], job->argv_size[1])))
        return -SNPY_EARG;
    if (!do_sched(&sched_conf))
        return 0;
    /* add a schedule instance as its sub job */
    if (job->sub == 0) {
        rc = add_job_instance(db_conn, job);
        if (rc) 
            status = SNPY_ESPAWNJ;
        new_state = SNPY_UPDATE_SCHED_STATE(job->state, 
                                            SNPY_SCHED_STATE_BLOCKED);
        goto change_state;
    }                               /* done adding job instance */
    
    /*  instance exsits */
    int inst_done, inst_result;
    int inst_id = job->sub;
    /* check result */
    rc = db_get_ival(db_conn, "done", inst_id, &inst_done) ||
        db_get_ival(db_conn, "result", inst_id, &inst_result);
    if (rc) 
        return rc;

    if (!inst_done)
        return -EBUSY;  /* job instance still running */
    if (inst_result) {  /* snapshot sub job error */
        new_state = SNPY_UPDATE_SCHED_STATE(job->state,
                                            SNPY_SCHED_STATE_DONE);
        status = SNPY_ESUB;
        goto change_state;
    }

    /* job instance complete successfully */
    if (job->next == 0) {
        rc = add_next_sched(db_conn, job);
        if (rc) {
            status = SNPY_ESPAWNJ;
       }
       new_state = SNPY_UPDATE_SCHED_STATE(job->state,
                                           SNPY_SCHED_STATE_DONE);
 
    }
    
change_state:
    if (new_state != -1)    
        rc = snpy_job_update_state(db_conn, job,
                              job->id, job->argv[0],
                              job->state, new_state,
                              status, NULL);
    return rc?rc:status;
}


/* do_schd() - decide if a scheduled job should be dispatched now.
 *
 * return 
 */
static int do_sched(struct bk_single_sched_conf *conf) {

    time_t now = time(NULL);
    if (conf && now > conf->sched_time)
        return 1;
    return 0;
}

static int proc_blocked(MYSQL *db_conn, snpy_job_t *job) {
    int rc;
    struct bk_single_sched_conf sched_conf;
    if (!db_conn || !job) {
        return -EINVAL;
    }
    if ((rc = sched_conf_init(&sched_conf, job->argv[1], job->argv_size[1])))
        return rc;
    if (!do_sched(&sched_conf))
        return 0;
    int new_state = SNPY_UPDATE_SCHED_STATE(job->state,
                                            SNPY_SCHED_STATE_READY);
    rc = snpy_job_update_state(db_conn, job,
                               job->id, job->argv[0],
                               job->state, new_state,
                               0, NULL);
    return rc;
}

static int proc_term(MYSQL *db_conn, snpy_job_t *job) {
    if (db_check_sub_job_done(db_conn, job->id)) {
        int rc;
        int sched_state = SNPY_SCHED_STATE_DONE;
        int new_state = SNPY_UPDATE_SCHED_STATE(job->state, sched_state);
        rc = snpy_job_update_state(db_conn, job,
                                   job->id, job->argv[0],
                                   job->state, new_state,
                                   0, NULL);
        return rc;

    } 
    /* spawn next schedule */

    return 0;
}

/*
 * return:
 *  0 - success
 *  1 - database error
 *  2 - job record missing/invalid.
 */

int bk_single_sched_proc  (MYSQL *db_conn, int job_id) {  
    int rc; 
    snpy_job_t *job;
    rc = mysql_query(db_conn, "start transaction;");
    if (rc != 0)  return 1;
    
    db_lock_job_tree(db_conn, job_id);
    rc = snpy_job_get(db_conn, &job, job_id);
    if (rc) return 1;
    int sched_state = SNPY_GET_SCHED_STATE(job->state);
    switch (sched_state) {
    case SNPY_SCHED_STATE_CREATED:
        rc = proc_created(db_conn, job);
        break;

    case SNPY_SCHED_STATE_DONE:
        rc = proc_done(db_conn, job);
        break;  /* shouldn't be here*/

    case SNPY_SCHED_STATE_READY:
        rc = proc_ready(db_conn, job);
        break; 

    case SNPY_SCHED_STATE_BLOCKED:
        rc = proc_blocked(db_conn, job);
        break;

    case SNPY_SCHED_STATE_TERM:
        rc = proc_term(db_conn, job);
        break;
    }

    snpy_job_free(job); job = NULL;
    if (rc == 0) 
        rc = mysql_commit(db_conn);
    else 
        rc = mysql_rollback(db_conn);

    return rc;
}
