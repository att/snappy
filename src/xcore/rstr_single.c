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
#include <errno.h>


#include "snappy.h"
#include "db.h"
#include "arg.h"
#include "log.h"
#include "job.h"

#include "snpy_util.h"

#include "rstr_single.h"


static int proc_created(MYSQL *db_conn, snpy_job_t *job);
static int proc_done(MYSQL *db_conn, snpy_job_t *job);
static int proc_ready(MYSQL *db_conn, snpy_job_t *job);
static int proc_blocked(MYSQL *db_conn, snpy_job_t *job);
static int proc_term(MYSQL *db_conn, snpy_job_t *job);



static int job_validate(MYSQL *db_conn, snpy_job_t *job, 
                        char *err_msg, int err_msg_size) {
     /* find argument used for historical job */
    char hist_job_arg0[4096] = "";
    int hist_job_id;
    double js_val;
    int rc = snpy_get_json_val(job->argv[1], job->argv_size[1], 
                               ".rstr_to_job_id",
                               &js_val, sizeof js_val);
    if (rc) {
        if (err_msg) 
            snprintf(err_msg, err_msg_size,
                     "can not get restore to job id: %d.", rc);
        return rc;
    }

    hist_job_id = js_val;
    rc = db_get_val(db_conn, "arg0", hist_job_id,
                    hist_job_arg0, sizeof hist_job_arg0);
    if (rc) {
        if (err_msg) 
            snprintf(err_msg, err_msg_size,
                     "can not get job arg0: %d.", rc);
        return rc;
    }
    if (strcmp(hist_job_arg0, "export")) {
        if (err_msg) 
            snprintf(err_msg, err_msg_size,
                     "restore target job is not export.");
        
        return -SNPY_EARG;
    }
    return 0;

}

static int proc_created(MYSQL *db_conn, snpy_job_t *job) {
    int rc;
    int status = 0;
    char ext_err_msg[SNPY_LOG_MSG_SIZE]="";
    int new_state;
    if(job_validate(db_conn, job, ext_err_msg, sizeof ext_err_msg)) {
        new_state = SNPY_UPDATE_SCHED_STATE(job->state,
                                            SNPY_SCHED_STATE_DONE);
        status = SNPY_EINVREC;
        goto change_state;
    }

    new_state = SNPY_UPDATE_SCHED_STATE(job->state,
                                        SNPY_SCHED_STATE_READY); 
change_state:
    return snpy_job_update_state(db_conn, job,
                                 job->id, job->argv[0],
                                 job->state, new_state,
                                 status,
                                 "s", "ext_err_msg", ext_err_msg);

}



static int proc_done(MYSQL *db_conn, snpy_job_t *job) {
    
    return 0;
}

static int add_job_get(MYSQL *db_conn, snpy_job_t *job) {
    int rc;
    if (job->sub != 0) 
        return -EINVAL;
    if ((rc = db_insert_new_job(db_conn)) < 0)  
        return rc;
    snpy_job_t sub_job;
    memset(&sub_job, 0, sizeof sub_job);
    sub_job.id = rc;
    sub_job.sub = 0; sub_job.next = 0; sub_job.parent = job->id;
    sub_job.grp = sub_job.id; sub_job.root = job->root;
    sub_job.state = SNPY_SCHED_STATE_CREATED;
    sub_job.result = 0;
    sub_job.policy = BIT(0) | BIT(1) | BIT(2); /* arg0, arg2 */
    
    /* fill out restore job's restore target */
    char *sub_job_arg2 = NULL; 
    /* if we see arg2 column is non-empty then use it */
    if (strlen(job->argv[2]) != 0) {
        sub_job_arg2 = job->argv[2]; /* use what frontend specifies */             
    } else {
        /* find argument used for historical job */
        char hist_job_arg2[4096] = "";
        int hist_job_id;
        double js_val;
        rc = snpy_get_json_val(job->argv[1], job->argv_size[1], ".rstr_to_job_id",
                               &js_val, sizeof js_val);
        if (rc)
            return rc;

        hist_job_id = js_val;
        rc = db_get_val(db_conn, "arg2", hist_job_id, 
                        hist_job_arg2, sizeof hist_job_arg2);
        if (rc)
            return rc;
        sub_job_arg2 = hist_job_arg2;
    }
    /* update sub job */
    if((rc = db_update_job_partial(db_conn, &sub_job)) ||
       (rc = db_update_str_val(db_conn, "feid", sub_job.id, job->feid)) ||
       (rc = db_update_str_val(db_conn, "arg0", sub_job.id, "get")) ||
       (rc = db_update_str_val(db_conn, "arg1", sub_job.id, job->argv[1])) ||
       (rc = db_update_str_val(db_conn, "arg2", sub_job.id, sub_job_arg2)))
        
        return rc;
    /* set snap as the first sub job */
    if ((rc = db_update_int_val(db_conn, "sub", job->id, sub_job.id)))
        return rc;
 
    rc = snpy_job_update_state(db_conn, &sub_job,
                               job->id, job->argv[0],
                               0, SNPY_SCHED_STATE_CREATED,
                               0, NULL);
    return rc;

}

/*
 * proc_read() - handles ready state
 *
 */

static int proc_ready(MYSQL *db_conn, snpy_job_t *job) {

    int rc;
    int status = 0;
    int new_state;
    char msg[SNPY_LOG_MSG_SIZE]="";

    if (job->sub == 0) {
        rc = add_job_get(db_conn, job);
        if (rc) {
            new_state = SNPY_UPDATE_SCHED_STATE(job->state,
                                                SNPY_SCHED_STATE_DONE);
            status = SNPY_ESPAWNJ;
            goto change_state;
        } else {
            new_state = SNPY_UPDATE_SCHED_STATE(job->state, 
                                                SNPY_SCHED_STATE_BLOCKED);
            status = 0;
            goto change_state;
        }
    }
    
    /* get job exist */
    int get_done, get_result;
    int get_job_id = job->sub;
    rc = db_get_ival(db_conn, "done", get_job_id, &get_done) ||
        db_get_ival(db_conn, "result", get_job_id, &get_result);
    if (rc) 
        return rc;

    if (!get_done)
        return -EBUSY;
    /* get job is done */
    if (get_result) {  /* snapshot sub job error */
        new_state = SNPY_UPDATE_SCHED_STATE(job->state,
                                            SNPY_SCHED_STATE_DONE);
        status = SNPY_ESUB;
        goto change_state;

    }
    /* success */
    status = 0;
    new_state = SNPY_UPDATE_SCHED_STATE(job->state,
                                        SNPY_SCHED_STATE_DONE);

    /* update job status */

change_state:
    return snpy_job_update_state(db_conn, job, 
                          job->id, job->argv[0],
                          job->state, new_state,
                          status, NULL); 



    return 0;

}

static int job_check_ready(MYSQL *db_conn, snpy_job_t *job, int *error) {
    int is_ready;
    int rc;
    if ((rc = db_check_sub_job_done(db_conn, job->id)) == 0) 
        is_ready = 0; 
    else 
        is_ready = 1;
        
    if (error) 
        *error = 0;
    return is_ready;

}


static int proc_blocked(MYSQL *db_conn, snpy_job_t *job) {
    int rc = 0;

    if (!db_conn || !job) 
        return -EINVAL;
    
    if (!job_check_ready(db_conn, job, &rc))
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

    return 0;
}

/*
 * return:
 *  0 - success
 *  1 - database error
 *  2 - job record missing/invalid.
 */

int rstr_single_proc (MYSQL *db_conn, int job_id) {  
    int rc; 
    int status = 0;
    snpy_job_t *job;
    rc = mysql_query(db_conn, "start transaction;");
    if (rc != 0)  return 1;
    
    db_lock_job_tree(db_conn, job_id);
    rc = snpy_job_get(db_conn, &job, job_id);
    if (rc) return 1;
    int sched_state = SNPY_GET_SCHED_STATE(job->state);
    switch (sched_state) {
    case SNPY_SCHED_STATE_CREATED:
        proc_created(db_conn, job);
        break;

    case SNPY_SCHED_STATE_DONE:
        proc_done(db_conn, job);
        break;  /* shouldn't be here*/

    case SNPY_SCHED_STATE_READY:
        proc_ready(db_conn, job);
        break; 

    case SNPY_SCHED_STATE_BLOCKED:
       proc_blocked(db_conn, job);
        break;

    case SNPY_SCHED_STATE_TERM:
        proc_term(db_conn, job);
        break;
    }
    

    snpy_job_free(job); job = NULL;

    if (status == 0) 
        rc = mysql_commit(db_conn);
    else 
        rc = mysql_rollback(db_conn);

    return rc?1:status;
}

