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

#include "stringbuilder.h"
#include "ciniparser.h"
#include "json.h"
#include "snpy_util.h"
#include "snpy_data_tag.h" 


#include "snappy.h"
#include "db.h"
#include "arg.h"
#include "log.h"
#include "job.h"
#include "error.h"
#include "conf.h"
#include "plugin.h"

#include "export.h"


struct plugin_env {
    char wd[PATH_MAX];
    char entry_pt[PATH_MAX];
};


static int proc_created(MYSQL *db_conn, snpy_job_t *job);
static int proc_ready(MYSQL *db_conn, snpy_job_t *job);
static int proc_blocked(MYSQL *db_conn, snpy_job_t *job);
static int proc_term(MYSQL *db_conn, snpy_job_t *job);

static int plugin_chooser(snpy_job_t *job, struct plugin **pi); 

//static int plugin_env_init(struct plugin_env *env, snpy_job_t *job);

static int job_get_wd(int job_id, char *wd, int wd_size);


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
    int rc = 0, status = 0;
    if (!job || !pi)
        return -EINVAL;
    int error;
    struct json *js = json_open(JSON_F_NONE, &error);
    if (!js)
        return -SNPY_EARG;
    if ((rc = json_loadstring(js, job->argv[2]))) {
        json_close(js);
        return -SNPY_EARG;
    }
    const char *pi_name = json_string(js, ".tp_name");
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
    status = plugin_choose(job->argv[2], NULL, &pi);
    if (status) 
        return -status;
    int rc = snprintf(pi_exec_path, pi_exec_path_len, "%s/%s/%s",
                      conf_get_plugin_home(), pi->name, plugin_get_exec(pi));
    if (rc >= pi_exec_path_len) 
        return -ENAMETOOLONG;
    if (!access(pi_exec_path, X_OK)) 
        return -errno;
    return 0;
}


static int get_src_plug_id(const char *arg) {
    /*TODO */
    return 0;
}
static int get_tgt_plug_id(const char *arg) {

    /*TODO */
    return 0;
}
static int get_src_plug_ver(const char *arg) {

    /*TODO */
    return 0;
}
static int get_tgt_plug_ver(const char *arg) {

    /*TODO */
    return 0;
}

static int get_snap_ts(const char *buf) {
    return 0;
}

static int get_env_init(MYSQL *db_conn, snpy_job_t *job) {
    int rc;
    int status = 0;
    char wd[PATH_MAX] = "";
    int wd_fd;
    const char *run_dir = conf_get_run();
    if (job_get_wd(job->id, wd, PATH_MAX)) 
        return -SNPY_ECONF;
    struct stat wd_st;
    if (!lstat(wd, &wd_st) && S_ISDIR(wd_st.st_mode)) {
        syslog(LOG_DEBUG, "working directory exists, trying cleanup.\n");
        if ((rc = rmdir_recurs(wd))) 
            return rc;
    }

    /* setting up directories */
    if ((rc = mkdir(wd, 0700))) 
        return -errno;
    if(((wd_fd = open(wd, O_RDONLY)) == -1) ||
        (rc = mkdirat(wd_fd, "meta", 0700)) || 
        (rc = mkdirat(wd_fd, "data", 0700))) {

        status = errno;
        goto free_wd_fd;
    }
    
    /* setup id */
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
    
    if ((rc = kv_put_sval("meta/arg", 
                          job->argv[2], job->argv_size[2], wd))) {
        
        status = -rc;
        goto free_wd_fd;
    }

    if ((rc = kv_put_sval("meta/rstr_arg", 
                          job->argv[1], job->argv_size[1], wd))) {
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
    char wd[PATH_MAX]="";
    char exec[PATH_MAX]="";
    char ext_err_msg[256]="";
    

    if ((status = job_get_wd(job->id, wd, sizeof wd))) 
        return -status;
    if ((status = job_get_plugin_exec(job, exec, sizeof exec))) 
        return -status;

    if((rc = get_env_init(db_conn, job))) {
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



static int add_job_import(MYSQL *db_conn, snpy_job_t *job) {
    int rc;
    
    snpy_job_t import;
    memset(&import, 0, sizeof import);
   
    /* TODO: add more job state check */

    if ((rc = db_insert_new_job(db_conn)) < 0)
        return rc;
    import.id = rc;
    import.sub = 0; import.next = 0; import.parent = job->id;
    import.grp = job->grp; /* set sub group id */
    import.root = job->root;
    import.state = SNPY_SCHED_STATE_CREATED;
    import.result = 0;
    import.policy = BIT(0) | BIT(2); /* arg0, arg2 */
 
    /* update import job */
    if((rc = db_update_job_partial(db_conn, &import)) ||
       (rc = db_update_str_val(db_conn, "feid", import.id, job->feid)) ||
       (rc = db_update_str_val(db_conn, "arg0", import.id, "import")) ||
       (rc = db_update_str_val(db_conn, "arg2", import.id, job->argv[2]))) 

        return rc;
    
    /* set import job as the sub job of current get job  */
    if ((rc = db_update_int_val(db_conn, "sub", job->id, import.id)))
        return rc;
    return snpy_job_update_state(db_conn, &import,
                                 job->id, job->argv[0],
                                 0, SNPY_SCHED_STATE_CREATED,
                                 0, 
                                 NULL);
      
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

    if ( (rc = job_get_wd(job->id, wd_path, sizeof wd_path))) {
        new_state = SNPY_UPDATE_SCHED_STATE(job->state, SNPY_SCHED_STATE_TERM);
        status = SNPY_EBADJ; 
        snprintf(ext_err_msg, sizeof ext_err_msg,
                 "error getting working dir, code: %d.", rc);
        goto change_state;
    }

    if ((rc = kv_get_ival("meta/pid", &pid, wd_path))) {
               
        new_state = SNPY_UPDATE_SCHED_STATE(job->state, SNPY_SCHED_STATE_TERM);
        status = SNPY_EBADJ;
        snprintf(ext_err_msg, sizeof ext_err_msg,
                 "get plugin pid error, code: %d.", rc);
        goto change_state;
    }

    if (!kill(pid, 0)) {
        syslog(LOG_DEBUG, "plugin process pid: %d still running.\n", pid);
        return 0;
    }
    
    char arg_out[4096];
    
    if ((rc = kv_get_ival("meta/status", &status, wd_path)) ||
        (rc = kv_get_sval("meta/arg.out", arg_out, sizeof arg_out, wd_path))) {
        new_state = SNPY_UPDATE_SCHED_STATE(job->state,
                                            SNPY_SCHED_STATE_TERM);
        status = SNPY_EBADJ;

        snprintf(ext_err_msg, sizeof ext_err_msg,
                 "error getting meta/status or arg.out, code: %d.", rc);
        goto change_state;
    }

    if ((rc = db_update_str_val(db_conn, "arg2", job->id, arg_out))) {
        new_state = SNPY_UPDATE_SCHED_STATE(job->state,
                                            SNPY_SCHED_STATE_TERM);
        status = SNPY_EDBCONN;
        goto change_state;
    }

    /* export complete successfully */
    rc = add_job_import(db_conn, job); /*add put job as the next job */
    if (rc) {
        new_state = SNPY_UPDATE_SCHED_STATE(job->state,
                                            SNPY_SCHED_STATE_TERM);
        status = SNPY_EDBCONN;
        snprintf(ext_err_msg, sizeof ext_err_msg,
                 "error add put job as the sub job: %d.", rc); 
        goto change_state;
    }

    new_state = SNPY_UPDATE_SCHED_STATE(job->state, SNPY_SCHED_STATE_TERM);
    status = 0;
    
change_state:
    return  snpy_job_update_state(db_conn, job,
                                  job->id, job->argv[0],
                                  job->state, new_state,
                                  status,
                                  "s", "ext_err_msg", ext_err_msg);
}

/*
 * proc_term() - waiting for put job to finish
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
    /* if there is a sub job, check sub status */
    if (job->sub) {
        int import_done=0, import_result=0;
        rc = db_get_ival(db_conn, "done", job->sub, &import_done) || 
            db_get_ival(db_conn, "result", job->sub, &import_result);
        if (rc) {
            snprintf(ext_err_msg, sizeof ext_err_msg,
                     "error getting import status: %d.", rc);
            return -SNPY_EDBCONN;
        }

        if (!import_done)
            return 0;

        if (import_result) {
            status = SNPY_ESUB;
            snprintf(ext_err_msg, sizeof ext_err_msg,
                     "error in sub job with id: %d.", job->sub);
        }

    }
    
    snpy_wd_cleanup(job);

    return  snpy_job_update_state(db_conn, job,
                                  job->id, job->argv[0],
                                  job->state, new_state,
                                  status,
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

int get_proc (MYSQL *db_conn, int job_id) {  
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

