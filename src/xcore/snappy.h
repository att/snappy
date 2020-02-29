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

#ifndef SNPY_H
#define SNPY_H

#include "dictionary.h"

#define SNPY_LOG_SIZE 4096

#define SNPY_MAX_ARGS 8


typedef struct snpy_job {
    int id;
    int sub;
    int next;

    int parent;
    int grp;
    int root;
    int state;

    int result;
    int policy;
    char *feid;
    int feid_size;
    char *log;
    char log_size;
    char *argv[SNPY_MAX_ARGS];
    int argv_size[SNPY_MAX_ARGS];
    int buf_size;
    int alloc_size;
    char buf[0];
} snpy_job_t;


extern dictionary *snpy_conf;

enum snpy_state_bit {
    SNPY_STATE_BIT_CREATED = 0,
    SNPY_STATE_BIT_DONE = 1,
    SNPY_STATE_BIT_READY = 2,
    SNPY_STATE_BIT_RUN = 3,
    SNPY_STATE_BIT_BLOCKED = 4,
    SNPY_STATE_BIT_TERM = 5
}; 

#define BIT(nr) (1UL << (nr))

#define SCHED_STATE_MASK 0xFF
#define EXTRA_STATE_MASK 0xFFFFFF00

#define SNPY_GET_SCHED_STATE(x) ((x)&SCHED_STATE_MASK)

#define SNPY_UPDATE_SCHED_STATE(old_state, new_sched_state) \
    (( (old_state) & EXTRA_STATE_MASK) | (new_sched_state))

enum snpy_job_sched_state {
    SNPY_SCHED_STATE_CREATED = 1 << SNPY_STATE_BIT_CREATED,
    SNPY_SCHED_STATE_DONE = 1 << SNPY_STATE_BIT_DONE,
    SNPY_SCHED_STATE_READY = 1 << SNPY_STATE_BIT_READY,
    SNPY_SCHED_STATE_RUN = 1 << SNPY_STATE_BIT_RUN,
    SNPY_SCHED_STATE_BLOCKED = 1 << SNPY_STATE_BIT_BLOCKED,
    SNPY_SCHED_STATE_TERM = 1 << SNPY_STATE_BIT_TERM
};

static const char * job_state_msg[] = {
    "created",
    "done",
    "ready",
    "running",
    "blocked",
    "terminated"
};

#define SNPY_EBASE 0x1c61862a

enum snpy_error {
    SNPY_EDBCONN = SNPY_EBASE,
    SNPY_EINVREC,
    SNPY_ENOPROC,
    SNPY_EBADJ,
    SNPY_EENVJ,
    SNPY_ESPAWNJ,
    SNPY_ESTATJ,
    SNPY_EPROC,
    SNPY_ESUB,
    SNPY_ENEXT,
    SNPY_EPLUG,
    SNPY_EARG,
    SNPY_ECONF,
    SNPY_EINVPLUG,
    SNPY_EAMBIPLUG,
    SNPY_ENOPLUG,
    SNPY_EINCOMPARG,
    SNPY_ELOG,
    SNPY_ERESPOOLFUL,
    SNPY_ENOIMPL,
    SNPY_ELAST
};

#endif
