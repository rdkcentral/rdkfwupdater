# Copyright 2025 Comcast Cable Communications Management, LLC
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

# Makefile for librdkFwupdateMgr - Client library for firmware update registration

# Compiler flags
AM_CFLAGS = -Wall -Werror -fPIC
AM_CFLAGS += $(GLIB_CFLAGS) $(GIO_CFLAGS)
AM_CFLAGS += -I$(top_srcdir)/librdkFwupdateMgr/include
AM_CFLAGS += -I$(top_srcdir)/librdkFwupdateMgr/src

AM_LDFLAGS = -L$(PKG_CONFIG_SYSROOT_DIR)/$(libdir)
AM_LDFLAGS += $(GLIB_LIBS) $(GIO_LIBS)
AM_LDFLAGS += -lpthread

# Build the client library
lib_LTLIBRARIES = librdkFwupdateMgr.la

# Library sources
librdkFwupdateMgr_la_SOURCES = \
    $(top_srcdir)/librdkFwupdateMgr/src/rdkFwupdateMgr_process.c \
    $(top_srcdir)/librdkFwupdateMgr/src/rdkFwupdateMgr_log.c

# Library flags
librdkFwupdateMgr_la_CFLAGS = $(AM_CFLAGS)
librdkFwupdateMgr_la_LDFLAGS = -shared -version-info 1:0:0
librdkFwupdateMgr_la_LIBADD = $(AM_LDFLAGS)

# Public headers to install
librdkFwupdateMgr_include_HEADERS = \
    $(top_srcdir)/librdkFwupdateMgr/include/rdkFwupdateMgr_process.h \
    $(top_srcdir)/librdkFwupdateMgr/include/rdkFwupdateMgr_client.h

librdkFwupdateMgr_includedir = $(includedir)/rdkFwupdateMgr

# Example program
noinst_PROGRAMS = example_plugin_registration

example_plugin_registration_SOURCES = \
    $(top_srcdir)/librdkFwupdateMgr/examples/example_plugin_registration.c

example_plugin_registration_CFLAGS = $(AM_CFLAGS)
example_plugin_registration_LDADD = librdkFwupdateMgr.la $(AM_LDFLAGS)
example_plugin_registration_LDFLAGS = -L.

# Extra files to distribute
EXTRA_DIST = \
    $(top_srcdir)/librdkFwupdateMgr/QUICK_START.md \
    $(top_srcdir)/librdkFwupdateMgr/PROCESS_REGISTRATION_API.md \
    $(top_srcdir)/librdkFwupdateMgr/IMPLEMENTATION_README.md
