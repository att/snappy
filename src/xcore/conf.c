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

#include <errno.h>

#include "conf.h"
#include "snpy_util.h"
#include "snappy.h"



dictionary *snpy_conf = NULL;

int conf_init(const char *snpy_conf_file) {
    const char *conf_locs[] = {
        "./snappy.conf",
        "/etc/snappy.conf",
        "/var/lib/snappy/etc/snappy.conf"
    };
    int i;
    if (snpy_conf_file) 
        goto load_conf_file;

    /* choose conf file location based on priority */
    for (i = 0; i < ARRAY_SIZE(conf_locs); i ++) {
        if (!access(conf_locs[i], R_OK))
            snpy_conf_file = conf_locs[i];
    }
    
    if (!snpy_conf_file)
        return -SNPY_ECONF;

load_conf_file:

    snpy_conf = ciniparser_load(snpy_conf_file);
    if (!snpy_conf)
        return -SNPY_ECONF;
    return 0;
}

const char *conf_get_xcore_home(void) {
    return ciniparser_getstring(snpy_conf, "xcore:broker_home",
                                "/var/lib/snappy");
}

const char *conf_get_plugin_home(void) {
    return ciniparser_getstring(snpy_conf, "plugin:plugin_home", 
                                "/var/lib/snappy/plugins");
}

const char *conf_get_run(void) {
    return  ciniparser_getstring(snpy_conf, "xcore:run_path", 
                                 "/var/lib/snappy/run");
}

const char *conf_get_log(void) {
    return  ciniparser_getstring(snpy_conf, "xcore:log", 
                                 "/var/lib/snappy/run/xcore.log");
}
void conf_deinit(void) {
    free(snpy_conf);

}
