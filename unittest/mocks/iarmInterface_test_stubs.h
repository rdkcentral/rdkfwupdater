// unittest/mocks/iarmInterface_test_stubs.h
#ifdef GTEST_ENABLE

#ifndef IARM_BUS_MFRLIB_NAME
#define IARM_BUS_MFRLIB_NAME "IARM_BUS_MFRLIB_NAME_teststub"
#endif

#ifndef IARM_BUS_MFRLIB_API_GetSerializedData
#define IARM_BUS_MFRLIB_API_GetSerializedData 111
#endif

typedef struct {
    int type;
    size_t bufLen;
    char buffer[256];
} IARM_Bus_MFRLib_GetSerializedData_Param_t;

#define mfrSERIALIZED_TYPE_PDRIVERSION 0

typedef int IARM_Result_t;
#define IARM_RESULT_SUCCESS 0

#endif // GTEST_ENABLE
