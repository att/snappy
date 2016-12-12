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
#include <errno.h>

#include "util.h"
#include "error.h"
#include "json.h"


int snpy_strerror(int errnum, char *buf, int buf_size) {
    if (!buf) 
        return -EINVAL;
    buf[0] = 0;
    if (errnum >= JSON_EBASE && errnum < JSON_ELAST) {
        if(strlcpy(buf, json_strerror(errnum), buf_size) >= buf_size)
            return -ERANGE;
        else 
            return 0;
    }
    if (errnum >= SNPY_EBASE && errnum < SNPY_ELAST) {
        int rc = strlcpy(buf, snpy_errmsg_tab[errnum - SNPY_EBASE], buf_size); 
        if ( rc >= buf_size)
            return -ERANGE;
        else 
            return 0;
    }
    else 
        return strerror_r(errnum, buf, buf_size);
}


