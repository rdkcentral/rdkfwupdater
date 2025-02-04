WORKDIR=`pwd`
export ROOT=/usr
export INSTALL_DIR=${ROOT}/local
mkdir -p $INSTALL_DIR
cd $ROOT

# Below dependencies are available in native build containers 
# 1] utilities
# 2] libsyswrapper


#Build rdkfwupdater
cd /home/rdkfwupdater
autoreconf -i
./configure --prefix=${INSTALL_DIR}
make && make install
