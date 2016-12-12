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
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <syslog.h>


//#include "jsmn.h"
#include "snappy.h"
#include "db.h"
#include "arg.h"
#include "log.h"
#include "job.h"
#include "util.h"

#include "bk_single_full.h"


static int proc_created(MYSQL *db_conn, snpy_job_t *job);
static int proc_done(MYSQL *db_conn, snpy_job_t *job);
static int proc_ready(MYSQL *db_conn, snpy_job_t *job);
static int proc_blocked(MYSQL *db_conn, snpy_job_t *job);
static int proc_zombie(MYSQL *db_conn, snpy_job_t *job);

static int job_check_ready(MYSQL *db_conn, snpy_job_t *job, int *error);


static int proc_created(MYSQL *db_conn, snpy_job_t *job) {
    int rc;
    int new_state = SNPY_UPDATE_SCHED_STATE(job->state,
                                            SNPY_SCHED_STATE_READY);

    return  snpy_job_update_state(db_conn, job,
                                  job->id, job->argv[0],
                                  job->state, new_state,
                                  0,
                                  NULL);
    

}
static int proc_done(MYSQL *db_conn, snpy_job_t *job) {
    
    return 0;
}

static int add_job_snap(MYSQL *db_conn, snpy_job_t *job) {
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
    sub_job.policy = BIT(0) | BIT(2); /* arg0, arg2 */
    
    /* update sub job */
    if((rc = db_update_job_partial(db_conn, &sub_job)) || 
       (rc = db_update_str_val(db_conn, "feid", sub_job.id, job->feid)) ||
       (rc = db_update_str_val(db_conn, "arg0", sub_job.id, "snap")) ||
       (rc = db_update_str_val(db_conn, "arg2", sub_job.id, job->argv[2])))
        
        return rc;
    /* set snap as the first sub job  */
    if ((rc = db_update_int_val(db_conn, "sub", job->id, sub_job.id)))
        return rc;
 
    rc = snpy_job_update_state(db_conn, &sub_job,
                               job->id, job->argv[0],
                               0, SNPY_SCHED_STATE_CREATED,
                               0, NULL);
    return rc;

}

static int add_job_export(MYSQL *db_conn, snpy_job_t *job) {
    int rc;
    

    snpy_job_t export;
    memset(&export, 0, sizeof export);
    int sub_grp_id;
    if (job->sub == 0) 
        return -EINVAL;
    if ((rc = db_get_ival(db_conn, "grp",  job->sub, &sub_grp_id))) 
        return rc;
    
    /* TODO: add more snap job state check */

    if ((rc = db_insert_new_job(db_conn)) < 0)  
        return rc;
    export.id = rc;
    export.sub = 0; export.next = 0; export.parent = job->id;
    export.grp = sub_grp_id; /* set sub group id */
    export.root = job->root;
    export.state = SNPY_SCHED_STATE_CREATED;
    export.result = 0;
    export.policy = BIT(0) | BIT(2); /* arg0, arg2 */
 
    char export_arg[4096];
    
    
    /* update export job */
    if(
       (rc = db_get_val(db_conn,
                        "arg2", 
                        job->sub, 
                        export_arg, ARRAY_SIZE(export_arg))) ||
       (rc = db_update_job_partial(db_conn, &export)) || 
       (rc = db_update_str_val(db_conn, "feid", export.id, job->feid)) ||
       (rc = db_update_str_val(db_conn, "arg0", export.id, "export")) ||
       (rc = db_update_str_val(db_conn, "arg2", export.id, export_arg))
       )
        
        return rc;
    
    /* set export job as the next of snap  */
    if ((rc = db_update_int_val(db_conn, "next", job->sub, export.id)))
        return rc;
    return snpy_job_update_state(db_conn, &export,
                                 job->id, job->argv[0],
                                 0, SNPY_SCHED_STATE_CREATED,
                                 0, NULL);

      
}


static int get_last_job(MYSQL *db_conn, snpy_job_t *job) {
    
    return 0;
}



/*
 * proc_ready() - handles ready state
 *
 */

static int proc_ready(MYSQL *db_conn, snpy_job_t *job) {
    int rc;
    int status = 0;
    int new_state;
    char msg[SNPY_LOG_MSG_SIZE]="";
    struct action {
        const char *proc;
        int (*action)(MYSQL*, snpy_job_t*);
    } action_list[3] =
    { 
        {"snap", add_job_snap},
        {"export", add_job_export}
    };
 
    if (job->sub == 0) {
        rc = add_job_snap(db_conn, job);
        if (rc) {
            new_state = SNPY_UPDATE_SCHED_STATE(job->state,
                                                SNPY_SCHED_STATE_DONE);
            status = SNPY_EPROC;
            goto change_state;
        } else {
            new_state = SNPY_UPDATE_SCHED_STATE(job->state, 
                                                SNPY_SCHED_STATE_BLOCKED);
            status = 0;
            goto change_state;
        }
    }
    
    /* snapshot job exist */
    int snap_done, snap_result;
    int snap_job_id = job->sub;
    int snap_job_next;
    rc = db_get_ival(db_conn, "next", snap_job_id, &snap_job_next) ||
        db_get_ival(db_conn, "done", snap_job_id, &snap_done) ||
        db_get_ival(db_conn, "result", snap_job_id, &snap_result);
    if (rc) 
        return rc;

    if (!snap_done)
        return 0;
    /* snapshot job is done */
    if (snap_result) {  /* snapshot sub job error */
        new_state = SNPY_UPDATE_SCHED_STATE(job->state,
                                            SNPY_SCHED_STATE_DONE);
        status = SNPY_ESUB;
        goto change_state;

    }
    if ((snap_job_next == 0)) { /* no export job yet */
        add_job_export(db_conn, job);
        new_state = SNPY_UPDATE_SCHED_STATE(job->state,
                                            SNPY_SCHED_STATE_BLOCKED);
        status = 0;
        goto change_state;
    }

    /* export exists */
    int export_done, export_result;
    int export_job_id = snap_job_next;
    
    rc = db_get_ival(db_conn, "done", export_job_id, &export_done) ||
        db_get_ival(db_conn, "result", export_job_id, &export_result);
    if (rc)
        return rc;
    if (!export_done) /* export running */
        return 0;
    /* export job is done */
    if(export_result) {     /* error */
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

}

/* job_check_ready() 
 *
 * Simple strategy: if all the sub job are done, it should be ready
 *
 */

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
static int proc_zombie(MYSQL *db_conn, snpy_job_t *job) {

    return 0;
}

/*
 * return:
 *  0 - success
 *  1 - database error
 *  2 - job record missing/invalid.
 */

int bk_single_full_proc (MYSQL *db_conn, int job_id) {  
    int rc; 
    int status = 0;
    snpy_job_t *job;
    rc = mysql_query(db_conn, "start transaction;");
    if (rc != 0)  return 1;
    
    db_lock_job_tree(db_conn, job_id);
    /*
    {      
        mysql_rollback(db_conn);
        syslog(LOG_DEBUG, "job is being handled by another broker, skipped.");
        return 0;
    }
    */
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

    case SNPY_SCHED_STATE_ZOMBIE:
        proc_zombie(db_conn, job);
        break;
    }
    

    snpy_job_free(job); job = NULL;

    if (status == 0) 
        rc = mysql_commit(db_conn);
    else 
        rc = mysql_rollback(db_conn);

    return rc?1:status;
}

