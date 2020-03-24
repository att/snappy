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
#include <unistd.h>


#include "snappy.h"
#include "db.h"
#include "arg.h"
#include "log.h"
#include "job.h"
#include "error.h"
#include "conf.h"
#include "plugin.h"

#include "snpy_util.h"
#include "snpy_log.h"
#include "stringbuilder.h"
#include "json.h"

#include "export.h"


struct plugin_env {
    char wd[PATH_MAX];
    char entry_pt[PATH_MAX];
};


static int proc_created(MYSQL *db_conn, snpy_job_t *job);
static int proc_ready(MYSQL *db_conn, snpy_job_t *job);
static int proc_blocked(MYSQL *db_conn, snpy_job_t *job);
static int proc_term(MYSQL *db_conn, snpy_job_t *job);

static int export(snpy_job_t *job);

static int plugin_env_init(struct plugin_env *env, snpy_job_t *job);
static int import_env_init(MYSQL* db_conn, snpy_job_t *job);

static int get_wd_path(int job_id, char *wrkdir_path, int wrkdir_path_size);


static int plugin_chooser(snpy_job_t *job, struct plugin **pi);
static int job_get_plugin_exec(snpy_job_t *job, 
                               char *pi_exec_path, int pi_exec_path_len);


static int job_get_wd(int job_id, char *wd, int wd_size) {
    
    int rc = snprintf (wd, wd_size, "/%s/%d/",
                       conf_get_run(), job_id);
    if (rc == wd_size) 
        return -ENAMETOOLONG;
    else 
        return 0;
}

/*  give a job - decide which plugin should be used
 *
 */

static int plugin_chooser(snpy_job_t *job, struct plugin **pi) {
    int status = 0;
    if (!job || !pi) 
        return -EINVAL;
    int error;
    struct json *js = json_open(JSON_F_NONE, &error);
    if (!js)
        return -SNPY_EARG;
    int rc = json_loadstring(js, job->argv[2]);
    if (rc) {
        goto close_js;
        return rc;
    }
    const char *pi_name = json_string(js, ".sp_name");
    if (!pi_name[0]) {
        status = SNPY_EINCOMPARG;
        goto close_js;
    }
    *pi = plugin_srch_by_name(pi_name);
    if (!(*pi)) {
        status = SNPY_ENOPLUG;
        goto close_js;
    }
close_js:
    json_close(js);
    return -status;
}

static int job_get_plugin_exec(snpy_job_t *job, 
                               char *pi_exec_path, int pi_exec_path_len) {
    int status = 0;
    if (!job)
        return -EINVAL;
    struct plugin *pi;
    status = plugin_choose(job->argv[2], &pi, NULL);
    if (status) 
        return -status;
    int rc = snprintf(pi_exec_path, pi_exec_path_len, "%s/%s/%s",
                      conf_get_plugin_home(), pi->name, plugin_get_exec(pi));
    if (rc >= pi_exec_path_len) 
        return -ENAMETOOLONG;
    if (access(pi_exec_path, X_OK)) 
        return -errno;
    return 0;
}



int plugin_env_init(struct plugin_env *env, snpy_job_t *job) {
    int rc;
    if (!env || !job)
        return -EINVAL;
    snprintf (env->wd, sizeof env->wd, "/%s/%d/",
              conf_get_run(), job->id);
    rc = strlcpy(env->entry_pt,
                 "/var/lib/snappy/plugins/swift/snpy_swift",
                 sizeof env->entry_pt);
    
    return 0;
}


/* 
 * get_get_id() - funny name as it is, it means to get the "get" job id.
 *
 * return:  <= 0 - error, > 0 - id 
 */
static int get_get_id(MYSQL *db_conn, int job_id) {
    int rc;
    int status;
    MYSQL_RES *result = NULL;
    const char *sql_fmt_str = 
        "select id from snappy.jobs where sub=%d;";
    rc = db_exec_sql(db_conn, 1, NULL, 0, sql_fmt_str, job_id);
    if (rc) {
        goto free_result;
    }
    result = mysql_store_result(db_conn);
    if (result == NULL) {
        goto free_result;
    }
    if ( mysql_num_rows(result) != 1)  {
        goto free_result;
    } else {
        int id;
        MYSQL_ROW row = mysql_fetch_row(result);
        unsigned long *col_lens = mysql_fetch_lengths(result);
        if (!row || !col_lens) {
            snpy_log(&xcore_log, SNPY_LOG_ERR, "%s", mysql_error(db_conn));
            goto free_result;
        }
        return atoi(row[0]); /*TODO: robust check */
    }

free_result:
    mysql_free_result(result);
    snpy_log(&xcore_log, SNPY_LOG_ERR, "job %d: query error: %s.", 
           job_id, mysql_error(db_conn));
    return -mysql_errno(db_conn);
}



static int import_env_init(MYSQL *db_conn, snpy_job_t *job) {
    int rc;
    int status = 0;
    char wd[PATH_MAX] = "";
    int wd_fd;
    rc = snprintf(wd, sizeof wd, 
                  "/%s/%d", conf_get_run(), job->id);
    if (rc == sizeof wd)
        return -ERANGE;
    struct stat wd_st;
    

    /* wd directory check if it already exists*/
    if (!lstat(wd, &wd_st) && S_ISDIR(wd_st.st_mode)) {
        snpy_log(&xcore_log, SNPY_LOG_DEBUG, "working directory exists, trying cleanup.\n");
        if ((rc = rmdir_recurs(wd))) 
            return rc;
    }

    /* setting up directories */
    if ((rc = mkdir(wd, 0700))) 
        return -errno;
    if(((wd_fd = open(wd, O_RDONLY)) == -1) ||
        (rc = mkdirat(wd_fd, "meta", 0700))) {
    
        status = errno;
        goto free_wd_fd;
    }
    /* move data from the get job dir */
    int get_id = get_get_id(db_conn, job->id);
    if (get_id <= 0) {
        status = -get_id;
        goto free_wd_fd;
    }
    char get_data_dir[PATH_MAX] = "";
    char import_data_dir[PATH_MAX] = "";
    snprintf(get_data_dir, PATH_MAX,
             "%s/%d/data", conf_get_run(), get_id);

    snprintf(import_data_dir, PATH_MAX,
             "%s/%d/data", conf_get_run(), job->id);

    rc = rename(get_data_dir, import_data_dir);
    if (rc == -1) {
        status = errno;
        snpy_log(&xcore_log, SNPY_LOG_ERR, "error moving export data directory: %d.", status);
        goto free_wd_fd;
    }

    /* setup import id */

    if ((rc = kv_put_ival("meta/id", job->id, wd))) {
        status = -rc;
        goto free_wd_fd;
    }

    /* setup cmd */
    if ((rc = kv_put_sval("meta/cmd", job->argv[0], job->argv_size[0], wd))) {
        status = -rc;
        goto free_wd_fd;
    }

    /* setup arg */
    if ((rc = kv_put_sval("meta/arg", job->argv[2], job->argv_size[2], wd))) {
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
    char ext_err_msg[SNPY_LOG_MSG_SIZE]="";
    char wd[PATH_MAX]="";
    char exec[PATH_MAX]="";
    

    if ((status = job_get_wd(job->id, wd, sizeof wd))) 
        return -status;
    if ((status = job_get_plugin_exec(job, exec, sizeof exec))) 
        return -status;

    if((rc = import_env_init(db_conn, job))) {
        /* handling error */
        status = SNPY_EENVJ;
        new_state = SNPY_UPDATE_SCHED_STATE(job->state,
                                            SNPY_SCHED_STATE_TERM);
        snprintf(ext_err_msg, sizeof ext_err_msg,
                 "env init error, code: %d.", rc);

        goto change_state;
    }
    /* spawn snapshot process */
    pid_t pid = fork();
    if (pid < 0) {
       status = SNPY_ESPAWNJ;
       new_state = SNPY_UPDATE_SCHED_STATE(job->state,
                                           SNPY_SCHED_STATE_TERM);

       snprintf(ext_err_msg, sizeof ext_err_msg,
                "fork error, code: %d.", rc);
       goto change_state;
    }

    /* child process */
    if (pid == 0) {         
        if (chdir(wd)) { /* switch to working directory */
            status = errno;
            exit(-status);
        }
        char * const argv[] = { 
            [0] = exec,
            [1] = NULL
        };
        if (execve(exec, argv, NULL) == -1) {
            status = errno;
            rc = kv_put_ival("meta/status", status, wd);
            exit(-status);
        }
    }
    
    if ((rc = kv_put_ival("meta/pid", pid, wd))) {
        status = SNPY_EBADJ;
        new_state = SNPY_UPDATE_SCHED_STATE(job->state,
                                            SNPY_SCHED_STATE_TERM);

        snprintf(ext_err_msg, sizeof ext_err_msg,
                 "can not set meta/pid, code: %d.", rc);
        goto change_state;
    }

    /* if we are here, change job status to running */
    status = 0;
    new_state = SNPY_UPDATE_SCHED_STATE(job->state,
                                        SNPY_SCHED_STATE_RUN);

change_state:   
    return  snpy_job_update_state(db_conn, job,
                                  job->id, job->argv[0],
                                  job->state, new_state,
                                  status,
                                  "s", "ext_err_msg", ext_err_msg);
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

/* 
 * check_snap_run_state() - sanity check for running snapshot job status
 *
 */
#if 0
static int check_snap_run_state(MYSQL *db_conn, snpy_job_t *job) {
    int wrkdir_fd = open(plugin_env.wd, O_RDONLY);
    if (wrkdir_fd == -1)  
        return -errno;

    if (faccessat(wrkdir_fd, "meta/pid", F_OK, 0) == -1) {
        close(wrkdir_fd);
        return -errno;
    }
    return 0;
}
#endif



static int proc_run(MYSQL *db_conn, snpy_job_t *job) {
    int rc;
    char buf[64];
    int pid;
    int status;
    int new_state;
    log_rec_t log_rec;

    char wd_path[PATH_MAX]="";
    char msg[SNPY_LOG_MSG_SIZE]="";

    if (get_wd_path(job->id, wd_path, sizeof wd_path)) {
        new_state = SNPY_UPDATE_SCHED_STATE(job->state, SNPY_SCHED_STATE_TERM);
        status = SNPY_EBADJ;
        goto change_state;
    }

    if ((rc = kv_get_ival("meta/pid", &pid, wd_path))) {
        new_state = SNPY_UPDATE_SCHED_STATE(job->state, SNPY_SCHED_STATE_TERM);
        status = SNPY_EBADJ;
        goto change_state;
    }
    if (!kill(pid, 0)) {
        snpy_log(&xcore_log, SNPY_LOG_DEBUG, "plugin process pid: %d still running.\n", pid);
        return 0;
    }
   
    char arg_out[4096];
    
    if ((rc = kv_get_ival("meta/status", &status, wd_path)) ||
        (rc = kv_get_sval("meta/arg.out", arg_out, sizeof arg_out, wd_path))) {
        new_state = SNPY_UPDATE_SCHED_STATE(job->state,
                                            SNPY_SCHED_STATE_TERM);
        status = SNPY_EBADJ;
        goto change_state;
    }

    if ((rc = db_update_str_val(db_conn, "arg2", job->id, arg_out))) {
        new_state = SNPY_UPDATE_SCHED_STATE(job->state,
                                            SNPY_SCHED_STATE_TERM);
        status = SNPY_EDBCONN;
        goto change_state;
    }

    /*  complete successfully */
    new_state = SNPY_UPDATE_SCHED_STATE(job->state, SNPY_SCHED_STATE_TERM);
    status = 0;
    
change_state:
    return  snpy_job_update_state(db_conn, job,
                                  job->id, job->argv[0],
                                  job->state, new_state,
                                  status,
                                  NULL);
}

/*
 * proc_term() - handles term state
 *
 */

static int proc_term(MYSQL *db_conn, snpy_job_t *job) {
    int rc;
    int status;
    int new_state;
    char ext_err_msg[256]="";
    if (!job) {
        return -EINVAL;
    }

    new_state = SNPY_UPDATE_SCHED_STATE(job->state, SNPY_SCHED_STATE_DONE);
    status = job->result;
    snpy_wd_cleanup(job);

    return  snpy_job_update_state(db_conn, job,
                                  job->id, job->argv[0],
                                  job->state, new_state,
                                  status,
                                  "s", "ext_err_msg", ext_err_msg);

    return 0;
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

int import_proc (MYSQL *db_conn, int job_id) {  
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

