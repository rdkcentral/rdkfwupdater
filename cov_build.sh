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
./configure --prefix=${INSTALL_DIR} CFLAGS="-Wno-unused-result -Wno-format-truncation -Wno-error=format-security"
make && make install

#Build libsyswrapper
cd /home
git clone https://github.com/rdkcmf/rdk-libSyscallWrapper.git
cd /home/rdk-libSyscallWrapper
autoreconf -i
./configure --prefix=${INSTALL_DIR}
make
make install

#Build rdkfwupdater
cd /home/rdkfwupdater
autoreconf -i
./configure --prefix=${INSTALL_DIR}
make && make install
