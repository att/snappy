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
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <sys/wait.h>
#include <dirent.h>
#include <syslog.h>
#include <unistd.h>


#include "snappy.h"
#include "db.h"
#include "arg.h"
#include "log.h"
#include "job.h"
#include "snpy_util.h"
#include "error.h"
#include "stringbuilder.h"
#include "conf.h"

#include "snap.h"


struct plugin_env {
    char wd[PATH_MAX];
    char entry_pt[PATH_MAX];
};


static int proc_created(MYSQL *db_conn, snpy_job_t *job);
static int proc_ready(MYSQL *db_conn, snpy_job_t *job);
static int proc_blocked(MYSQL *db_conn, snpy_job_t *job);
static int proc_term(MYSQL *db_conn, snpy_job_t *job);

static int snap_env_init(snpy_job_t *job) ;

static int plugin_env_init(struct plugin_env *env, snpy_job_t *job);

static int get_wd_path(int job_id, char *wrkdir_path, int wrkdir_path_size);

int plugin_env_init(struct plugin_env *env, snpy_job_t *job) {
    int rc;
    if (!env || !job)
        return -EINVAL;
    snprintf (env->wd, sizeof env->wd, "/%s/%d/",
              conf_get_run(), job->id);
    rc = strlcpy(env->entry_pt,
                 "/var/lib/snappy/plugins/rbd/snpy_rbd",
                 sizeof env->entry_pt);
    
    return 0;
}


static int snap_env_init(snpy_job_t *job) {
    int rc;
    int status = 0;
    char wd_path[PATH_MAX] = "";
    int wd_fd;
    rc = snprintf(wd_path, sizeof wd_path, 
                  "/%s/%d", conf_get_run(), job->id);
    if (rc == sizeof wd_path)
        return -ERANGE;
    struct stat wd_st;
    if (!lstat(wd_path, &wd_st) && S_ISDIR(wd_st.st_mode)) {
        syslog(LOG_DEBUG, "working directory exists, trying cleanup.\n");
        if ((rc = rmdir_recurs(wd_path))) 
            return rc;
    }

    /* setting up directories */
    if ((rc = mkdir(wd_path, 0700)))   
        return -errno;
    if(((wd_fd = open(wd_path, O_RDONLY)) == -1) ||
        (rc = mkdirat(wd_fd, "meta", 0700)) || 
        (rc = mkdirat(wd_fd, "data", 0700))) {

        status = errno;
        goto free_wd_fd;
    }
    

    /* setup id */

    if ((rc = kv_put_ival("meta/id", job->id, wd_path))) {
        status = -rc;
        goto free_wd_fd;
    }

    /* setup cmd */
    if ((rc = kv_put_sval("meta/cmd", job->argv[0], job->argv_size[0], wd_path))) {
        status = -rc;
        goto free_wd_fd;
    }

    /* setup arg */
    if ((rc = kv_put_sval("meta/arg", job->argv[2], job->argv_size[2], wd_path))) {
        status = -rc;
        goto free_wd_fd;
    }

free_wd_fd:
    close(wd_fd);
    return -status;;
}


static int proc_created(MYSQL *db_conn, snpy_job_t *job) {
    int rc;
    int status;
    int new_state;
    char msg[SNPY_LOG_MSG_SIZE]="";
    char wd_path[PATH_MAX]="";
    
    struct  plugin_env pe;

    
    if((rc = plugin_env_init(&pe, job)) || (rc = snap_env_init(job))) {
        /* handling error */
        status = SNPY_EENVJ;
        
        new_state = SNPY_UPDATE_SCHED_STATE(job->state,
                                            SNPY_SCHED_STATE_TERM);
        goto change_state;
    }

    /* spawn snapshot process */
    pid_t pid = fork();
    if (pid < 0) {
       status = SNPY_ESPAWNJ;
       new_state = SNPY_UPDATE_SCHED_STATE(job->state,
                                           SNPY_SCHED_STATE_TERM);
       goto change_state;
    }

    if (pid == 0) { 
        if (chdir(pe.wd)) { /* switch to working directory */
            status = errno;
            exit(-status);
        }
        if (execl(pe.entry_pt, pe.entry_pt, (char*)NULL) == -1) {
            status = errno;
            exit(-status);
        }
    }
    
    if (get_wd_path(job->id, wd_path, sizeof wd_path) || 
        (rc = kv_put_ival("meta/pid", pid, wd_path))) {
        status = SNPY_EBADJ;
        new_state = SNPY_UPDATE_SCHED_STATE(job->state,
                                            SNPY_SCHED_STATE_TERM);
        goto change_state;
    }

    /* if we are here, change job status to running */
    status = 0;
    new_state = SNPY_UPDATE_SCHED_STATE(job->state,
                                        SNPY_SCHED_STATE_RUN);

change_state:   
    /* To running state */
    return  snpy_job_update_state(db_conn, job,
                                  job->id, job->argv[0],
                                  job->state, new_state,
                                  status,
                                  NULL);
   
}


static int proc_ready(MYSQL *db_conn, snpy_job_t *job) {
    return 0;
}

static int get_wd_path(int job_id, char *wd_path, int wd_path_size) {
    
    int rc = snprintf (wd_path, wd_path_size, "/%s/%d/",
                       conf_get_run(), job_id);
    if (rc == wd_path_size) 
        return -ENAMETOOLONG;
    else 
        return 0;
}

static int proc_run(MYSQL *db_conn, snpy_job_t *job) {
    int rc;
    char buf[64];
    int pid;
    int status;
    int new_state;
    log_rec_t log_rec;

    char wd_path[PATH_MAX]="";
    char ext_err_msg[SNPY_LOG_MSG_SIZE]="";

    if (get_wd_path(job->id, wd_path, sizeof wd_path)) {
        new_state = SNPY_UPDATE_SCHED_STATE(job->state, SNPY_SCHED_STATE_TERM);
        status = SNPY_EBADJ;
        goto change_state;
    }

    if ((rc = kv_get_ival("meta/pid", &pid, wd_path))) {
               
        new_state = SNPY_UPDATE_SCHED_STATE(job->state, SNPY_SCHED_STATE_TERM);
        status = SNPY_EBADJ;
        snprintf(ext_err_msg, sizeof ext_err_msg, 
                 "failed getting plugin pid: %d", rc);
        goto change_state;
    }
    rc = waitpid(pid, NULL, WNOHANG);
    if (rc == 0 || (rc == -1 && errno != ECHILD)) 
        return 0; 
    
    
    char arg_out[4096];
    int plugin_job_status;
    if ((rc = kv_get_ival("meta/status", &plugin_job_status, wd_path)) ||
        (rc = kv_get_sval("meta/arg.out", arg_out, sizeof arg_out, wd_path))) {
        new_state = SNPY_UPDATE_SCHED_STATE(job->state,
                                            SNPY_SCHED_STATE_TERM);
        status = SNPY_EBADJ;
        snprintf(ext_err_msg, sizeof ext_err_msg, 
                 "failed getting plugin status or arg.out: %d", rc);
        goto change_state;
    }

    /* plugin failure */
    if (plugin_job_status) {
        new_state = SNPY_UPDATE_SCHED_STATE(job->state,
                                            SNPY_SCHED_STATE_TERM);
        status = SNPY_EPLUG;
        if (kv_get_sval("meta/status_msg", 
                         ext_err_msg, sizeof ext_err_msg, wd_path)) 
            ext_err_msg[0] = 0;
    }

    if ((rc = db_update_str_val(db_conn, "arg2", job->id, arg_out))) {
        new_state = SNPY_UPDATE_SCHED_STATE(job->state,
                                            SNPY_SCHED_STATE_TERM);
        status = SNPY_EDBCONN;
        goto change_state;
    }
    /* snapshot complete successfully */
    status = 0;
    new_state = SNPY_UPDATE_SCHED_STATE(job->state, SNPY_SCHED_STATE_TERM);

    
change_state:
    return  snpy_job_update_state(db_conn, job,
                                  job->id, job->argv[0],
                                  job->state, new_state,
                                  status,
                                  "s", "ext_err_msg", ext_err_msg);

}

/*
 * proc_term() - handles term state
 *
 */

static int proc_term(MYSQL *db_conn, snpy_job_t *job) {
    int rc;
    int status;
    int new_state;
    char ext_err_msg[SNPY_LOG_MSG_SIZE]="";
    if (!job)
        return -EINVAL;
  
    /* harvesting put job status */
    new_state = SNPY_UPDATE_SCHED_STATE(job->state, SNPY_SCHED_STATE_DONE);
  
    snpy_wd_cleanup(job);

    return  snpy_job_update_state(db_conn, job,
                                  job->id, job->argv[0],
                                  job->state, new_state,
                                  job->result,
                                  "s", "ext_err_msg", ext_err_msg);

}

/* 
 *
 */

static int job_check_ready(snpy_job_t *job) {
    return 1;

}


static int proc_blocked(MYSQL *db_conn, snpy_job_t *job) {
    return 0;

}



/*
 * return:
 *  0 - success
 *  1 - database error
 *  2 - job record missing/invalid.
 */

int snap_proc (MYSQL *db_conn, int job_id) {  
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
        status = proc_created(db_conn, job);
        break;

    case SNPY_SCHED_STATE_DONE:
        break;  /* shouldn't be here*/

    case SNPY_SCHED_STATE_READY:
        status = proc_ready(db_conn, job);
        break; 

    case SNPY_SCHED_STATE_RUN:
        status = proc_run(db_conn, job);
        break; 


    case SNPY_SCHED_STATE_BLOCKED:
        status = proc_blocked(db_conn, job);
        break;

    case SNPY_SCHED_STATE_TERM:
        status = proc_term(db_conn, job);
        break;
    default:
        status = SNPY_ESTATJ;
        break;
    }
    

    snpy_job_free(job); job = NULL;

    if (status == 0) 
        rc = mysql_commit(db_conn);
    else 
        rc = mysql_rollback(db_conn);

    return rc?1:(-status);
}

