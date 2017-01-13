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

#include <string.h>


#include "snappy.h"
#include "db.h"
#include "proc.h"




static proc_tab_entry_t proc_tab[128] = {
    { "bk_single_sched", bk_single_sched_proc },
    { "bk_single_full", bk_single_full_proc },
//    { "bk_single_incr", bk_single_incr_proc },
    { "rstr_single", rstr_single_proc},
    { "snap", snap_proc},
    { "export", export_proc},
    { "import", import_proc},
//    { "diff", diff_proc},
//   { "patch", patch_proc},
    { "put", put_proc},
    { "get", get_proc},
    { "proc_tab_end", NULL}
};



job_proc_t proc_get_job_proc(const char *proc_name) {
    int i = 0;
    for ( i = 0; i < (sizeof proc_tab) / (sizeof proc_tab[0]); i ++) {          
        if (!strcmp("proc_tab_end", proc_tab[i].name)) {
            return NULL;
        }
        if (!strcmp(proc_name, proc_tab[i].name)) {
            return proc_tab[i].proc;
        }
    }
    return NULL;
}


