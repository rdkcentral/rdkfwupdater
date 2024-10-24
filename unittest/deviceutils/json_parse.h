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

#ifndef __JSON_PARSE_H_
#define __JSON_PARSE_H_

#include <cjson/cJSON.h>

typedef cJSON   JSON;

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
int SetJsonVars(char *fileIn, char *fileOut, int setenvvars);

/* function ParseJsonStr - returns a pointer to a JSON object.
   Usage: JSON *ParseJsonStr <Input JSON String>
 
            Input JSON String - a pointer to a string containing the JSON to parse. Typically, this input is
            the return from the GetJson() function call.

            RETURN - a pointer to a JSON object if parse was successful, NULL otherwise.
 
            Function Notes - The JSON pointer return must be freed by the caller when no longer needed
                       otherwise a memory leak will occur. Free the Json pointer by calling
                       FreeJson().
*/
JSON *ParseJsonStr(char *pJsonStr);

/* function FreeJson - deletes a JSON object created by ParseJsonStr
   Usage: int FreeJson <Input JSON *pJson> 
            Input JSON *pJson - a pointer to a JSON object to free, This would have been
                       created by a previous call to ParseJsonStr(). 

            RETURN - 0 if successful, non-zero otherwise.
*/
int FreeJson(JSON *pJson);

/* function GetJsonItem - returns a pointer to the requested JSON object.
 
   Usage: JSON* GetJsonItem <Input JSON pointer> <Search String>
 
            Input JSON pointer - a pointer to a JSON object. This would be the return from ParseJsonStr()
            or another JSON object within a larger JSON object.

            Search String - a pointer to a JSON object name to be searched for.

            RETURN - a JSON pointer to the item requested if it is found, NULL otherwise.
 
            Function Notes - the function returns a JSON object within a larger JSON. It does not
            return a pointer to a "name : value" pair.
 
*/
JSON* GetJsonItem(JSON *pJson, char *pValToGet);

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
size_t GetJsonValFromString(char *pJsonStr, char *pValToGet, char *pOutputVal, size_t maxlen);

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
size_t GetJsonVal(JSON *pJson, char *pValToGet, char *pOutputVal, size_t maxlen);

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
size_t GetJsonValContainingFromString(char *pJsonStr, char *pValToGet, char *pOutputVal, size_t maxlen);

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
size_t GetJsonValContaining(JSON *pJson, char *pValToGet, char *pOutputVal, size_t maxlen);

/* function GetJson - reads a json file and returns a pointer to a dynamically allocated
   (malloc'd) character array containing the JSON file. It is the caller's
   responsibilty to release the allocated memory using free().
   Usage: GetJson <Input JSON file>

            Input JSON file - filename containing the JSON to read.

            Returns - character pointer to string containing the contents of the JSON
            file if successful, NULL otherwise.
*/
char* GetJson(char *filename_in);

/* function IsJsonArray - checks to see if a JSON object is an array.
 
   Usage: bool IsJsonArray <Input JSON Object pointer>
 
            Input JSON Object - a pointer to the JSON object to be tested

            RETURN - true if object is an array, false otherwise.

*/
bool IsJsonArray(JSON *pJson);

/* function GetJsonArraySize - returns the number of items in a JSON array.
 
   Usage: unsigned GetJsonArraySize <Input JSON Object pointer>
 
            Input JSON Object - a pointer to the JSON object

            RETURN - the number of items in the JSON array. 0 (zero) if object is not an array

*/
unsigned GetJsonArraySize(JSON *pJson);

/* function GetJsonArrayItem - gets a JSON item from a JSON array.
 
   Usage: unsigned GetJsonArraySize <Pointer to array of JSON Objects> <Index of the JSON array item to return>
 
            Input JSON Object - a pointer to the JSON array.
 
            Input index - the JSON array item index to get

            RETURN - a pointer to a JSON item

*/
JSON* GetJsonArrayItem(JSON *pJson, unsigned index);


#endif
