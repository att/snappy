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


#include "jsmn.h"
#include "snappy.h"
#include "db.h"
#include "arg.h"
#include "log.h"
#include "job.h"

#include "snap.h"


static int proc_created(MYSQL *db_conn, snpy_job_t *job);
static int proc_done(MYSQL *db_conn, snpy_job_t *job);
static int proc_ready(MYSQL *db_conn, snpy_job_t *job);
static int proc_blocked(MYSQL *db_conn, snpy_job_t *job);
static int proc_term(MYSQL *db_conn, snpy_job_t *job);


static int proc_created(MYSQL *db_conn, snpy_job_t *job) {
    int rc;
    int new_state = SNPY_UPDATE_SCHED_STATE(job->state,
                                            SNPY_SCHED_STATE_BLOCKED);
    log_rec_t rec = {job->id, job->state, new_state, time(NULL),
        0, "ok", ""};
                                    
    char log_buf[SNPY_LOG_SIZE];
    memcpy(log_buf, job->log, job->log_size);
    if ((rc = log_add_rec(log_buf, sizeof log_buf, &rec)) ||
        (rc = db_update_int_val(db_conn, "state", job->id, new_state)) ||
        (rc = db_update_str_val(db_conn, "log", job->id,  log_buf))) 
        return rc;
    
    return 0;
}
static int proc_done(MYSQL *db_conn, snpy_job_t *job) {
    
    return 0;
}

/*
 * proc_read() - handles ready state
 *
 */

static int proc_ready(MYSQL *db_conn, snpy_job_t *job) {
    int rc;
    snpy_job_t sub_job;
    if (job->sub != 0) 
        return -EINVAL;
    if ((rc = db_insert_new_job(db_conn)) < 0)  
        return rc;
    sub_job.id = rc;
    sub_job.sub = 0; sub_job.next = 0; sub_job.parent = job->id;
    sub_job.grp = sub_job.id; sub_job.root = job->root;
    sub_job.state = SNPY_SCHED_STATE_CREATED;
    sub_job.result = 0;
    sub_job.policy = BIT(0) | BIT(2); /* arg0, arg2 */
    
    /* setting up log */
    struct log_rec sub_log_rec = {
        .id = job->id,
        .old_state = 0,
        .new_state = SNPY_SCHED_STATE_CREATED,
        .timestamp = time(NULL),
        .res_code = 0,
        .res_msg = "ok",
        .extra = ""
    };
    char log_buf[SNPY_LOG_SIZE] = "";
    if((rc = log_add_rec(log_buf, sizeof log_buf, &sub_log_rec)))
        return rc;

    /* update sub job */
    if((rc = db_update_job_partial(db_conn, &sub_job)) || 
       (rc = db_update_str_val(db_conn, "feid", sub_job.id, job->feid)) ||
       (rc = db_update_str_val(db_conn, "arg0", sub_job.id, "snapshot")) ||
       (rc = db_update_str_val(db_conn, "arg2", sub_job.id, job->argv[2])) ||
       (rc = db_update_str_val(db_conn, "log", sub_job.id, log_buf)))
        
        return rc;

    /* update job status */
    int new_state = SNPY_UPDATE_SCHED_STATE(job->state, SNPY_SCHED_STATE_TERM);
    if ((rc = db_update_int_val(db_conn, "state", job->id, new_state)) ||
        (rc = db_update_int_val(db_conn, "sub", job->id, sub_job.id)))
        return rc;

    log_rec_t log_rec = {
        .id = job->id,
        .old_state = job->state,
        .new_state = new_state,
        .timestamp = time(NULL),
        .res_code = 0,
        .res_msg = "ok",
        .extra = ""
    };

    memcpy(log_buf, job->log, job->log_size);
    if ((rc = log_add_rec(log_buf, sizeof log_buf, &log_rec)) ||
        (rc = db_update_str_val(db_conn, "log", job->id, log_buf)))
        return rc;


    return 0;

}

static int job_check_ready(snpy_job_t *job) {
    return 1;

}
static int proc_blocked(MYSQL *db_conn, snpy_job_t *job) {
    int rc;
    if (!db_conn || !job) 
        return -EINVAL;
    
    if (!job_check_ready(job))
        return 0;

    int new_state = SNPY_UPDATE_SCHED_STATE(job->state,
                                            SNPY_SCHED_STATE_READY);
    log_rec_t rec = { job->id, job->state, new_state, time(NULL), 0, "ok", ""};

    char log_buf[SNPY_LOG_SIZE] = "";
    memcpy(log_buf, job->log, job->log_size);
    if ((rc = log_add_rec(log_buf, sizeof log_buf, &rec))) {
        return rc;
    }
    if ((rc = db_update_int_val(db_conn, "state", job->id, new_state)) ||
        (rc = db_update_str_val(db_conn, "log", job->id,  log_buf)))
        return rc;

    return 0;

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

int diff_proc (MYSQL *db_conn, int job_id) {  
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

