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

#ifndef CEPH_RADOS_TYPES_H
#define CEPH_RADOS_TYPES_H

#include <stdint.h>

/**
 * @struct obj_watch_t
 * One item from list_watchers
 */
struct obj_watch_t {
  char addr[256];
  int64_t watcher_id;
  uint64_t cookie;
  uint32_t timeout_seconds;
}; 

#endif
