#ifndef MISCELLANEOUS_MOCK_H
#define MISCELLANEOUS_MOCK_H

#include <gmock/gmock.h>
#include <ctime>

#include "miscellaneous.h"
#include "rdkv_upgrade.h"
#include "rfcinterface.h"

class MockExternal {
public:
    MOCK_METHOD(unsigned int, doGetDwnlBytes, (void*), ());
    MOCK_METHOD(int, doInteruptDwnl, (void*, unsigned int), ());
    MOCK_METHOD(void, setForceStop, (int), ());
    MOCK_METHOD(T2ERROR, t2_event_s, (char*, char*), ());
    MOCK_METHOD(T2ERROR, t2_event_d, (char*, int), ());
    MOCK_METHOD(void, t2_init, (char*), ());
    MOCK_METHOD(int, getDeviceProperties, (DeviceProperty_t*), ());
    MOCK_METHOD(int, getImageDetails, (ImageDetails_t*), ());
    MOCK_METHOD(int, createDir, (const char*), ());
    MOCK_METHOD(int, createFile, (const char*), ());
    MOCK_METHOD(void, t2_uninit, (), ());
    MOCK_METHOD(void, log_exit, (), ());
    MOCK_METHOD(int, doHttpFileDownload, (void*, FileDwnl_t*, MtlsAuth_t*, unsigned int, char*, int*), ());
    MOCK_METHOD(int, logFileData, (const char*), ());
    MOCK_METHOD(bool, isMediaClientDevice, (), ());
    MOCK_METHOD(int, doAuthHttpFileDownload, (void*, FileDwnl_t*, int*), ());
    MOCK_METHOD(void, logMilestone, (const char*), ());
    MOCK_METHOD(int, eraseFolderExceParamFile, (const char*, const char*, const char*, const char*), ());
    MOCK_METHOD(int, doCurlPutRequest, (void*, FileDwnl_t*, char*, int*), ());
    MOCK_METHOD(int, checkAndEnterStateRed, (int, const char*), ());
    MOCK_METHOD(int, getRFCSettings, (Rfc_t*), ());
    MOCK_METHOD(void, eventManager, (const char*, const char*), ());
    MOCK_METHOD(int, updateFWDownloadStatus, (struct FWDownloadStatus*, const char*), ());
    MOCK_METHOD(int, init_event_handler, (), ());
    MOCK_METHOD(int, isDwnlBlock, (int), ());
    MOCK_METHOD(bool, checkCodebigAccess, (), ());
    MOCK_METHOD(int, term_event_handler, (), ());
    MOCK_METHOD(int, isThrottleEnabled, (const char*, const char*, int), ());
    MOCK_METHOD(int, isOCSPEnable, (), ());
    MOCK_METHOD(int, getMtlscert, (MtlsAuth_t*), ());
    MOCK_METHOD(int, isIncremetalCDLEnable, (const char*), ());
    MOCK_METHOD(bool, isDelayFWDownloadActive, (int, const char*, int), ());
    MOCK_METHOD(bool, checkPDRIUpgrade, (const char*), ());
    MOCK_METHOD(bool, isUpgradeInProgress, (), ());
    MOCK_METHOD(bool, isMmgbleNotifyEnabled, (), ());
    MOCK_METHOD(time_t, getCurrentSysTimeSec, (), ());
    MOCK_METHOD(int, notifyDwnlStatus, (const char*, const char*, RFCVALDATATYPE), ());
    MOCK_METHOD(bool, updateOPTOUTFile, (const char*), ());
    MOCK_METHOD(bool, CheckIProuteConnectivity, (const char*), ());
    MOCK_METHOD(bool, isDnsResolve, (const char*), ());
    MOCK_METHOD(void, unsetStateRed, (), ());
    MOCK_METHOD(bool, checkForValidPCIUpgrade, (int, const char*, const char*, const char*), ());
    MOCK_METHOD(bool, isPDRIEnable, (), ());
    MOCK_METHOD(bool, lastDwnlImg, (char*, size_t), ());
    MOCK_METHOD(bool, currentImg, (char*, size_t), ());
    MOCK_METHOD(bool, CurrentRunningInst, (const char*), ());
    MOCK_METHOD(void, eraseTGZItemsMatching, (const char*, const char*), ());
    MOCK_METHOD(bool, prevFlashedFile, (char*, size_t), ());
    MOCK_METHOD(int, doCodeBigSigning, (int, const char*, char*, size_t, char*, size_t), ());
    MOCK_METHOD(void*, allocDowndLoadDataMem, (size_t), ());
    MOCK_METHOD(int, GetFileContents, (const char*, char**, size_t*), ());
    MOCK_METHOD(int, GetFirmwareVersion, (char*, size_t), ());
    MOCK_METHOD(int, GetBuildType, (char*, size_t, BUILDTYPE*), ());
    MOCK_METHOD(int, GetMFRName, (char*, size_t), ());
    MOCK_METHOD(int, GetUTCTime, (char*, size_t), ());
    MOCK_METHOD(int, GetTimezone, (char*, size_t), ());
    MOCK_METHOD(void, waitForNtp, (), ());
    MOCK_METHOD(int, GetCapabilities, (char*, size_t), ());
    MOCK_METHOD(int, stripinvalidchar, (char*), ());
    MOCK_METHOD(int, makeHttpHttps, (char*), ());
};

extern MockExternal* global_mockexternal_ptr;

#endif
