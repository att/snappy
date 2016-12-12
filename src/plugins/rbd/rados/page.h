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

#ifndef CEPH_PAGE_H
#define CEPH_PAGE_H

namespace ceph {
  // these are in common/page.cc
  extern unsigned _page_size;
  extern unsigned long _page_mask;
  extern unsigned _page_shift;
}

#endif


#define CEPH_PAGE_SIZE ceph::_page_size
#define CEPH_PAGE_MASK ceph::_page_mask
#define CEPH_PAGE_SHIFT ceph::_page_shift


