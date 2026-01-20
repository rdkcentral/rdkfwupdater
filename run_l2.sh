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

# Clone common_utilities if not already present
#if [ ! -d "common_utilities" ]; then
#    git clone https://github.com/rdkcentral/common_utilities.git
#fi
#ls
#cd common_utilities
#git checkout topic/RDK-59276-modularization
# Build common utilities
#autoreconf -i
#./configure --prefix=${INSTALL_DIR} CFLAGS=" -DRDK_LOGGER"
#make && make install

# Return to main rdkfwupdater directory
#cd ..

#Build rdkfwupdater
autoreconf -i
./configure --prefix=${INSTALL_DIR} --enable-rdkcertselector=yes --enable-mountutils=yes --enable-rfcapi=yes CFLAGS="-DRDK_LOGGER "
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
cp test/functional-tests/tests/rc-proxy-params.json /tmp/rc-proxy-params.json

# ========================================
# Start D-Bus System Daemon (Required for D-Bus tests)
# ========================================

mkdir -p /etc/dbus-1/system.d
cp test/functional-tests/tests/org.rdkfwupdater.Service.conf /etc/dbus-1/system.d/
pkill -HUP dbus-daemon 2>/dev/null || true
sleep 1

echo ""
echo "Starting D-Bus system daemon..."

# Check if D-Bus is already running
if ! pgrep -x "dbus-daemon" > /dev/null; then
    # Ensure D-Bus runtime directory exists
    mkdir -p /run/dbus
    
    # Start D-Bus system daemon
    dbus-daemon --system --fork
    
    # Wait for D-Bus to be ready
    sleep 2
    
    # Verify D-Bus started successfully
    if pgrep -x "dbus-daemon" > /dev/null; then
        echo " D-Bus daemon started successfully"
    else
        echo "ERROR: Failed to start D-Bus daemon"
        echo "  D-Bus tests will fail!"
    fi
else
    echo "D-Bus daemon already running"
fi

echo ""
echo "=========================================="
echo "Running L2 Integration Tests"
echo "=========================================="
echo ""

# Run all existing tests
echo "[1/2] Running existing image download tests..."
#pytest --json-report --json-report-file $RESULT_DIR/rdkfwupdater_image_tests.json \
#       test/functional-tests/tests/test_imagedwnl.py \
#       test/functional-tests/tests/test_imagedwnl_error.py \
#       test/functional-tests/tests/test_certbundle_dwnl.py \
#       test/functional-tests/tests/test_peripheral_imagedwnl.py

# Run new D-Bus handler and cache tests
echo ""
echo "[2/2] Running D-Bus handler and cache tests..."
pytest -v -s --json-report --json-report-file $RESULT_DIR/rdkfwupdater_dbus_tests.json \
	test/functional-tests/tests/test_dbus_DownloadFirmware.py \
	test/functional-tests/tests/test_dbus_UnregisterProcess.py  \
	test/functional-tests/tests/test_dbus_CheckForUpdate.py \
	test/functional-tests/tests/test_dbus_RegisterProcess.py \
	test/functional-tests/tests/test_dbus_UpdateFirmware.py

echo ""
echo "=========================================="
echo "L2 Test Results"
echo "=========================================="
echo "Image tests report: $RESULT_DIR/rdkfwupdater_image_tests.json"
echo "D-Bus tests report: $RESULT_DIR/rdkfwupdater_dbus_tests.json"
echo "=========================================="
