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
AC_CHECK_FUNCS([memset])
AC_CHECK_FUNCS([socket])
AC_CHECK_HEADERS([arpa/inet.h])
AC_CHECK_HEADERS([netinet/in.h])
AC_CHECK_HEADERS([sys/ioctl.h])
AC_CHECK_HEADERS([sys/socket.h])
AC_CHECK_HEADERS([termio.h])
AC_FUNC_MKTIME
AC_PROG_RANLIB
AC_TYPE_SIZE_T
AC_TYPE_UINT32_T

RDK_FLAVOR="ubuntu"
AC_ARG_WITH([rdk-flavor],
[  --with-rdk-flavor    build flavor],
[case "${withval}" in
  ubuntu) RDK_FLAVOR=ubuntu;;
  rdkb) RDK_FLAVOR=rdkb ;;
  rdkv) RDK_FLAVOR=rdkv ;;
  *) AC_MSG_ERROR([bad value ${withval} for --with-rdk-flavor]) ;;
esac],[RDK_FLAVOR="ubuntu"])
AM_CONDITIONAL([ubuntu], [test "x$RDK_FLAVOR" = xubuntu])
AM_COND_IF([ubuntu], [AC_DEFINE([__UBUNTU__], [1], [ubuntu platform selection macro])])
AM_COND_IF([ubuntu], [PKG_CHECK_MODULES([dbuscpp], [dbus-c++-glib-1])])

AM_CONDITIONAL([rdkb], [test "x$RDK_FLAVOR" = xrdkb])
AM_COND_IF([rdkb], [AC_DEFINE([__RDKB__], [1], [rdkb platform selection macro])])
AM_COND_IF([rdkb], [PKG_CHECK_MODULES([dbus], [dbus-1])])
AM_COND_IF([rdkb], [PKG_CHECK_MODULES([gio], [gio-2.0])])

AM_CONDITIONAL([rdkv], [test "x$RDK_FLAVOR" = xrdkv])
AM_COND_IF([rdkv], [AC_DEFINE([__RDKV__], [1], [rdkv platform selection macro])])

AC_ARG_ENABLE([cpc-cedm],
    [AS_HELP_STRING([--enable-cpc-cedm], [Enable CPC CEDM support])],
    [USE_CPC_CEDM="$enableval"],
    [USE_CPC_CEDM=no])

AM_CONDITIONAL([USE_CPC_CEDM], [test "$USE_CPC_CEDM" = "yes"])

if test "$USE_CPC_CEDM" = "yes"; then
    AC_DEFINE([USE_CPC_CEDM], [1], [Define to enable CPC CEDM support])
fi

RDK_DEVICE="none"
AC_ARG_WITH([rdk-device],
[  --with-rdk-device    device model possible values arrisxb6, technicolorxb6, ciscoxb3, xb3_3939, pacexf3, none],
[case "${withval}" in
  pacexf3-bci) RDK_DEVICE=pacexf3 ;; # translate xf3-bci to xf3 as they are both same.
  *) RDK_DEVICE=${withval} ;;
esac],[RDK_DEVICE=none])

AM_CONDITIONAL([pacexf3], [test "x$RDK_DEVICE" = xpacexf3])
AM_COND_IF([pacexf3], [AC_DEFINE([__pacexf3__], [1], [pacexf3 device selection macro])])

AC_ARG_ENABLE([comcast-build],
              AS_HELP_STRING([--enable-comcast-build],[enable comcast build (default is yes)]),
              [
                case "${enableval}" in
                 yes) ENABLE_COMCAST_BUILD=true 
                      ENABLE_COMCAST_OFFSET_FLAG="-DCOMCAST_OFFSET" ;;
                  no) [echo "comcast build is disabled"] ;;
                   *) AC_MSG_ERROR([bad value ${enableval} for --enable-comcast-build]) ;;
                esac
              ],
              [
                ENABLE_COMCAST_BUILD=true
                [echo "comcast build is enabled"]
              ])
AM_CONDITIONAL([ENABLE_COMCAST_BUILD], [test x$ENABLE_COMCAST_BUILD = xtrue])

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

PKG_CHECK_MODULES([cjson], [libcjson])
PKG_CHECK_MODULES([curl], [libcurl])
AM_COND_IF([ubuntu], [PKG_CHECK_MODULES([dbus], [dbus-1])])
AM_COND_IF([ubuntu], [PKG_CHECK_MODULES([gio], [gio-2.0])])


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


