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

#ifndef CEPH_RBD_FEATURES_H
#define CEPH_RBD_FEATURES_H

#define RBD_FEATURE_LAYERING      (1<<0)
#define RBD_FEATURE_STRIPINGV2    (1<<1)

#define RBD_FEATURES_INCOMPATIBLE (RBD_FEATURE_LAYERING|RBD_FEATURE_STRIPINGV2)
#define RBD_FEATURES_ALL          (RBD_FEATURE_LAYERING|RBD_FEATURE_STRIPINGV2)

#endif
