WORKDIR=`pwd`
export ROOT=/usr
export INSTALL_DIR=${ROOT}/local
mkdir -p $INSTALL_DIR

#cd /opt/
#git clone https://github.com/rdkcentral/rdk_logger.git
#cd rdk_logger
#autoreconf -i
#./configure
#make & make install
#rm -rf /opt/rdk_logger
#cd ${WORKDIR}

# Below dependencies are available in native build containers 
# 1] utilities
# 2] libsyswrapper


#Build rdkfwupdater
autoreconf -i
./configure --prefix=${INSTALL_DIR} CFLAGS="-DRDK_LOGGER" 
make && make install
