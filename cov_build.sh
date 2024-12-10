WORKDIR=`pwd`
export ROOT=/usr
export INSTALL_DIR=${ROOT}/local
mkdir -p $INSTALL_DIR
cd $ROOT

# Build common utilities
cd /home
git clone https://github.com/rdkcentral/common_utilities.git
cd /home/common_utilities
autoreconf -i
./configure --prefix=${INSTALL_DIR}
sed -i 's/-Werror//g' ./utils/Makefile 
make && make install

#Build libsyswrapper
cd /home
git clone https://github.com/rdkcmf/rdk-libSyscallWrapper.git
cd /home/rdk-libSyscallWrapper
#autoupdate
autoreconf -i
./configure --prefix=${INSTALL_DIR}
make
make install

#Build rdkfwupdater
export CFLAGS="-DCONTAINER_COVERITY_ENABLE"
cd /home/rdkfwupdater
autoreconf -i
./configure --enable-coverity-feature  --prefix=${INSTALL_DIR}
#sed -i 's/-Werror//g' ./utils/Makefile 
make && make install
