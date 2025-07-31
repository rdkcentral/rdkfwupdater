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

cd ./unittest/

automake --add-missing
autoreconf --install

./configure

make

#./rdkfw_device_status_gtest

#devicestatus=$?
#echo "*********** Return value $devicestatus"

#./rdkfw_deviceutils_gtest

#deviceutils=$?
#echo "==========> Return value $deviceutils"

#./rdkfw_main_gtest

#mainapp=$?
#echo "-------------> Retrun value $mainapp"

./rdkfw_interface_gtest

rdkfw_interface=$?
echo "-------------> Retrun value $rdkfw_interface"

if [ "$devicestatus" = "0" ] && [ "$deviceutils" = "0" ] && [ "$mainapp" = "0" ] && [ "$rdkfw_interface" = "0" ]; then
    cd ../src/

    lcov --capture --directory . --output-file coverage.info

    lcov --remove coverage.info '/usr/*' --output-file coverage.filtered.info

    genhtml coverage.filtered.info --output-directory out
else
    echo "L1 UNIT TEST FAILED. PLEASE CHECK AND FIX"
    exit 1
fi
