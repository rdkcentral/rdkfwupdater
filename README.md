# Description -- Detail code flow For rdkvfwupgrader

## File and Folder  Details

- rdkv_main: This is the main module which will use all the below module to do the download and flashing to the memory.
- cedmInterface: This component is responsible for getting cedm details like certificate and key from the device. This certificate and key is use for communicate with XCONF and SSR server.
- deviceutils: This component is responsible to get all device information like build info, partner id, mac address etc.
- flash: This component is responsible to flash the image to the flash memory after download completed.
- iarmInterface: This component is provide api interface to use IARM related call.
- jsonprocess: This component is responsible for processing the xconf response and parse the json file. Then create a data structure to use further.
- rfcInterface: This component provided rfc api to use for getting rfc value and setting rfc value.
- dwnlUtils: This a share library and provide all the download api using curl interface.
- parsejson: This is a share library and provide all the json parser api using cjson.
- utils: This is a share library and provide all the device specific information by reading device configure file or using some api.

## Communication Between Script file and App:

- "swupdate_utility.sh" is the script file which used for firmware download. "rdkvfwupgrader" application called by "swupdate_utility.sh" with required parameter as part of command line.

# Function Details

# main():

- This function is the starting point of this application. This function is responsible for all type of initialization like logging, iarm event, installing signal, logging triger type, checking either any other instance of this process is running or not.
  Once all the above operation complted then it call MakeXconfComms() function.
  
## MakeXconfComms():

- This function create http XCONF request by reading all the device details and send the request to XCONF server.

## processJsonResponse():

- Once XCONF response received then this function will do processing of the response and create the data structure for use.

## checkTriggerUpgrade():

- Once the processing complete then this function read the data structure and trigger the download one by one.

## upgradeRequest():

- This is function is the wrapper function of all the download function. Inside this function all the business logic written to select which type of download xconf, codebig, direct, retrydownload and fallback download.

## downloadFile():

- Use for download image from Direct server both XCONF and SSR. This function also support state red recovery download.

## codebigdownloadFile():

- Use for download image from codebig server both XCONF and SSR.

## doCurlInit():

- This function initialize the curl instance and return the CURL pointer. This pointer is use for download image from the server.

## doHttpFileDownload():

- This Function is uing curl lib and connecting to SSR server. Setting all required curl relted option. And returning downlaod file size. curl status  and http status sending via parameter.

## doStopDownload():

- Release the curl resource and stop the download.

## retryDownload():

- This function try to trigger download again if it fail first time.

## fallBack():

- This function is use for switch between direct and codebig download. If the direct download is fail 3 times with http 0 error code then codebig download will trigger and vice versa.

## filePresentCheck():

- This functions check either file is present or not.

## eventManager():

- This Function use for sending status to event manager.

## isStateRedSupported():

- This Function checks either state red support is available or not.

## isInStateRed():

- Check alreday is in state red or not.

## updateFWDownloadStatus():

- Update firmware download status inside status file.

## checkAndEnterStateRed():

- Enter to state red if state red support is present.

## getRFCSettings():
- Reading all required rfc.

## read_RFCProperty():
- Reading rfc using rfc api


