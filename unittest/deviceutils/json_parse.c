/*
 * Copyright 2023 Comcast Cable Communications Management, LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <cjson/cJSON.h>
#include "json_parse.h"
#include "rdkv_cdl_log_wrapper.h"

/* function writeItemVal - optionally writes a JSON "name : value" pair to an output file in the format
                           of "name=value" which allows the resulting file to be sourced into a shell sript.

   Usage: int writeItemVal <FILE * to write output> <char * to name> <char * to value> <setenv flag>
            Input JSON String - a pointer to a string containing the JSON to parse. Typically, this is
            the return from the GetJson() function call.

            Search String - a pointer to a string to be searched for. This is the "name" part of the
            "name : value" pair in the input string.

            Output Location - a character pointer to store the output value if found.

            Size of Output Location - The size of the Output Location storage. This value includes a
            NULL terminator. Size values of 1 or greater ensure a NULL terminated string.

            RETURN - an integer value equal to the number of characters copied to the Output Location.
*/

static int writeItemVal( FILE* fpout, char *pName, char *pVal, int setenvvars )
{
    int iRet = 0;

    if( pName != NULL )
    {
        if( fpout != NULL )
        {
            fprintf( fpout, "%s=", pName );
            if( pVal != NULL )
            {
                fprintf( fpout, "\"%s\"\n", pVal );
            }
            else
            {
                SWLOG_ERROR( "writeItemVal: pVal is not printable\n" );
                iRet = 1;
            }
        }
        if( pName != NULL && pVal != NULL )
        {
            if( setenvvars )
            {
                iRet = setenv( pName, pVal, 1 );
                if( iRet )
                {
                    SWLOG_ERROR( "writeItemVal: setenv( %s, %s, 1 ) returned %d\n", pName, pVal, iRet );
                }
            }
        }
    }
    else
    {
        SWLOG_ERROR( "writeItemVal: one or more input args invalid\n" );
        iRet = 1;
    }
    return iRet;
}


/* function convertInvalidChars - environment variable names must only be [a-z], [A-Z], [0-9] or '_'.
                                  Invalid characters are converted to '_' (underscore).

   Usage: int convertInvalidChars <char * to NULL terminated string >
            Input NULL terminated String - a pointer to a string containing the string to check for invalid characters

            returns - none

*/

static void convertInvalidChars( char *pStr )
{
    if( pStr != NULL )
    {
        while( *pStr )
        {
            if( (*pStr < '0') ||
                (*pStr > '9' && *pStr < 'A') ||
                (*pStr > 'Z' && *pStr < 'a' && *pStr != '_') ||
                (*pStr > 'z') )
            {
                *pStr = '_';
            }
            ++pStr;
        }
    }
}

static size_t getitemval( cJSON *pcitem, char *pOut, size_t szpOutSize )
{
    char *pVal = NULL;
    int iRit;
    size_t len = 0;
    char cOut[50];

    if( cJSON_IsString( pcitem ) )
    {
        pVal = pcitem->valuestring;
    }
    else if( cJSON_IsBool( pcitem ) )
    {
        pVal = cJSON_IsTrue( pcitem ) ? "true" : "false";
    }
    else if( cJSON_IsNumber( pcitem ) )
    {
        iRit = snprintf( cOut, sizeof( cOut ), "%f",  pcitem->valuedouble );
        if( iRit > 0 )
        {
            --iRit;
        }
        pVal = &cOut[iRit];
        while( *pVal == '0' && iRit )
        {
            *pVal = 0;
            --pVal;
            --iRit;
        }
        if( *pVal == '.' )
        {
            *pVal = 0;
        }
        pVal = cOut;
    }

    if( pVal != NULL )
    {
        len = snprintf( pOut, szpOutSize, "%s", pVal );
    }
    else if( szpOutSize )
    {
        *pOut = 0;
    }
    return len;
}

/* function SetJsonVars - reads a json file and optionally writes individual pairs to
   an output file in the format name=value and/or set environment variables using name and value. 
   th the Usage: int SetJsonVars <Input JSON file> <Outfile file> <optional Set Environment Variables>
            Input JSON file - filename containing the JSON to parse.

            Output file - where to write the output name=value pairs. If NULL
            then no output file is created.

            Set Environment Variables - zero to skip setting of environment variables, 
            non-zero to set them. Defaults to 1 if argument is not present.

            RETURN - integer value of 0 on success, 1 otherwise.
*/

int SetJsonVars( char *fileIn, char *fileOut, int setenvvars )
{
    FILE *fpout = NULL;
    cJSON *json = NULL;
    cJSON *item = NULL;
    int iRet = 1;
    char *pStr, *pName, *pVal;
    char cOut[100];


    pStr = GetJson( fileIn );
    if( pStr != NULL )
    {
        json = (cJSON *)ParseJsonStr( pStr );
        if( json )
        {
            if( fileOut )
            {
                if( (fpout=fopen( fileOut, "w" )) == NULL )
                {
                    SWLOG_INFO( "SetJsonVars: cannot open %s for writing\n", fileOut );
                }
            }
            cJSON_ArrayForEach( item, json )
            {
                if( fpout || setenvvars )
                {
                    pName = item->string;
                    if( !cJSON_IsInvalid( item ) && pName )
                    {
                        convertInvalidChars( pName );
                        getitemval( item, cOut, sizeof(cOut) );
                        
                        iRet = writeItemVal( fpout, pName, cOut, setenvvars );
                    }
                    else
                    {
                        SWLOG_ERROR( "SetJsonVars: item is invalid\n" );
                    }
                }
            }
            if( fpout )
            {
                fclose( fpout );
            }
            cJSON_Delete( json );
        }
        else
        {
            SWLOG_ERROR( "SetJsonVar: cJSON_Parse error writing\n" );
        }
        free( pStr );
    }
    else
    {
        SWLOG_ERROR( "SetJsonVars: GetJson return NULL\n");
    }
    return iRet;
}

/* function ParseJsonStr - returns a pointer to a JSON object.
   Usage: JSON *ParseJsonStr <Input JSON String>
 
            Input JSON String - a pointer to a string containing the JSON to parse. Typically, this input is
            the return from the GetJson() function call.

            RETURN - a pointer to a JSON object if parse was successful, NULL otherwise.
 
            Function Notes - The JSON pointer return must be freed by the caller when no longer needed
                       otherwise a memory leak will occur. Free the Json pointer by calling
                       FreeJson().
*/

JSON *ParseJsonStr( char *pJsonStr )
{
    JSON *pjson = NULL;

    if( pJsonStr != NULL )
    {
        pjson = (JSON*)cJSON_Parse( pJsonStr );
    }
    else
    {
        SWLOG_ERROR( "ParseJsonStr Error: No JSON string to parse\n" );
    }
    return pjson;
}


/* function FreeJson - deletes a cJson object created by ParseJsonStr
   Usage: int FreeJson <Input JSON *pJson> 
            Input JSON *pJson - a pointer to a JSON object to free, This would have been
                       created by a previous call to ParseJsonStr(). 

            RETURN - 0 if successful, non-zero otherwise.
*/

int FreeJson( JSON *pJson )
{
    int ret = -1;

    if( pJson != NULL )
    {
        cJSON_Delete( (cJSON*)pJson );
        ret = 0;
    }
    else
    {
        SWLOG_ERROR( "FreeJson Error: No Json object to free\n" );
    }
    return ret;
}

/* function GetJsonItem - returns a pointer to the requested JSON object.
 
   Usage: JSON* GetJsonItem <Input JSON pointer> <Search String>
 
            Input JSON pointer - a pointer to a JSON object. This would be the return from ParseJsonStr()
            or another JSON object within a larger JSON object.

            Search String - a pointer to a JSON object name to be searched for.

            RETURN - a JSON pointer to the item requested if it is found, NULL otherwise.
 
            Function Notes - the function returns a JSON object within a larger JSON. It does not
            return a pointer to a "name : value" pair.
 
*/

JSON* GetJsonItem( JSON *pJson, char *pValToGet )
{
    JSON *item = NULL;

    if( pJson != NULL && pValToGet != NULL && *pValToGet )
    {
        item=(JSON *)cJSON_GetObjectItem( (cJSON *)pJson, pValToGet );
    }

    return item;
}

/* function GetJsonValFromString - searches a JSON string for a name which matches the search string.
 
   Usage: size_t GetJsonValFromString <Input JSON String> <Search String> <Output Location> <Size of Output Location>
            Input JSON String - a pointer to a string containing the JSON to parse. Typically, this is
            the return from the GetJson() function call.

            Search String - a pointer to a string to be searched for. This is the "name" part of the
            "name : value" pair in the input string.

            Output Location - a character pointer to store the output value if found.

            Size of Output Location - The size of the Output Location storage. This value includes a
            NULL terminator. Size values of 1 or greater ensure a NULL terminated string.

            RETURN - an integer value equal to the number of characters copied to the Output Location.

            Function Notes - The function can return a length greater then the maxlen argument. If this occurs,
            it's indicative that data was truncated to prevent the output buffer from being overrun. A larger
            buffer is required to hold the complete output string.
*/
size_t GetJsonValFromString( char *pJsonStr, char *pValToGet, char *pOutputVal, size_t maxlen )
{
    JSON *pjson;
    size_t szLen = 0;

    if( pJsonStr != NULL && pOutputVal != NULL )
    {
        pjson = ParseJsonStr( pJsonStr );
        if( pjson != NULL )
        {
            szLen = GetJsonVal( pjson, pValToGet, pOutputVal, maxlen );
            FreeJson( pjson );
        }
    }
    return szLen;
}

/* function GetJsonVal - searches a JSON string for a value.
 
   Usage: int GetJsonVal <Input JSON pointer> <Search String> <Output Location> <Size of Output Location>
 
            Input JSON pointer - a pointer to a JSON object. This would be the return from ParseJsonStr().

            Search String - a pointer to a string to be searched for. This is the "name" part of the 
            "name : value" pair in the input string.

            Output Location - a character pointer to store the output value if found.

            Size of Output Location - The size of the Output Location storage. This value includes a
            NULL terminator. Size values of 1 or greater ensure a NULL terminated string.

            RETURN - an integer value equal to the number of characters copied to the Output Location.
 
            Function Notes - The function can return a length greater then the maxlen argument. If this occurs,
            it's indicative that data was truncated to prevent the output buffer from being overrun. A larger
            buffer is required to hold the complete output string.
*/
size_t GetJsonVal( JSON *pJson, char *pValToGet, char *pOutputVal, size_t maxlen )
{
    cJSON *pcjson = NULL;
    cJSON *item = NULL;
    size_t len = 0;

    if( pOutputVal != NULL && maxlen )
    {
        *pOutputVal = 0;
        if( pJson != NULL )
        {
            pcjson = (cJSON*)pJson;
            if( pValToGet != NULL && *pValToGet )
            {
                if( (item=cJSON_GetObjectItem( pcjson, pValToGet )) )
                {
                    len = getitemval( item, pOutputVal, maxlen );
                }
            }
            else
            {
                SWLOG_ERROR( "GetJsonVal: Error, no value to get\n" );
            }
        }
        else
        {
            SWLOG_ERROR( "GetJsonVal: Error, no json object to search\n" );
        }
    }
    else
    {
        SWLOG_ERROR( "GetJsonVal: Error, no place to store output\n" );
    }
    return len;
}

/* function GetJsonValContainingFromString - searches a JSON string for a name which conains all or part of the search string.
   This works similar to "grep."
 
   Usage: size_t GetJsonValContainingFromString <Input JSON String> <Search String> <Output Location> <Size of Output Location>
            Input JSON String - a pointer to a string containing the JSON to parse. Typically, this is
            the return from the GetJson() function call.

            Search String - a pointer to a string to be searched for. This is the "name" part of the
            "name : value" pair in the input string.

            Output Location - a character pointer to store the output value if found.

            Size of Output Location - The size of the Output Location storage. This value includes a
            NULL terminator. Size values of 1 or greater ensure a NULL terminated string.

            RETURN - an integer value equal to the number of characters copied to the Output Location.

            Function Notes - either pJsonStr or pJson is required but not both. If both arguments are supplied, pJson
            will be used. If using pCJson, be sure it is still valid (has not been deleted). The function can return a length
            greater then the maxlen argument. If this occurs, it's indicative that data was truncated to prevent the
            output buffer from being overrun. A larger buffer is required to hold the complete output string.
*/
size_t GetJsonValContainingFromString( char *pJsonStr, char *pValToGet, char *pOutputVal, size_t maxlen )
{
    JSON *pjson;
    size_t szLen = 0;

    if( pJsonStr != NULL && pOutputVal != NULL )
    {
        pjson = ParseJsonStr( pJsonStr );
        if( pjson != NULL )
        {
            szLen = GetJsonValContaining( pjson, pValToGet, pOutputVal, maxlen );
            FreeJson( pjson );
        }
    }
    return szLen;
}

/* function GetJsonValContaining - searches a JSON string for a name which conains all or part of the search string.
   Usage: int GetJsonVal <Input JSON String> <Input void pointer> <Search String> <Output Location> <Size of Output Location>
            Input JSON String - a pointer to a string containing the JSON to parse. Typically, this is
            the return from the GetJson() function call. Either pass in a JSON string or a pointer to a cJson object.
            The cJson object pointer takes precedence if both are present.

            Input void pointer - a void pointer which will be cast to a cJson object. This would be the return from ParseJsonStr().
            Either pass in a JSON string or a void pointer. The void pointer takes precedence if
            both are present.

            Search String - a pointer to a string to be searched for. This is the "name" part of the
            "name : value" pair in the input string.

            Output Location - a character pointer to store the output value if found.

            Size of Output Location - The size of the Output Location storage. This value includes a
            NULL terminator. Size values of 1 or greater ensure a NULL terminated string.

            RETURN - an integer value equal to the number of characters copied to the Output Location.

            Function Notes - either pJsonStr or pJson is required but not both. If both arguments are supplied, pJson
            will be used. If using pCJson, be sure it is still valid (has not been deleted). The function can return a length
            greater then the maxlen argument. If this occurs, it's indicative that data was truncated to prevent the
            output buffer from being overrun. A larger buffer is required to hold the complete output string.
*/
size_t GetJsonValContaining( JSON *pJson, char *pValToGet, char *pOutputVal, size_t maxlen )
{
    cJSON *pcjson = NULL;
    cJSON *pcitem = NULL;
    char *pName;
    size_t len = 0;
    int i = 0, rem_space;

    if( pOutputVal != NULL && maxlen )
    {
        *pOutputVal = 0;
        if( pJson != NULL )
        {
            pcjson = (cJSON *)pJson;        
            if( pValToGet != NULL && *pValToGet )
            {
                cJSON_ArrayForEach( pcitem, pcjson )
                {
                    pName = pcitem->string;
                    if( !cJSON_IsInvalid( pcitem ) && pName )
                    {
                        convertInvalidChars( pName );
                        if( strstr( pName, pValToGet ) )
                        {
                            pName = pOutputVal + len;     // reuse pName - where to put item value
                            rem_space = maxlen - len;
                            if( i && rem_space >= 2 )     // if something in buffer and there's room for comma and space
                            {
                                *pName++ = ',';
                                ++len;               // we added 1 char ...
                                --rem_space;         // and have 1 char less space remaining
                            }
                            if( rem_space > 0 )         // if we have a bit more room
                            {
                                i = getitemval( pcitem, pName, rem_space );
                                len += i;           // len can exceed maxlen to trigger break
                                if( len >= maxlen )
                                {
                                    break;              //  break cJSON_ArrayForEach
                                }
                            }
                        }
                    }
                    else
                    {
                        SWLOG_ERROR( "GetJsonValContaining: item is invalid\n" );
                    }
                }
            }
            else
            {
                SWLOG_ERROR( "GetJsonValContaining: Error, no value to get\n" );
            }
        }
        else
        {
            SWLOG_ERROR( "GetJsonValContaining: Error, no json object to search\n" );
        }
    }
    else
    {
        SWLOG_ERROR( "GetJsonValContaining: Error, no place to store output\n" );
    }
    return len;
}

/* function GetJson - reads a json file and returns a pointer to a dynamically allocated
   (malloc'd) character array containing the JSON file. It is the caller's
   responsibilty to release the allocated memory using free().
   Usage: GetJson <Input JSON file>

            Input JSON file - filename containing the JSON to read.

            Returns - character pointer to string containing the contents of the JSON
            file if successful, NULL otherwise.
*/
char* GetJson( char *filename_in )
{
    struct stat statbuf;
    char *pJson = NULL;
    char *pTmp;
    char *pMap;
    char *pc;
    size_t stFilesize;
    size_t stI;
    int fd;

    if( filename_in != NULL )
    {
        if( (fd=open( filename_in, O_RDONLY)) != -1 )
        {
            if( fstat (fd, &statbuf) == 0 )
            {
                stFilesize = (size_t)statbuf.st_size;
                if( (pMap=(char *)mmap( NULL, stFilesize, PROT_READ, MAP_PRIVATE, fd, 0 )) != MAP_FAILED )
                {
                    pTmp = pJson = (char *)malloc( (stFilesize + 1) * sizeof(char) );
                    if( pJson != NULL )
                    {
                        *pJson = 0;
                        pc = pMap;
                        stI = stFilesize;
                        while( stI-- && *pc )
                        {
                            *pTmp++ = *pc++;
                        }
                        *pTmp = 0;
                    }
                    else
                    {
                        SWLOG_ERROR( "GetJson: Unable to allocate memory\n" );
                    }
                    munmap( pMap, stFilesize );
                }
                else
                {
                    SWLOG_ERROR( "GetJson: Unable to create mmap for %s\n", filename_in );
                }
            }
            else
            {
                SWLOG_ERROR( "GetJson: Unable to stat %s\n", filename_in );
            }
            close( fd );
        }
        else
        {
            SWLOG_ERROR( "GetJson: Unable to open %s for reading\n", filename_in );
        }
    }
    else
    {
        SWLOG_ERROR( "GetJson: No input filename\n" );
    }

    return pJson;
}

/* function IsJsonArray - checks to see if a JSON object is an array.
 
   Usage: bool IsJsonArray <Input JSON Object pointer>
 
            Input JSON Object - a pointer to the JSON object to be tested

            RETURN - true if object is an array, false otherwise.

*/
bool IsJsonArray( JSON *pJson )
{
    return( (bool)cJSON_IsArray( (cJSON *)pJson ) );
}

/* function IsJsonArray - checks to see if a JSON object is an array.
 
   Usage: unsigned GetJsonArraySize <Input JSON Object pointer>
 
            Input JSON Object - a pointer to the JSON object

            RETURN - the number of items in the JSON array. 0 (zero) if object is not an array

*/
unsigned GetJsonArraySize( JSON *pJson )
{
    unsigned num = 0;

    if( cJSON_IsArray( (cJSON *)pJson ) )
    {
        num = (unsigned)cJSON_GetArraySize( (cJSON *)pJson );
    }
    return num;
}

/* function GetJsonArrayItem - gets a JSON item from a JSON array.
 
   Usage: unsigned GetJsonArraySize <Pointer to array of JSON Objects> <Index of the JSON array item to return>
 
            Input JSON Object - a pointer to the JSON array.
 
            Input index - the JSON array item index to get

            RETURN - a pointer to a JSON item

*/
JSON* GetJsonArrayItem( JSON *pJson, unsigned index )
{

    return (JSON*)cJSON_GetArrayItem( (cJSON *)pJson, index );
}





