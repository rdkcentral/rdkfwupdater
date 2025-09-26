WORKDIR=`pwd`
export ROOT=/usr
export INSTALL_DIR=${ROOT}/local
mkdir -p $INSTALL_DIR

# Below dependencies are available in native build containers 
# 1] utilities
# 2] libsyswrapper


#Build rdkfwupdater
autoreconf -i
./configure --prefix=${INSTALL_DIR} CFLAGS="-DRDK_LOGGER"
make && make install
