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

export top_srcdir=`pwd`
RESULT_DIR="/tmp/l2_test_report"
mkdir -p "$RESULT_DIR"

WORKDIR=`pwd`
export ROOT=/usr
export INSTALL_DIR=${ROOT}/local
mkdir -p $INSTALL_DIR

#Build rdkfwupdater
autoreconf -i
./configure --prefix=${INSTALL_DIR} --enable-rfcapi=yes CFLAGS="-DRDK_LOGGER"
make && make install


#./cov_build.sh

# Compile Test binary for mfrutils
cc -o /usr/bin/mfr_util test/functional-tests/tests/mfrutils.c 

rbuscli setv Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.SWDLSpLimit.Enable boolean true
rbuscli setv Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.SWDLSpLimit.TopSpeed int 1280000
rbuscli setv Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Identity.DbgServices.Enable boolean true
cp test/functional-tests/tests/rc-proxy-params.json /tmp/rc-proxy-params.json

pytest --json-report  --json-report-file $RESULT_DIR/rdkfwupdater_report.json test/functional-tests/tests/
