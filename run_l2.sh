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

git clone https://github.com/rdkcentral/common_utilities.git
cd common_utilities
git checkout develop
autoreconf -i
./configure  --enable-rdkcertselector --prefix=${INSTALL_DIR} CFLAGS=" -DRDK_LOGGER "
make && make install

cd ../

#Build rdkfwupdater
autoreconf -i
./configure --prefix=${INSTALL_DIR} --enable-rdkcertselector=yes --enable-mountutils=yes --enable-rfcapi=yes CFLAGS="-DRDK_LOGGER -DRDKFW_L2_TEST_BUILD"
make clean
make && make install

# Verify daemon binary was installed
echo ""
echo "Verifying rdkFwupdateMgr installation..."
if [ -f "/usr/local/bin/rdkFwupdateMgr" ]; then
    echo "Daemon binary found: /usr/local/bin/rdkFwupdateMgr"
    ls -lh /usr/local/bin/rdkFwupdateMgr
else
    echo "ERROR: Daemon binary NOT found at /usr/local/bin/rdkFwupdateMgr"
    echo "  Tests will fail - build may have failed"
    exit 1
fi

#./cov_build.sh

# Compile Test binary for mfrutils
cc -o /usr/bin/mfr_util test/functional-tests/tests/mfrutils.c 

rbuscli setv Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.SWDLSpLimit.Enable boolean true
rbuscli setv Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.SWDLSpLimit.TopSpeed int 1280000
rbuscli setv Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Identity.DbgServices.Enable boolean true
rbuscli setv Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.SWDLDirect.Enable boolean false
unset RDKFW_FORCE_DIRECTCDN
cp test/functional-tests/tests/rc-proxy-params.json /tmp/rc-proxy-params.json


echo ""
echo "=========================================="
echo "Running L2 Integration Tests"
echo "=========================================="
echo ""


rbuscli setv Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.SWDLDirect.Enable boolean true
export RDKFW_FORCE_DIRECTCDN=true

echo "Running DirectCDN image download tests..."
pytest --json-report --json-report-file $RESULT_DIR/rdkfwupdater_dcdn_image_tests.json \
    test/functional-tests/tests/test_DCDN_imagedwnl.py \
    test/functional-tests/tests/test_DCDN_imagedwnl_error.py \
    test/functional-tests/tests/test_DCDN_certbundle_dwnl.py \
    test/functional-tests/tests/test_DCDN_peripheral_imagedwnl.py

rbuscli setv Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.SWDLDirect.Enable boolean false
unset RDKFW_FORCE_DIRECTCDN



echo ""
echo "=========================================="
echo "L2 Test Results"
echo "=========================================="
echo "Image tests report: $RESULT_DIR/rdkfwupdater_dcdn_image_tests.json"
echo "=========================================="
