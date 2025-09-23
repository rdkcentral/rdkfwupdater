

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
AM_LDFLAGS += -lrdkloggers -ldwnlutil -lfwutils -lsecure_wrapper -lparsejson -lpthread

# Build the RDK upgrade shared library
lib_LTLIBRARIES = librdk_upgrade.la librdk_rfcIntf.la librdk_iarmIntf.la

librdk_upgrade_la_SOURCES = \
    ${top_srcdir}/src/rdkv_upgrade.c

librdk_rfcIntf_la_SOURCES = \
    ${top_srcdir}/src/rfcInterface/rfcinterface.c

librdk_iarmIntf_la_SOURCES = \
    ${top_srcdir}/src/iarmInterface/iarmInterface.c

librdk_upgrade_la_CFLAGS = -fPIC -I${top_srcdir}/src/include -I${top_srcdir}/src/deviceutils -I${top_srcdir}/src/iarmInterface -I${top_srcdir}/src/rfcInterface $(AM_CFLAGS)
librdk_upgrade_la_CPPFLAGS = -fPIC -I${top_srcdir}/src/include -I${top_srcdir}/src/deviceutils -I${top_srcdir}/src/iarmInterface -I${top_srcdir}/src/rfcInterface
librdk_rfcIntf_la_CFLAGS = -fPIC -I${top_srcdir}/src/include $(AM_CFLAGS)
librdk_iarmIntf_la_CFLAGS = -fPIC -I${top_srcdir}/src/include -I${top_srcdir}/src/deviceutils $(AM_CFLAGS)
librdk_iarmIntf_la_CPPFLAGS = -fPIC -I${top_srcdir}/src/iarmInterface

if IS_IARMEVENT_ENABLED
librdk_iarmIntf_la_CFLAGS += $(IARM_EVENT_FLAG)
endif
if IS_LIBRFCAPI_ENABLED
librdk_rfcIntf_la_CFLAGS += $(LIBRFCAPI_FLAG)
librdk_rfcIntf_la_CPPFLAGS = -fPIC -I${top_srcdir}/src/include
endif

if USE_CPC_CODE
librdk_upgrade_la_CFLAGS += -I${top_srcdir}/src/cedmInterface-cpc
librdk_upgrade_la_CPPFLAGS += -I${top_srcdir}/src/cedmInterface-cpc
else
librdk_upgrade_la_CFLAGS += -I${top_srcdir}/src/cedmInterface
librdk_upgrade_la_CPPFLAGS += -I${top_srcdir}/src/cedmInterface
endif

# Configure for shared library generation with proper flags
librdk_upgrade_la_LDFLAGS = -version-info 1:0:0 -shared
librdk_upgrade_la_LIBADD = $(AM_LDFLAGS)
librdk_rfcIntf_la_LDFLAGS = -version-info 1:0:0 -shared
librdk_rfcIntf_la_LIBADD = $(AM_LDFLAGS)
librdk_iarmIntf_la_LDFLAGS = -version-info 1:0:0 -shared
librdk_iarmIntf_la_LIBADD = $(AM_LDFLAGS)

# Library headers to install
librdk_upgrade_include_HEADERS = \
    ${top_srcdir}/src/include/rdkv_upgrade.h
librdk_rfcIntf_include_HEADERS = \
    ${top_srcdir}/src/rfcInterface/rfcinterface.h
librdk_iarmIntf_include_HEADERS = \
    ${top_srcdir}/src/iarmInterface/iarmInterface.h

librdk_upgrade_includedir = ${includedir}
librdk_rfcIntf_includedir = ${includedir}
librdk_iarmIntf_includedir = ${includedir}

bin_PROGRAMS= rdkvfwupgrader


rdkvfwupgrader_SOURCES = \
    ${top_srcdir}/src/rdkv_main.c \
    ${top_srcdir}/src/chunk.c \
    ${top_srcdir}/src/flash.c \
    ${top_srcdir}/src/json_process.c \
    ${top_srcdir}/src/device_status_helper.c \
    ${top_srcdir}/src/download_status_helper.c \
    ${top_srcdir}/src/deviceutils/device_api.c \
    ${top_srcdir}/src/deviceutils/deviceutils.c

# Link against the RDK upgrade shared library AND new interface libraries
rdkvfwupgrader_LDADD = librdk_upgrade.la librdk_rfcIntf.la librdk_iarmIntf.la $(cjson_LIBS) $(curl_LIBS) -lrdkloggers -ldwnlutil -lfwutils -lsecure_wrapper -lparsejson -lpthread -lrbus
rdkvfwupgrader_LDFLAGS = -L$(PKG_CONFIG_SYSROOT_DIR)/$(libdir)

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

if INSTALL_TEST_FWUPGRADER
bin_PROGRAMS += testrdkvfwupgrader

testrdkvfwupgrader_SOURCES = \
     ${top_srcdir}/test/testiarmInterface.c

testrdkvfwupgrader_CFLAGS = $(AM_CFLAGS) $(TEST_FWUPGRADER_CFLAGS)

testrdkvfwupgrader_include_HEADERS = \
    ${top_srcdir}/test/testiarmInterface.h

testrdkvfwupgrader_includedir = ${includedir}
endif


if IS_TELEMETRY2_ENABLED
rdkvfwupgrader_CFLAGS +=  $(T2_EVENT_FLAG)
AM_LDFLAGS += -ltelemetry_msgsender -lt2utils
endif

if IS_LIBRFCAPI_ENABLED
rdkvfwupgrader_CFLAGS += $(LIBRFCAPI_FLAG)
librdk_rfcIntf_la_LIBADD = -lrfcapi $(AM_LDFLAGS)
endif

if IS_IARMEVENT_ENABLED
rdkvfwupgrader_CFLAGS += $(IARM_EVENT_FLAG)
testrdkvfwupgrader_CFLAGS = $(IARM_EVENT_FLAG)
librdk_iarmIntf_la_LIBADD = -lIARMBus $(AM_LDFLAGS)
endif

if IS_LIBRDKCONFIG_ENABLED
rdkvfwupgrader_CFLAGS += $(LIBRDKCONFIG_FLAG)
AM_LDFLAGS += -lrdkconfig
endif

if IS_LIBRDKCERTSEL_ENABLED
rdkvfwupgrader_CFLAGS += $(LIBRDKCERTSEL_FLAG)
AM_LDFLAGS += -lRdkCertSelector
endif
