/*
 * Copyright 2023 Comcast Cable Communications Management, LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef VIDEO_TESTIARMINTERFACE_IARMINTERFACE_H_
#define VIDEO_TESTIARMINTERFACE_IARMINTERFACE_H_

#include <stdio.h>
#include <stdlib.h>
#ifndef CONTAINER_COVERITY_ENABLE
#include "sysMgr.h"
#include "libIARMCore.h"
#include "libIBus.h"
#include "libIBusDaemon.h"
#include <glib.h>
#endif
//#ifdef EN_MAINTENANCE_MANAGER
//#include "maintenanceMGR.h"
//#endif

//#define IARM_RDKVFWUPGRADER_EVENT "RdkvFWupgrader"
#define IARM_BUS_RDKVFWUPGRADER_MODECHANGED 0

void eventManagerTest(const char *cur_event_name, int app_mode) ;

#endif /* VIDEO_TESTIARMINTERFACE_IARMINTERFACE_H_ */
