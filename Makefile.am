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
AM_LDFLAGS += -lrdkloggers -ldwnlutil -lfwutils -lsecure_wrapper -lparsejson -lpthread -lrbus

AM_CFLAGS += $(GLIB_CFLAGS)
# Build the RDK upgrade shared library
lib_LTLIBRARIES = librdksw_upgrade.la librdksw_rfcIntf.la librdksw_iarmIntf.la librdksw_jsonparse.la librdksw_flash.la librdksw_fwutils.la

librdksw_upgrade_la_SOURCES = \
    ${top_srcdir}/src/rdkv_upgrade.c\
    ${top_srcdir}/src/chunk.c
if USE_CPC_CODE
librdksw_upgrade_la_SOURCES += \
    ${top_srcdir}/src/cedmInterface-cpc/codebigUtils.c\
    ${top_srcdir}/src/cedmInterface-cpc/mtlsUtils.c
else
librdksw_upgrade_la_SOURCES += \
    ${top_srcdir}/src/cedmInterface/codebigUtils.c\
    ${top_srcdir}/src/cedmInterface/mtlsUtils.c
endif

librdksw_rfcIntf_la_SOURCES = \
    ${top_srcdir}/src/rfcInterface/rfcinterface.c

librdksw_iarmIntf_la_SOURCES = \
    ${top_srcdir}/src/iarmInterface/iarmInterface.c

librdksw_jsonparse_la_SOURCES = \
    ${top_srcdir}/src/json_process.c

librdksw_flash_la_SOURCES = \
    ${top_srcdir}/src/flash.c \
    ${top_srcdir}/src/download_status_helper.c

librdksw_fwutils_la_SOURCES = \
    ${top_srcdir}/src/deviceutils/deviceutils.c \
    ${top_srcdir}/src/deviceutils/device_api.c

librdksw_upgrade_la_CFLAGS = -fPIC -I${top_srcdir}/src/include -I${top_srcdir}/src/deviceutils  $(AM_CFLAGS)
librdksw_upgrade_la_CPPFLAGS = -fPIC -I${top_srcdir}/src/include -I${top_srcdir}/src/deviceutils
librdksw_rfcIntf_la_CFLAGS = -fPIC -I${top_srcdir}/src/include -I${top_srcdir}/src/deviceutils $(AM_CFLAGS)
librdksw_rfcIntf_la_CPPFLAGS = -fPIC -I${top_srcdir}/src/include -I${top_srcdir}/src/deviceutils
librdksw_iarmIntf_la_CFLAGS = -fPIC -I${top_srcdir}/src/include -I${top_srcdir}/src/deviceutils $(AM_CFLAGS)
librdksw_iarmIntf_la_CPPFLAGS = -fPIC -I${top_srcdir}/src/include -I${top_srcdir}/src/deviceutils
librdksw_jsonparse_la_CFLAGS = -fPIC -I${top_srcdir}/src/include -I${top_srcdir}/src/deviceutils $(AM_CFLAGS)
librdksw_jsonparse_la_CPPFLAGS = -fPIC -I${top_srcdir}/src/include -I${top_srcdir}/src/deviceutils
librdksw_flash_la_CFLAGS = -fPIC -I${top_srcdir}/src/include -I${top_srcdir}/src/deviceutils $(AM_CFLAGS)
librdksw_flash_la_CPPFLAGS = -fPIC -I${top_srcdir}/src/include -I${top_srcdir}/src/deviceutils
librdksw_fwutils_la_CFLAGS = -fPIC -I${top_srcdir}/src/include -I${top_srcdir}/src/deviceutils $(AM_CFLAGS)
librdksw_fwutils_la_CPPFLAGS = -fPIC -I${top_srcdir}/src/include -I${top_srcdir}/src/deviceutils

if IS_IARMEVENT_ENABLED
librdksw_iarmIntf_la_CFLAGS += $(IARM_EVENT_FLAG)
endif

if IS_LIBRFCAPI_ENABLED
librdksw_rfcIntf_la_CFLAGS += $(LIBRFCAPI_FLAG)
endif

if IS_IARMEVENT_ENABLED
librdksw_iarmIntf_la_CFLAGS += $(IARM_EVENT_FLAG)
endif

if USE_CPC_CODE
librdksw_upgrade_la_CFLAGS += -I${top_srcdir}/src/cedmInterface-cpc
librdksw_upgrade_la_CPPFLAGS += -I${top_srcdir}/src/cedmInterface-cpc
librdksw_fwutils_la_CFLAGS += -I${top_srcdir}/src/cedmInterface-cpc
librdksw_fwutils_la_CPPFLAGS += -I${top_srcdir}/src/cedmInterface-cpc
else
librdksw_upgrade_la_CFLAGS += -I${top_srcdir}/src/cedmInterface
librdksw_upgrade_la_CPPFLAGS += -I${top_srcdir}/src/cedmInterface
librdksw_fwutils_la_CFLAGS += -I${top_srcdir}/src/cedmInterface
librdksw_fwutils_la_CPPFLAGS += -I${top_srcdir}/src/cedmInterface
endif

# Configure for shared library generation with proper flags
librdksw_upgrade_la_LDFLAGS = -shared
librdksw_upgrade_la_LIBADD = $(AM_LDFLAGS)
librdksw_rfcIntf_la_LDFLAGS = -shared
if IS_LIBRFCAPI_ENABLED
librdksw_rfcIntf_la_LIBADD = -lrfcapi $(AM_LDFLAGS)
else
librdksw_rfcIntf_la_LIBADD = $(AM_LDFLAGS)
endif
librdksw_iarmIntf_la_LDFLAGS = -shared
if IS_IARMEVENT_ENABLED
librdksw_iarmIntf_la_LIBADD = -lIARMBus $(AM_LDFLAGS)
else
librdksw_iarmIntf_la_LIBADD = $(AM_LDFLAGS)
endif
librdksw_jsonparse_la_LDFLAGS = -shared
librdksw_jsonparse_la_LIBADD = $(AM_LDFLAGS)
librdksw_flash_la_LDFLAGS = -shared
librdksw_flash_la_LIBADD = $(AM_LDFLAGS)
librdksw_fwutils_la_LDFLAGS = -shared
librdksw_fwutils_la_LIBADD = $(AM_LDFLAGS)

# Library headers to install
librdksw_upgrade_include_HEADERS = \
    ${top_srcdir}/src/include/rdkv_upgrade.h
librdksw_rfcIntf_include_HEADERS = \
    ${top_srcdir}/src/include/rfcinterface.h
librdksw_iarmIntf_include_HEADERS = \
    ${top_srcdir}/src/include/iarmInterface.h
librdksw_jsonparse_include_HEADERS = \
    ${top_srcdir}/src/include/json_process.h
librdksw_flash_include_HEADERS = \
    ${top_srcdir}/src/include/flash.h
librdksw_fwutils_include_HEADERS = \
    ${top_srcdir}/src/deviceutils/deviceutils.h \
    ${top_srcdir}/src/deviceutils/device_api.h

librdksw_upgrade_includedir = ${includedir}
librdksw_rfcIntf_includedir = ${includedir}
librdksw_iarmIntf_includedir = ${includedir}
librdksw_jsonparse_includedir = ${includedir}
librdksw_flash_includedir = ${includedir}
librdksw_fwutils_includedir = ${includedir}

bin_PROGRAMS= rdkvfwupgrader

# Conditionally build rdkFwupdateMgr only if D-Bus daemon is enabled
if ENABLE_DBUS_DAEMON
bin_PROGRAMS += rdkFwupdateMgr
bin_PROGRAMS += testClient
endif


rdkvfwupgrader_SOURCES = \
    ${top_srcdir}/src/rdkv_main.c \
    ${top_srcdir}/src/chunk.c \
    ${top_srcdir}/src/device_status_helper.c \
    ${top_srcdir}/src/rbusInterface/rbusInterface.c

# Link against the RDK upgrade shared library AND new interface libraries
rdkvfwupgrader_LDADD = librdksw_jsonparse.la librdksw_upgrade.la librdksw_rfcIntf.la librdksw_iarmIntf.la librdksw_flash.la librdksw_fwutils.la $(cjson_LIBS) $(curl_LIBS) -lrdkloggers -ldwnlutil -lfwutils -lsecure_wrapper -lparsejson -lpthread -lrbus
rdkvfwupgrader_LDFLAGS = -L$(PKG_CONFIG_SYSROOT_DIR)/$(libdir)

rdkvfwupgrader_include_HEADERS = \
    ${top_srcdir}/src/rbusInterface/rbusInterface.h \
    ${top_srcdir}/src/include/rfcinterface.h \
    ${top_srcdir}/src/include/iarmInterface.h \
    ${top_srcdir}/src/include/device_status_helper.h

rdkvfwupgrader_CFLAGS = -I${top_srcdir}/src/include -I${top_srcdir}/src/deviceutils
rdkvfwupgrader_CPPFLAGS = -I${top_srcdir}/src/include -I${top_srcdir}/src/deviceutils


rdkvfwupgrader_includedir = ${includedir}

# D-Bus daemon binary - only build if enabled
if ENABLE_DBUS_DAEMON
rdkFwupdateMgr_SOURCES = \
    ${top_srcdir}/src/rdkFwupdateMgr.c\
    ${top_srcdir}/src/chunk.c \
    ${top_srcdir}/src/device_status_helper.c \
    ${top_srcdir}/src/rbusInterface/rbusInterface.c \
    ${top_srcdir}/src/dbus/rdkv_dbus_server.c \
    ${top_srcdir}/src/dbus/rdkFwupdateMgr_handlers.c
# Link against the RDK upgrade shared library AND new interface libraries
rdkFwupdateMgr_LDADD = librdksw_jsonparse.la librdksw_upgrade.la librdksw_rfcIntf.la librdksw_iarmIntf.la librdksw_flash.la librdksw_fwutils.la $(cjson_LIBS) $(curl_LIBS) -lrdkloggers -ldwnlutil -lfwutils -lsecure_wrapper -lparsejson -lpthread -lrbus $(GLIB_LIBS)

rdkFwupdateMgr_LDFLAGS = -L$(PKG_CONFIG_SYSROOT_DIR)/$(libdir)
rdkFwupdateMgr_include_HEADERS = \
    ${top_srcdir}/src/rbusInterface/rbusInterface.h \
    ${top_srcdir}/src/include/rfcinterface.h \
    ${top_srcdir}/src/include/iarmInterface.h \
    ${top_srcdir}/src/include/device_status_helper.h \
    ${top_srcdir}/src/dbus/rdkv_dbus_server.h \
    ${top_srcdir}/src/dbus/rdkFwupdateMgr_handlers.h

rdkFwupdateMgr_CFLAGS = -I${top_srcdir}/src/include -I${top_srcdir}/src/deviceutils -I${top_srcdir}/src/dbus $(GLIB_CFLAGS) $(ENABLE_DBUS)
rdkFwupdateMgr_CPPFLAGS = -I${top_srcdir}/src/include -I${top_srcdir}/src/deviceutils -I${top_srcdir}/src/dbus $(GLIB_CFLAGS) $(ENABLE_DBUS)

rdkFwupdateMgr_includedir = ${includedir}

# Test client binary for testing daemon registration scenarios
testClient_SOURCES = \
    ${top_srcdir}/src/test/testClient.c

# Test client only needs GLib/GDBus for D-Bus communication  
testClient_LDADD = $(GLIB_LIBS)
testClient_LDFLAGS = -L$(PKG_CONFIG_SYSROOT_DIR)/$(libdir)
testClient_CFLAGS = $(GLIB_CFLAGS)
testClient_CPPFLAGS = $(GLIB_CFLAGS)
endif

if INSTALL_TEST_FWUPGRADER
bin_PROGRAMS += testrdkvfwupgrader

testrdkvfwupgrader_SOURCES = \
     ${top_srcdir}/test/testiarmInterface.c

testrdkvfwupgrader_CFLAGS = $(AM_CFLAGS) $(TEST_FWUPGRADER_CFLAGS)
testrdkvfwupgrader_LDADD = -lIARMBus $(AM_LDFLAGS) librdksw_jsonparse.la

testrdkvfwupgrader_include_HEADERS = \
    ${top_srcdir}/test/testiarmInterface.h

testrdkvfwupgrader_includedir = ${includedir}
endif


if IS_TELEMETRY2_ENABLED
rdkvfwupgrader_CFLAGS +=  $(T2_EVENT_FLAG)
if ENABLE_DBUS_DAEMON
rdkFwupdateMgr_CFLAGS +=  $(T2_EVENT_FLAG)
endif
AM_LDFLAGS += -ltelemetry_msgsender -lt2utils
endif

if IS_LIBRFCAPI_ENABLED
rdkvfwupgrader_CFLAGS += $(LIBRFCAPI_FLAG)
if ENABLE_DBUS_DAEMON
rdkFwupdateMgr_CFLAGS += $(LIBRFCAPI_FLAG)
endif
AM_LDFLAGS += -lrfcapi
endif

if IS_IARMEVENT_ENABLED
rdkvfwupgrader_CFLAGS += $(IARM_EVENT_FLAG)
if ENABLE_DBUS_DAEMON
rdkFwupdateMgr_CFLAGS += $(IARM_EVENT_FLAG)
endif
testrdkvfwupgrader_CFLAGS = $(IARM_EVENT_FLAG)
AM_LDFLAGS += -lIARMBus
endif

if IS_LIBRDKCERTSEL_ENABLED
librdksw_upgrade_la_CFLAGS += $(LIBRDKCERTSEL_FLAG)
rdkvfwupgrader_CFLAGS += $(LIBRDKCERTSEL_FLAG)
if ENABLE_DBUS_DAEMON
rdkFwupdateMgr_CFLAGS += $(LIBRDKCERTSEL_FLAG)
endif
AM_LDFLAGS += -lRdkCertSelector
endif

if IS_LIBRDKCONFIG_ENABLED
rdkvfwupgrader_CFLAGS += $(LIBRDKCONFIG_FLAG)
if ENABLE_DBUS_DAEMON
rdkFwupdateMgr_CFLAGS += $(LIBRDKCONFIG_FLAG)
endif
AM_LDFLAGS += -lrdkconfig
endif
