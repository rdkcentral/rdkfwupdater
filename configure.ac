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

AC_PREREQ([2.65])
AC_INIT([rdkfwupgrader], [1.0], [BUG-REPORT-ADDRESS])
AM_INIT_AUTOMAKE([subdir-objects])
#AM_INIT_AUTOMAKE([foreign])
LT_INIT

AC_PREFIX_DEFAULT(`pwd`)
AC_ENABLE_SHARED
AC_DISABLE_STATIC
AC_PROG_AWK
AC_PROG_LN_S
AC_PROG_MAKE_SET

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])

# Checks for programs.
AC_PROG_CXX
AC_PROG_CC
AC_PROG_INSTALL
AM_PROG_CC_C_O
AM_PROG_LIBTOOL(libtool)

TRACE_CFLAGS=" "
TRACE_LIBS==" "

# Checks for header files.
AC_CHECK_HEADERS([stdlib.h string.h unistd.h])
AC_FUNC_MKTIME
AC_PROG_RANLIB
AC_TYPE_SIZE_T
AC_TYPE_UINT32_T

AC_ARG_ENABLE([cpc-code],
    [AS_HELP_STRING([--enable-cpc-code], [Enable CPC code support])],
    [USE_CPC_CODE="$enableval"],
    [USE_CPC_CODE=no])

AM_CONDITIONAL([USE_CPC_CODE], [test "$USE_CPC_CODE" = "yes"])

if test "$USE_CPC_CODE" = "yes"; then
    AC_DEFINE([USE_CPC_CODE], [1], [Define to enable CPC CODE support])
fi

AC_ARG_ENABLE([mountutils],
        AS_HELP_STRING([--enable-mountutils],[enables mountutils replacement (default is no)]),
        [
          case "${enableval}" in
           yes) IS_LIBRDKCONFIG_ENABLED=true
                LIBRDKCONFIG_FLAG=" -DLIBRDKCONFIG_BUILD ";;
           no)  IS_LIBRDKCONFIG_ENABLED=false ;;
          *) AC_MSG_ERROR([bad value ${enableval} for --enable-mountutils]) ;;
           esac
           ],
        [echo "mountutils is disabled"])
AM_CONDITIONAL([IS_LIBRDKCONFIG_ENABLED], [test x$IS_LIBRDKCONFIG_ENABLED = xtrue])
AC_SUBST(LIBRDKCONFIG_FLAG)

AC_ARG_ENABLE([rfcapi],
        AS_HELP_STRING([--enable-rfcapi],[enables rfcapi]),
        [
          case "${enableval}" in
           yes) IS_LIBRFCAPI_ENABLED=true
                LIBRFCAPI_FLAG=" -DRFC_API_ENABLED ";;
           no)  IS_LIBRFCAPI_ENABLED=false ;;
          *) AC_MSG_ERROR([bad value ${enableval} for --enable-rfcapi]) ;;
           esac
           ],
        [echo "rfcapi is disabled"])
AM_CONDITIONAL([IS_LIBRFCAPI_ENABLED], [test x$IS_LIBRFCAPI_ENABLED = xtrue])
AC_SUBST(LIBRFCAPI_FLAG)

AC_ARG_ENABLE([t2api],
        AS_HELP_STRING([--enable-t2api],[enables telemetry]),
        [
          case "${enableval}" in
           yes) IS_TELEMETRY2_ENABLED=true
                T2_EVENT_FLAG=" -DT2_EVENT_ENABLED ";;
           no)  IS_TELEMETRY2_ENABLED=false ;;
          *) AC_MSG_ERROR([bad value ${enableval} for --enable-t2enable]) ;;
           esac
           ],
        [echo "telemetry is disabled"])
AM_CONDITIONAL([IS_TELEMETRY2_ENABLED], [test x$IS_TELEMETRY2_ENABLED = xtrue])
AC_SUBST(T2_EVENT_FLAG)

AC_ARG_ENABLE([iarmevent],
        AS_HELP_STRING([--enable-iarmevent],[enables IARM event]),
        [
          case "${enableval}" in
           yes) IS_IARMEVENT_ENABLED=true
                IARM_EVENT_FLAG=" -DIARM_ENABLED ";;
           no)  IS_IARMEVENT_ENABLED=false ;;
          *) AC_MSG_ERROR([bad value ${enableval} for --enable-iarmevent]) ;;
           esac
           ],
        [echo "iarm is disabled"])
AM_CONDITIONAL([IS_IARMEVENT_ENABLED], [test x$IS_IARMEVENT_ENABLED = xtrue])
AC_SUBST(IARM_EVENT_FLAG)
#AC_ARG_ENABLE([coverity-feature],
#    [AS_HELP_STRING([--enable-coverity-feature], [Enable COVERITY code support])],
#    [CONTAINER_COVERITY_ENABLE=yes],
#    [CONTAINER_COVERITY_ENABLE=no])
#
#AM_CONDITIONAL([CONTAINER_COVERITY_ENABLE], [test "$CONTAINER_COVERITY_ENABLE" = "yes"])
#
#if test "$CONTAINER_COVERITY_ENABLE" = "yes"; then
#    AC_DEFINE([CONTAINER_COVERITY_ENABLE], [1], [Define to enable Coverity support])
#fi

PKG_CHECK_MODULES([cjson], [libcjson])
PKG_CHECK_MODULES([curl], [libcurl])

# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_C_INLINE

# Checks for library functions.
AC_FUNC_MALLOC

AC_SUBST(TRACE_CFLAGS)
AC_SUBST(TRACE_LIBS)
AC_CONFIG_FILES(
    Makefile
    )
AC_OUTPUT
