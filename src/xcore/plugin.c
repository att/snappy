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

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>

#include <limits.h>

#include "plugin.h"
#include "snappy.h"
#include "ciniparser.h"
#include "stringbuilder.h"
#include "util.h"


#define PLUG_NAME_LEN 64

static struct plugin plugin_tbl[64];

int plugin_tbl_init(void) {
    int rc = 0;
    const char *plugin_home = 
        ciniparser_getstring(snpy_conf, "plugin:plugin_home",
                             "/var/lib/snappy/plugins");

    DIR *dir = opendir(plugin_home);

    if (!dir) {
        perror("opendir");
        return -errno;
    }
    int i;
    for (i = 0; i < ARRAY_SIZE(plugin_tbl); ) {
        struct dirent *ent = readdir(dir);
        if (!ent) 
            break;
        if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, "..")) 
            continue;
        if (ent->d_type != DT_DIR) 
            continue;
        char info_path[PATH_MAX] = "";
        rc = stringbuilder(info_path, sizeof info_path, "/",
                           plugin_home, ent->d_name, "info");
        dictionary *info = ciniparser_load(info_path);
        if (!info) 
            continue;
        const char *name =  ciniparser_getstring(info, ":name", "");
        if (!name)
            continue;
        plugin_tbl[i].info = info;
        plugin_tbl[i].name = name;
        i++;
    }
    
    closedir(dir);
    return 0;
}

int plugin_tbl_deinit(void) {
    int i;
    for (i = 0; i < ARRAY_SIZE(plugin_tbl); i ++) {
        if (plugin_tbl[i].info)
            ciniparser_freedict(plugin_tbl[i].info);
    }
    return 0;
}

struct plugin *plugin_srch(const char *name) {
    int i;
    if (!name)
        return NULL;
    for (i = 0; i < ARRAY_SIZE(plugin_tbl); i ++) {
        if (!strncmp(name, plugin_tbl[i].name, PLUG_NAME_LEN)) 
            return &plugin_tbl[i];
    }
    return NULL;
}

const char *plugin_get_exec(struct plugin *pi) {
    if (!pi || !pi->info) 
        return "";
    return ciniparser_getstring(pi->info, ":exec", "");
}


#if 0

int main(void) {
    int i;
    snpy_load_conf(NULL);
    plugin_tbl_init();
    for (i = 0; i < ARRAY_SIZE(plugin_tbl); i++) {
        if (plugin_tbl[i].name)
            printf("plugin: %s\n", plugin_tbl[i].name);
    }
    plugin_tbl_deinit();
    snpy_free_conf();
    return 0;
}
#endif 
