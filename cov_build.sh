#!/bin/bash
set -e
##############################
# EXPERIMENTAL
# DO NOT MODIFY

GITHUB_WORKSPACE="${PWD}"
# # ############################# 
#1. Install Dependencies and packages

apt update
apt install -y libsqlite3-dev libcurl4-openssl-dev valgrind lcov clang libsystemd-dev libboost-all-dev libwebsocketpp-dev meson libcunit1 libcunit1-dev curl protobuf-compiler-grpc libgrpc-dev libgrpc++-dev libunwind-dev libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libwpewebkit-1.0-dev
pip install jsonref

############################
# Build trevor-base64
if [ ! -d "trower-base64" ]; then
git clone https://github.com/xmidt-org/trower-base64.git
fi
cd trower-base64
meson setup --warnlevel 3 --werror build
ninja -C build
ninja -C build install
cd ..
###########################################
# Clone the required repositories


git clone --branch  R4.4.3 https://github.com/rdkcentral/ThunderTools.git

git clone --branch R4.4.1 https://github.com/rdkcentral/Thunder.git

git clone --branch main https://github.com/rdkcentral/entservices-apis.git

#git clone https://github.com/rdkcentral/entservices-connectivity.git

git clone https://github.com/rdkcentral/entservices-runtime.git

git clone https://$GITHUB_TOKEN@github.com/rdkcentral/entservices-testframework.git

############################
# Build Thunder-Tools
echo "======================================================================================"
echo "buliding thunderTools"
cd ThunderTools
echo "akshay current working dir: "${PWD}
echo "akshay applying patch in thunder tools"
patch -p1 < $GITHUB_WORKSPACE/entservices-testframework/Tests/L1Tests/patches/00010-R4.4-Add-support-for-project-dir.patch
cd -


cmake -G Ninja -S ThunderTools -B build/ThunderTools \
    -DEXCEPTIONS_ENABLE=ON \
    -DCMAKE_INSTALL_PREFIX="$GITHUB_WORKSPACE/install/usr" \
    -DCMAKE_MODULE_PATH="$GITHUB_WORKSPACE/install/tools/cmake" \
    -DGENERIC_CMAKE_MODULE_PATH="$GITHUB_WORKSPACE/install/tools/cmake" \

cmake --build build/ThunderTools --target install


############################
# Build Thunder
echo "======================================================================================"
echo "buliding thunder"

cd Thunder
echo "akshay current working dir: "${PWD}
echo "akshay applying patch in thunder"
patch -p1 < $GITHUB_WORKSPACE/entservices-testframework/Tests/L2Tests/patches/Use_Legact_Alt_Based_On_ThunderTools_R4.4.3.patch
patch -p1 < $GITHUB_WORKSPACE/entservices-testframework/Tests/L2Tests/patches/error_code_R4_4.patch
patch -p1 < $GITHUB_WORKSPACE/entservices-testframework/Tests/L1Tests/patches/1004-Add-support-for-project-dir.patch
patch -p1 < $GITHUB_WORKSPACE/entservices-testframework/Tests/L1Tests/patches/RDKEMW-733-Add-ENTOS-IDS.patch
cd -

cmake -G Ninja -S Thunder -B build/Thunder \
    -DMESSAGING=ON \
    -DCMAKE_INSTALL_PREFIX="$GITHUB_WORKSPACE/install/usr" \
    -DCMAKE_MODULE_PATH="$GITHUB_WORKSPACE/install/tools/cmake" \
    -DGENERIC_CMAKE_MODULE_PATH="$GITHUB_WORKSPACE/install/tools/cmake" \
    -DBUILD_TYPE=Debug \
    -DBINDING=127.0.0.1 \
    -DPORT=55555 \
    -DEXCEPTIONS_ENABLE=ON \

cmake --build build/Thunder --target install


############################
# Build entservices-apis
echo "======================================================================================"
echo "buliding entservices-apis"
cd entservices-apis
rm -rf jsonrpc/DTV.json
cd ..

cmake -G Ninja -S entservices-apis  -B build/entservices-apis \
    -DEXCEPTIONS_ENABLE=ON \
    -DCMAKE_INSTALL_PREFIX="$GITHUB_WORKSPACE/install/usr" \
    -DCMAKE_MODULE_PATH="$GITHUB_WORKSPACE/install/tools/cmake" \

cmake --build build/entservices-apis --target install



############################
# generating extrnal headers
cd $GITHUB_WORKSPACE
cd entservices-testframework/Tests
echo " Empty mocks creation to avoid compilation errors"
echo "======================================================================================"
mkdir -p headers
mkdir -p headers/audiocapturemgr
mkdir -p headers/rdk/ds
mkdir -p headers/rdk/iarmbus
mkdir -p headers/rdk/iarmmgrs-hal
mkdir -p headers/ccec/drivers
mkdir -p headers/network
echo "dir created successfully"
echo "======================================================================================"

echo "======================================================================================"
echo "empty headers creation"
cd headers
echo "current working dir: "${PWD}
touch audiocapturemgr/audiocapturemgr_iarm.h
touch ccec/drivers/CecIARMBusMgr.h
touch rdk/ds/audioOutputPort.hpp
touch rdk/ds/compositeIn.hpp
touch rdk/ds/dsDisplay.h
touch rdk/ds/dsError.h
touch rdk/ds/dsMgr.h
touch rdk/ds/dsTypes.h
touch rdk/ds/dsUtl.h
touch rdk/ds/exception.hpp
touch rdk/ds/hdmiIn.hpp
touch rdk/ds/host.hpp
touch rdk/ds/list.hpp
touch rdk/ds/manager.hpp
touch rdk/ds/sleepMode.hpp
touch rdk/ds/videoDevice.hpp
touch rdk/ds/videoOutputPort.hpp
touch rdk/ds/videoOutputPortConfig.hpp
touch rdk/ds/videoOutputPortType.hpp
touch rdk/ds/videoResolution.hpp
touch rdk/iarmbus/libIARM.h
touch rdk/iarmbus/libIBus.h
touch rdk/iarmbus/libIBusDaemon.h
touch rdk/iarmmgrs-hal/deepSleepMgr.h
touch rdk/iarmmgrs-hal/mfrMgr.h
touch rdk/iarmmgrs-hal/pwrMgr.h
touch rdk/iarmmgrs-hal/sysMgr.h
touch network/wifiSrvMgrIarmIf.h
touch network/netsrvmgrIarm.h
touch libudev.h
touch rfcapi.h
touch rbus.h
touch telemetry_busmessage_sender.h
touch maintenanceMGR.h
touch pkg.h
touch secure_wrapper.h
touch wpa_ctrl.h
touch btmgr.h
touch rdk_logger_milestone.h
touch gdialservice.h
touch gdialservicecommon.h
echo "files created successfully"
echo "======================================================================================"

cd ../../
cp -r /usr/include/gstreamer-1.0/gst /usr/include/glib-2.0/* /usr/lib/x86_64-linux-gnu/glib-2.0/include/* /usr/local/include/trower-base64/base64.h .

############################
# Build entservices-deviceanddisplay
echo "======================================================================================"
echo "buliding entservices-deviceanddisplay"
cd $GITHUB_WORKSPACE
cmake -G Ninja -S entservices-runtime -B build/entservices-runtime \
  -DUSE_THUNDER_R4=ON \
  -DCMAKE_INSTALL_PREFIX="$GITHUB_WORKSPACE/install/usr" \
  -DCMAKE_MODULE_PATH="$GITHUB_WORKSPACE/install/tools/cmake" \
  -DCMAKE_VERBOSE_MAKEFILE=ON \
  -DCMAKE_DISABLE_FIND_PACKAGE_IARMBus=ON \
  -DCMAKE_DISABLE_FIND_PACKAGE_RFC=ON \
  -DCMAKE_DISABLE_FIND_PACKAGE_DS=ON \
  -DCMAKE_CXX_FLAGS="-DEXCEPTIONS_ENABLE=ON \
                      -I ${PWD}/entservices-testframework/Tests/headers \
                      -I ${PWD}/entservices-testframework/Tests/headers/audiocapturemgr \
                      -I ${PWD}/entservices-testframework/Tests/headers/rdk/ds \
                      -I ${PWD}/entservices-testframework/Tests/headers/rdk/iarmbus \
                      -I ${PWD}/entservices-testframework/Tests/headers/rdk/iarmmgrs-hal \
                      -I ${PWD}/entservices-testframework/Tests/headers/ccec/drivers \
                      -I ${PWD}/entservices-testframework/Tests/headers/network \
                      -I ${PWD}/entservices-testframework/Tests \
                      -I ${PWD}/Thunder/Source \
                      -I ${PWD}/Thunder/Source/core \
                      -include ${PWD}/entservices-testframework/Tests/mocks/devicesettings.h \
                      -include ${PWD}/entservices-testframework/Tests/mocks/Iarm.h \
                      -include ${PWD}/entservices-testframework/Tests/mocks/Rfc.h \
                      -include ${PWD}/entservices-testframework/Tests/mocks/RBus.h \
                      -include ${PWD}/entservices-testframework/Tests/mocks/Telemetry.h \
                      -include ${PWD}/entservices-testframework/Tests/mocks/Udev.h \
                      -include ${PWD}/entservices-testframework/Tests/mocks/pkg.h \
                      -include ${PWD}/entservices-testframework/Tests/mocks/maintenanceMGR.h \
                      -include ${PWD}/entservices-testframework/Tests/mocks/gdialservice.h \
                      -include ${PWD}/entservices-testframework/Tests/mocks/wpa_ctrl_mock.h \
                      -include ${PWD}/entservices-testframework/Tests/mocks/secure_wrappermock.h \
                      --coverage -Wall -Werror -Wno-error=format \
                      -Wl,-wrap,system -Wl,-wrap,popen -Wl,-wrap,syslog -Wl,-wrap,wpa_ctrl_open -Wl,-wrap,wpa_ctrl_request -Wl,-wrap,wpa_ctrl_close -Wl,-wrap,wpa_ctrl_pending -Wl,-wrap,wpa_ctrl_recv -Wl,-wrap,wpa_ctrl_attach \
                      -DENABLE_TELEMETRY_LOGGING -DUSE_IARMBUS \
                      -DENABLE_SYSTEM_GET_STORE_DEMO_LINK -DENABLE_DEEP_SLEEP \
                      -DENABLE_SET_WAKEUP_SRC_CONFIG -DENABLE_THERMAL_PROTECTION \
                      -DUSE_DRM_SCREENCAPTURE -DHAS_API_SYSTEM -DHAS_API_POWERSTATE \
                      -DHAS_RBUS -DDISABLE_SECURITY_TOKEN -DENABLE_DEVICE_MANUFACTURER_INFO -DUSE_THUNDER_R4=ON -DTHUNDER_VERSION=4 -DTHUNDER_VERSION_MAJOR=4 -DTHUNDER_VERSION_MINOR=4" \
  -DCOMCAST_CONFIG=OFF \
  -DRDK_SERVICES_COVERITY=ON \
  -DRDK_SERVICES_L1_TEST=ON \
  -DDS_FOUND=ON \
  -DPLUGIN_PERFORMANCEMETRICS=ON \


cmake --build build/entservices-runtime --target install
echo "======================================================================================"
exit 0
