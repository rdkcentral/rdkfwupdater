WORKDIR=`pwd`
export ROOT=/usr
export INSTALL_DIR=${ROOT}/local
mkdir -p $INSTALL_DIR

git clone https://github.com/rdkcentral/common_utilities.git
cd common_utilities
git checkout topic/RDKTV-39792
autoreconf -i
./configure  --enable-rdkcertselector --prefix=${INSTALL_DIR} CFLAGS=" -DRDK_LOGGER "
make && make install

# Below dependencies are available in native build containers 
# 1] utilities
# 2] libsyswrapper


#Build rdkfwupdater
autoreconf -i
./configure --prefix=${INSTALL_DIR} CFLAGS="-DRDK_LOGGER" --enable-extended-logger
make && make install
