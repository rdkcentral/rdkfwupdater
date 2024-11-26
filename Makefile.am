# Copyright 2023 Comcast Cable Communications Management, LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# SPDX-License-Identifier: Apache-2.0
#

AM_CFLAGS = $(TRACE_CFLAGS)

AM_CFLAGS += -Wall -Werror $(cjson_CFLAGS) $(curl_CFLAGS) $(CFLAGS)
AM_LDFLAGS = -L$(PKG_CONFIG_SYSROOT_DIR)/$(libdir)
AM_LDFLAGS += $(cjson_LIBS) $(curl_LIBS)
AM_LDFLAGS += -lrfcapi -ltelemetry_msgsender -lt2utils -lIARMBus -lrdkloggers -ldwnlutil -lfwutils -lsecure_wrapper -lparsejson -lpthread
if IS_LIBRDKCONFIG_ENABLED
AM_CFLAGS += $(LIBRDKCONFIG_FLAG)
AM_LDFLAGS += -lrdkconfig
endif

bin_PROGRAMS= rdkvfwupgrader
bin_PROGRAMS+= testrdkvfwupgrader

rdkvfwupgrader_SOURCES = \
    ${top_srcdir}/src/rdkv_main.c \
    ${top_srcdir}/src/chunk.c \
    ${top_srcdir}/src/flash.c \
    ${top_srcdir}/src/json_process.c \
    ${top_srcdir}/src/device_status_helper.c \
    ${top_srcdir}/src/download_status_helper.c \
    ${top_srcdir}/src/iarmInterface/iarmInterface.c \
    ${top_srcdir}/src/rfcInterface/rfcinterface.c \
    ${top_srcdir}/src/deviceutils/device_api.c \
    ${top_srcdir}/src/deviceutils/deviceutils.c

rdkvfwupgrader_include_HEADERS = \
    ${top_srcdir}/src/rfcInterface/rfcinterface.h \
    ${top_srcdir}/src/iarmInterface/iarmInterface.h \
    ${top_srcdir}/src/deviceutils/device_api.h \
    ${top_srcdir}/src/deviceutils/deviceutils.h \
    ${top_srcdir}/src/include/device_status_helper.h

rdkvfwupgrader_CFLAGS = -I${top_srcdir}/src/include -I${top_srcdir}/src/deviceutils
rdkvfwupgrader_CPPFLAGS = -I${top_srcdir}/src/include -I${top_srcdir}/src/deviceutils

if USE_CPC_CODE
rdkvfwupgrader_SOURCES += \
    ${top_srcdir}/src/cedmInterface-cpc/codebigUtils.c \
    ${top_srcdir}/src/cedmInterface-cpc/mtlsUtils.c

rdkvfwupgrader_include_HEADERS += \
    ${top_srcdir}/src/cedmInterface-cpc/codebigUtils.h \
    ${top_srcdir}/src/cedmInterface-cpc/mtlsUtils.h

rdkvfwupgrader_CFLAGS += -I${top_srcdir}/src/cedmInterface-cpc
rdkvfwupgrader_CPPFLAGS += -I${top_srcdir}/src/cedmInterface-cpc
else
rdkvfwupgrader_SOURCES += \
    ${top_srcdir}/src/cedmInterface/codebigUtils.c \
    ${top_srcdir}/src/cedmInterface/mtlsUtils.c

rdkvfwupgrader_include_HEADERS += \
    ${top_srcdir}/src/cedmInterface/codebigUtils.h \
    ${top_srcdir}/src/cedmInterface/mtlsUtils.h

rdkvfwupgrader_CFLAGS += -I${top_srcdir}/src/cedmInterface
rdkvfwupgrader_CPPFLAGS += -I${top_srcdir}/src/cedmInterface

endif

rdkvfwupgrader_includedir = ${includedir}

testrdkvfwupgrader_SOURCES = \
     ${top_srcdir}/test/testiarmInterface.c

testrdkvfwupgrader_include_HEADERS = \
    ${top_srcdir}/test/testiarmInterface.h

testrdkvfwupgrader_includedir = ${includedir}
