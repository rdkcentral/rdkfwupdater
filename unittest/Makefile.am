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

AUTOMAKE_OPTIONS = subdir-objects

# Define the program name and the source files
bin_PROGRAMS = rdkfw_device_status_gtest rdkfw_deviceutils_gtest rdkfw_main_gtest rdkfw_interface_gtest rdkfwupdatemgr_main_flow_gtest rdkFwupdateMgr_handlers_gtest

# Define the include directories
# NOTE: We explicitly use -I. to prioritize local test headers over system headers
# This prevents conflicts with external library headers in /usr/local/include/
# rdk_fwdl_utils.h includes rdkv_cdl_log_wrapper.h inside common_utilities. to over come this ,  
COMMON_CPPFLAGS = -std=c++11 -I. -I/usr/include/cjson -I../src -I../src/cedmInterface -I../src/deviceutils/ -I../src/include/ -I../src/rbusInterface/ -I./mocks/ -I./deviceutils/ -DGTEST_ENABLE -DIARM_ENABLED -DRFC_API_ENABLED -DT2_EVENT_ENABLED

# GLib/GIO flags for D-Bus handler tests
GLIB_CFLAGS := $(shell pkg-config --cflags glib-2.0 gio-2.0)
GLIB_LIBS := $(shell pkg-config --libs glib-2.0 gio-2.0)

# Define the libraries to link against
COMMON_LDADD =  -lgtest -lgtest_main -lgmock_main -lgmock -lcjson -lgcov

# Define the compiler flags
COMMON_CXXFLAGS = -frtti -fprofile-arcs -ftest-coverage -fpermissive

# Define the source files
rdkfw_device_status_gtest_SOURCES = device_status_helper_gtest.cpp ./mocks/device_status_helper_mock.cpp ../src/device_status_helper.c ../src/download_status_helper.c

rdkfw_deviceutils_gtest_SOURCES = deviceutils/device_api_gtest.cpp deviceutils/deviceutils_gtest.cpp deviceutils/json_parse.c ./mocks/deviceutils_mock.cpp ../src/deviceutils/device_api.c ../src/deviceutils/deviceutils.c

# rdkfw_main_gtest: Tests core firmware upgrade logic (rdkv_main.c and rdkv_upgrade.c)
# REFACTORED: Uses mocks instead of real helper implementations to avoid multiple definition errors
# Key decisions:
#   1. REMOVED download_status_helper.c - mocked in miscellaneous_mock.cpp (updateFWDownloadStatus, notifyDwnlStatus)
#   2. REMOVED device_status_helper.c - mocked in miscellaneous_mock.cpp (CurrentRunningInst, isDnsResolve, etc.)
#   3. KEPT chunk.c - core upgrade logic, not mocked
#   4. KEPT flash.c - core flashing logic, not mocked
#   5. KEPT json_process.c - core JSON parsing, not mocked
#   6. KEPT deviceutils/* - device utility functions with separate deviceutils_mock.cpp
# Note: The original design (develop/) also excluded these helper files, relying on mocks
rdkfw_main_gtest_SOURCES = basic_rdkv_main_gtest.cpp \
    ../src/rdkv_main.c \
    ../src/rdkv_upgrade.c \
    ../src/chunk.c \
    ../src/flash.c \
    ../src/json_process.c \
    ../src/deviceutils/device_api.c \
    ../src/deviceutils/deviceutils.c \
    deviceutils/json_parse.c \
    ./mocks/deviceutils_mock.cpp

rdkfw_interface_gtest_SOURCES = fwdl_interface_gtest.cpp \
    ./mocks/interface_mock.cpp \
    ../src/rfcInterface/rfcinterface.c \
    ../src/iarmInterface/iarmInterface.c \
    ./mocks/rbus_mock.c \
    ../src/rbusInterface/rbusInterface.c \
    deviceutils/json_parse.c

rdkfwupdatemgr_gtest_SOURCES = rdkfwupdatemgr_gtest.cpp \
    ../src/dbus/rdkFwupdateMgr_handlers.c \
    ../src/json_process.c \
    ../src/rdkv_main.c \
    ../src/rdkv_upgrade.c \
    ../src/deviceutils/device_api.c \
    ../src/deviceutils/deviceutils.c \
    ../src/rfcInterface/rfcinterface.c \
    deviceutils/json_parse.c \
    miscellaneous_mock.cpp \
    ./mocks/deviceutils_mock.cpp

# rdkFwupdateMgr_handlers_gtest: Tests D-Bus handler functions for daemon
# Tests the async, non-blocking, cache-first CheckForUpdate implementation
# Uses mocks for XConf, device info, and other external dependencies
rdkFwupdateMgr_handlers_gtest_SOURCES = rdkFwupdateMgr_handlers_gtest.cpp \
    ./mocks/rdkFwupdateMgr_mock.cpp \
    ../src/dbus/rdkFwupdateMgr_handlers.c \
    ../src/json_process.c \
   deviceutils/json_parse.c
# Note: json_process.c is mocked in rdkFwupdateMgr_mock.cpp to control XConf parsing behavior
# Note: rdkFwupdateMgr_mock.cpp provides stubs for current_flash, rfc_list, IsFlashInProgress, SWLOG_* functions

# Apply common properties to each program
rdkfw_device_status_gtest_CPPFLAGS = $(COMMON_CPPFLAGS)
rdkfw_device_status_gtest_LDADD = $(COMMON_LDADD) 
rdkfw_device_status_gtest_CXXFLAGS = $(COMMON_CXXFLAGS)
rdkfw_device_status_gtest_CFLAGS = $(COMMON_CXXFLAGS)

rdkfw_deviceutils_gtest_CPPFLAGS = $(COMMON_CPPFLAGS) -DDEVICE_API
rdkfw_deviceutils_gtest_LDADD = $(COMMON_LDADD) 
rdkfw_deviceutils_gtest_CXXFLAGS = $(COMMON_CXXFLAGS)
rdkfw_deviceutils_gtest_CFLAGS = $(COMMON_CXXFLAGS)

rdkfw_main_gtest_CPPFLAGS = $(COMMON_CPPFLAGS) -DGTEST_BASIC
rdkfw_main_gtest_LDADD = $(COMMON_LDADD) 
rdkfw_main_gtest_CXXFLAGS = $(COMMON_CXXFLAGS)
rdkfw_main_gtest_CFLAGS = $(COMMON_CXXFLAGS)

rdkfw_interface_gtest_CPPFLAGS = $(COMMON_CPPFLAGS)
rdkfw_interface_gtest_LDADD = $(COMMON_LDADD)
rdkfw_interface_gtest_CXXFLAGS = $(COMMON_CXXFLAGS)
rdkfw_interface_gtest_CFLAGS = $(COMMON_CXXFLAGS)

rdkfwupdatemgr_gtest_CPPFLAGS = $(COMMON_CPPFLAGS) -I../src/dbus $(GLIB_CFLAGS)
rdkfwupdatemgr_gtest_LDFLAGS = $(GLIB_LIBS)
rdkfwupdatemgr_gtest_LDADD = $(COMMON_LDADD)
rdkfwupdatemgr_gtest_CXXFLAGS = $(COMMON_CXXFLAGS)
rdkfwupdatemgr_gtest_CFLAGS = $(COMMON_CXXFLAGS)

rdkFwupdateMgr_handlers_gtest_CPPFLAGS = $(COMMON_CPPFLAGS) -I../src/dbus $(GLIB_CFLAGS) -DHANDLER_TEST_ONLY
rdkFwupdateMgr_handlers_gtest_LDFLAGS = $(GLIB_LIBS)
rdkFwupdateMgr_handlers_gtest_LDADD = $(COMMON_LDADD)
rdkFwupdateMgr_handlers_gtest_CXXFLAGS = $(COMMON_CXXFLAGS)
rdkFwupdateMgr_handlers_gtest_CFLAGS = $(COMMON_CXXFLAGS)

# rdkfwupdatemgr_main_flow_gtest: Tests rdkFwupdateMgr.c main flow functions
# Covers: getTriggerType, handle_signal, prevCurUpdateInfo, initialValidation gaps, main() logic
# Uses mocks ONLY - no real rfcinterface.c or iarmInterface.c to avoid multiple definition errors
# Key decisions:
#   1. REMOVED rfcinterface.c - mocked in miscellaneous_mock.cpp (getRFCSettings, isIncremetalCDLEnable, etc.)
#   2. REMOVED iarmInterface.c - mocked in miscellaneous_mock.cpp (init_event_handler, eventManager, etc.)
#   3. REMOVED rbusInterface.c and rbus_mock.c - not needed for main flow tests
#   4. KEPT rdkFwupdateMgr.c - core file under test
#   5. KEPT deviceutils/* - device utility functions with deviceutils_mock.cpp
#   6. KEPT json_process.c - core JSON parsing
rdkfwupdatemgr_main_flow_gtest_SOURCES = rdkfwupdatemgr_main_flow_gtest.cpp \
    ../src/rdkFwupdateMgr.c \
    ../src/deviceutils/device_api.c \
    ../src/deviceutils/deviceutils.c \
    ../src/json_process.c \
    deviceutils/json_parse.c \
    miscellaneous_mock.cpp \
    ./mocks/deviceutils_mock.cpp

rdkfwupdatemgr_main_flow_gtest_CPPFLAGS = $(COMMON_CPPFLAGS) -I../src/dbus $(GLIB_CFLAGS)
rdkfwupdatemgr_main_flow_gtest_LDFLAGS = $(GLIB_LIBS)
rdkfwupdatemgr_main_flow_gtest_LDADD = $(COMMON_LDADD)
rdkfwupdatemgr_main_flow_gtest_CXXFLAGS = $(COMMON_CXXFLAGS)
rdkfwupdatemgr_main_flow_gtest_CFLAGS = $(COMMON_CXXFLAGS)
