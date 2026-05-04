// File: unittest/mocks/iarmInterface_mock.h

#pragma once
#include <cstddef>

class IarmInterfaceMock {
public:
    virtual ~IarmInterfaceMock() = default;
    virtual size_t GetPDRIFileNameUsingMFR(char *pPDRIFilename, size_t szBufSize) {
        // Default mock does nothing real
        return 0;
    }
};

extern IarmInterfaceMock *g_IarmInterfaceMock;
