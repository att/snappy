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

#ifndef SNPY_PLUGIN_H
#define SNPY_PLUGIN_H

#include "ciniparser.h"

struct plugin {
    int id;
    const char *name;
    dictionary *info;

};


int plugin_tbl_init(void);
struct plugin *plugin_srch_by_name(const char *name);
struct plugin *plugin_srch_by_id(int id);
const char *plugin_get_exec(struct plugin *pi);
#endif
