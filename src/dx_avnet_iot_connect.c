/*
MIT License
Copyright (c) Avnet Corporation. All rights reserved.
Author: Brian Willess
Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "dx_avnet_iot_connect.h"

// This file implements the logic required to connect and interface with Avnet's IoTConnect cloud platform
// https://www.avnet.com/wps/portal/us/solutions/iot/software/iot-platform/

// Variables used to track IoTConnect connections details
static char dtgGUID[DX_AVNET_IOT_CONNECT_GUID_LEN + 1];
static char sidString[DX_AVNET_IOT_CONNECT_SID_LEN + 1];
static bool avnetConnected = false;
bool flag_99 = true;
IOTConnectCallback devicecallback = NULL;
unsigned char df_no;

// static EventLoopTimer *IoTCTimer = NULL;
static const int AVNET_IOT_DEFAULT_POLL_PERIOD_SECONDS = 15; // Wait for 15 seconds for IoT Connect to send first response

// Forward function declarations
static void MonitorAvnetConnectionHandler(EventLoopTimer *timer);
static void AvnetSendHelloTelemetry(void);
static IOTHUBMESSAGE_DISPOSITION_RESULT ReceiveMessageCallback(IOTHUB_MESSAGE_HANDLE, void *);
//static const char *ErrorCodeToString(int iotConnectErrorCode);
static bool SyncStore(JSON_Object *dProperties);
static void SetDeviceCallback(IOTConnectCallback);
static void IoTCdeviceallinfo(unsigned int);
static void SendAck(char *Ack_Data, char *Ack_time, int messageType);// Object  - will send the command ACK from D2C
static void DeviceCallback(char *payload);

static DX_TIMER_BINDING monitorAvnetConnectionTimer = {.name = "monitorAvnetConnectionTimer", .handler = MonitorAvnetConnectionHandler};

static void AvnetReconnectCallback(bool connected) {
    // Since we're going to be connecting or re-connecting to Azure
    // Set the IoT Connected flag to false
    avnetConnected = false;

    // Send the IoT Connect hello message to inform the platform that we're on-line!  We expect
    // to receive a hello response C2D message with connection details we need to send telemetry
    // data.
    AvnetSendHelloTelemetry();
    
    // Start the timer to make sure we see the IoT Connect "first response"
    // const struct timespec IoTCHelloPeriod = {.tv_sec = AVNET_IOT_DEFAULT_POLL_PERIOD_SECONDS, .tv_nsec = 0};
    // SetEventLoopTimerPeriod(IoTCTimer, &IoTCHelloPeriod);
    dx_timerChange(&monitorAvnetConnectionTimer, &(struct timespec){.tv_sec = AVNET_IOT_DEFAULT_POLL_PERIOD_SECONDS, .tv_nsec = 0});
}

// Call from the main init function to setup periodic timer and handler
void dx_avnetConnect(DX_USER_CONFIG *userConfig, const char *networkInterface)
{
    // Create the timer to monitor the IoTConnect hello response status
    if (!dx_timerStart(&monitorAvnetConnectionTimer)) {
        dx_terminate(DX_ExitCode_Init_IoTCTimer);
    }

    // Register to receive updates when the application receives an Azure IoTHub connection update
    // and C2D messages
    dx_azureRegisterConnectionChangedNotification(AvnetReconnectCallback);
    dx_azureRegisterMessageReceivedNotification(ReceiveMessageCallback);

    SetDeviceCallback(DeviceCallback);
    
    dx_azureConnect(userConfig, networkInterface, NULL);
}

/// <summary>
/// IoTConnect timer event:  Check for response status and send hello message
/// </summary>
static void MonitorAvnetConnectionHandler(EventLoopTimer *timer)
{
    if (ConsumeEventLoopTimerEvent(timer) != 0) {
        dx_terminate(DX_ExitCode_IoTCTimer_Consume);
        return;
    }

    // If we're not connected to IoTConnect, then fall through to re-send the hello message
    if (!avnetConnected) {

        if (dx_isAzureConnected()) {
            AvnetSendHelloTelemetry();
        }
    }
}

/// <summary>
///     Callback function invoked when a C2D message is received from IoT Hub.
/// </summary>
/// <param name="message">The handle of the received message</param>
/// <param name="context">The user context specified at IoTHubDeviceClient_LL_SetMessageCallback()
/// invocation time</param>
/// <returns>Return value to indicates the message procession status (i.e. accepted, rejected,
/// abandoned)</returns>
static IOTHUBMESSAGE_DISPOSITION_RESULT ReceiveMessageCallback(IOTHUB_MESSAGE_HANDLE message, void *context)
{
    Log_Debug("[AVT IoTConnect] Received C2D message\n");

    // Use a flag to track if we rx the dtg value
    bool dtgFlag = false;

    const unsigned char *buffer = NULL;
    size_t size = 0;
    if (IoTHubMessage_GetByteArray(message, &buffer, &size) != IOTHUB_MESSAGE_OK) {
        Log_Debug("[AVT IoTConnect] Failure performing IoTHubMessage_GetByteArray\n");
        return IOTHUBMESSAGE_REJECTED;
    }

    // 'buffer' is not null terminated, make a copy and null terminate it.
    unsigned char *str_msg = (unsigned char *)malloc(size + 1);
    if (str_msg == NULL) {
        Log_Debug("[AVT IoTConnect] Could not allocate buffer for incoming message\n");
        abort();
    }

    memcpy(str_msg, buffer, size);
    str_msg[size] = '\0';

    Log_Debug("[AVT IoTConnect] Received message '%s' from IoT Hub\n", str_msg);

    // Process the message.  We're expecting a specific JSON structure from IoT Connect
    //{
    //    "d": {
    //        "ec": 0,
    //        "ct": 200,
    //        "sid": "NDA5ZTMyMTcyNGMyNGExYWIzMTZhYzE0NTI2MTFjYTU=UTE6MTQ6MDMuMDA=",
    //        "meta": {
    //            "g": "0ac9b336-f3e7-4433-9f4e-67668117f2ec",
    //            "dtg": "9320fa22-ae64-473d-b6ca-aff78da082ed",
    //            "edge": 0,
    //            "gtw": "",
    //            "at": 1,
    //            "eg": "bdcaebec-d5f8-42a7-8391-b453ec230731"
    //        },
    //        "has": {
    //            "d": 0,
    //            "attr": 1,
    //            "set": 0,
    //            "r": 0,
    //            "ota": 0
    //        }
    //    }
    //}
    //
    // The code below will drill into the structure and pull out each piece of data and store it
    // into variables

    // Using the mesage string get a pointer to the rootMessage
    JSON_Value *rootMessage = NULL;
    rootMessage = json_parse_string(str_msg);
    if (rootMessage == NULL) {
        Log_Debug("[AVT IoTConnect] Cannot parse the string as JSON content.\n");
        goto cleanup;
    }

    // Using the rootMessage pointer get a pointer to the rootObject
    JSON_Object *rootObject = json_value_get_object(rootMessage);

    // Check to see if this is a command message
    if (json_object_has_value(rootObject, "cmdType") != 0)
    {
        char cmd_value[10]={0};
        //char dt[10] = {0};
        strncpy(cmd_value, (char *)json_object_get_string(rootObject, "cmdType"), 4);
        Log_Debug("command Type: %s\n",cmd_value);
        unsigned char cmdvalue = (unsigned char)strtol(cmd_value, NULL, 16);
        JSON_Object *data_obj1 = json_object_dotget_object(rootObject, "data");
        JSON_Value *data_obj = json_object_dotget_value(rootObject, "data");
        switch (cmdvalue)
        {
            
            case 0x01:
                Log_Debug("WARNING: got the 'cmdType' '0x01' for Device message \n");
                if(data_obj != NULL)
                {
                   // char dt[10] = {0};
                   // strncpy(dt, (char *)json_object_get_string(rootObject, "data"), 4);
                   // Log_Debug("ota data Type: %s\n",dt);
                    char *data = json_serialize_to_string(data_obj);
                   // Log_Debug("INFO: Received message '%s' from IoT Hub\n", str_msg);
                    Log_Debug("data: %s \n",data);
                    if(devicecallback != NULL)
                    {
                      devicecallback(data);
                      //TwinUpdateCallback(data);
                    }

                } 
                break;

            case 0x02:
                Log_Debug("WARNING: got the 'cmdType' '0x02' for OTA update \n");
                if(data_obj != NULL)
                {
                   // char dt[10] = {0};
                   // strncpy(dt, (char *)json_object_get_string(rootObject, "data"), 4);
                   // Log_Debug("ota data Type: %s\n",dt);
                    char *data = json_serialize_to_string(data_obj);
                   // Log_Debug("INFO: Received message '%s' from IoT Hub\n", str_msg);
                    Log_Debug("data: %s \n",data);
                    //if(devicecallback != NULL)
                    //{
                     // devicecallback(data);
                  //  }

                }
                break;

           
            case 0x10:
                 Log_Debug("WARNING: got the 'cmdType' '0x10' for Device message \n");
                 //calling_type(201);
                 IoTCdeviceallinfo(201);
                 break;
            
            case 0x17:
                 Log_Debug("WARNING: got the 'cmdType' '0x17' for ('df') Data Frequency update \n");
                if(data_obj1!= NULL)
                {
                    if (json_object_has_value(data_obj1, "df") >= 0)
	                 {
     	                df_no = (uint8_t)json_object_get_number(data_obj1, "df");
                        Log_Debug("data:df_no: %d\n", df_no);

                        // Update the data frequency data item, the timer handler that sends 
                        // telemetry should poll to see if this data changes and if so, use the
                        // new value.  If the value is zero, then the timer handler should not
                        // send any telemetry data to IoTConnect
                        syncInfo.df = df_no;

		            }
                    // else df < 0, don't do anything
                }

                break;
            
            case 0x11:
                 Log_Debug("WARNING: got the 'cmdType' '0x11' for Device message \n");
                 break;

            case 0x12:
                 Log_Debug("WARNING: got the 'cmdType' '0x12' for Device message \n");
                 break; 

            case 0x13:
                 Log_Debug("WARNING: got the 'cmdType' '0x13' for Device message \n");
                 break; 

            case 0x99:
                 Log_Debug("WARNING: got the 'cmdType' '0x99' for Device message \n");
                 //disconnect();
                JSON_Value *DISP_C = json_value_init_object();
                JSON_Object *DISP_obj_C = json_value_get_object(DISP_C);
        
		        json_object_dotset_string(DISP_obj_C, "sid", syncInfo.sid); 
		        //json_object_dotset_number(DISP_obj_C, "uniqueid",syncInfo.uniqueId);
	            json_object_dotset_string(DISP_obj_C, "guid", syncInfo.gGUID );
                json_object_dotset_boolean(DISP_obj_C, "ack", false );
                json_object_dotset_string(DISP_obj_C, "ackId", "");
                json_object_dotset_boolean(DISP_obj_C, "command",false );
                json_object_dotset_string(DISP_obj_C, "cmdType", "0x16");
		        char *DISP_Json_Data_C = json_serialize_to_string(DISP_C);
                Log_Debug("send Ack_Json_Data_C message: %s \n",DISP_Json_Data_C); 
                devicecallback(DISP_Json_Data_C);
                avnetConnected = false;
                flag_99 = false;

                break;         
            case 0x16:
                Log_Debug("WARNING: got the 'cmdType' \n");
                break;
            default:
                break;
        }	    
    }
    
// Using the root object get a pointer to the d object
    JSON_Object *dProperties = json_object_dotget_object(rootObject, "d");
    if (dProperties == NULL) 
    {
        Log_Debug("dProperties == NULL\n");
    }
    else if(dProperties != NULL)
    { // There is a "d" object, drill into it and pull the data
        if (json_object_has_value(dProperties, "ec") != 0) 
        {
            unsigned char ec = (unsigned char)json_object_get_number(dProperties, "ec");
            if (json_object_has_value(dProperties, "ct") != 0) 
            {
                syncInfo.commandType=(unsigned char)json_object_get_number(dProperties, "ct");
                Log_Debug("command Type: %d\n", syncInfo.commandType);
            }
            switch (ec)
            {
                case 0:
                    switch (syncInfo.commandType)
                    {
                        case 200:
                            //store sync call
                            // pull dtg and sid
                            dtgFlag = SyncStore(dProperties);
                            if(strnlen(syncInfo.sid, DX_AVNET_IOT_CONNECT_SID_LEN) == DX_AVNET_IOT_CONNECT_SID_LEN){
                                IoTCdeviceallinfo(210);
                            }
                            break;
                        case 210:
                            dtgFlag = SyncStore(dProperties);
                            break;
                        case 204:
                            // store sync call
                            break;
                        default:
                            break;
                    }
                    Log_Debug("\nINFO_IN :  Error Code : 0 'OK'\n");
                    break;
                case 1: 
				    Log_Debug("\nINFO_IN :  Error Code : 1 'DEVICE_NOT_REGISTERED'\n");
				    break;
			    case 2: 
				    Log_Debug("\nINFO_IN :  Error Code : 2 'AUTO_REGISTER'\n");
				    break;
			    case 3: 
				    Log_Debug("\nINFO_IN :  Error Code : 3 'DEVICE_NOT_FOUND'\n");
				    break;
			    case 4:
				    Log_Debug("\nINFO_IN :  Error Code : 4 'DEVICE_INACTIVE'\n");
				    break;
			    case 5: 
				    Log_Debug("\nINFO_IN :  Error Code : 5 'OBJECT_MOVED'\n");
				    break;
			    case 6: 
			        Log_Debug("\nINFO_IN :  Error Code : 6 'CPID_NOT_FOUND'\n");
				    break;
                default:
                    break;
            }
        }
      

        // Check to see if we received all the required data we need to interact with IoTConnect
        if (dtgFlag) {

            // Verify that the new dtg is a valid GUID, if not then we just received an empty dtg.
            if (DX_AVNET_IOT_CONNECT_GUID_LEN == strnlen(dtgGUID, DX_AVNET_IOT_CONNECT_GUID_LEN + 1)) {

                // Set the IoTConnect Connected flag to true
                avnetConnected = true;
                Log_Debug("[AVT IoTConnect] IoTCConnected\n");
            }
        } else {

            // Set the IoTConnect Connected flag to false
            avnetConnected = false;
            Log_Debug("[AVT IoTConnect] Did not receive all the required data from IoTConnect\n");
            Log_Debug("[AVT IoTConnect] !IoTCConnected\n");
        }
    }

cleanup:
    // Release the allocated memory.
    json_value_free(rootMessage);
    free(str_msg);

    return IOTHUBMESSAGE_ACCEPTED;
}

static void AvnetSendHelloTelemetry(void)
{

    // Send the IoT Connect hello message to inform the platform that we're on-line!
    JSON_Value *rootValue = json_value_init_object();
    JSON_Object *rootObject = json_value_get_object(rootValue);

    json_object_dotset_number(rootObject, "mt", 200);
    json_object_dotset_number(rootObject, "v", IOT_CONNECT_API_VERSION);

    char *serializedTelemetryUpload = json_serialize_to_string(rootValue);

    if (!dx_azurePublish(serializedTelemetryUpload, strnlen(serializedTelemetryUpload, 64), NULL, 0, NULL)) {

        Log_Debug("[AVT IoTConnect] IoTCHello message send error: %s\n", "error");
    }

    Log_Debug("[AVT IoTConnect] TX: %s\n", serializedTelemetryUpload);
    json_free_serialized_string(serializedTelemetryUpload);
    json_value_free(rootValue);
}


// Construct a new message that contains all the required IoTConnect data and the original telemetry
// message. Returns false if we have not received the first response from IoTConnect, if the
// target buffer is not large enough, or if the incoming data is not valid JSON.
bool dx_avnetJsonSerializePayload(const char *originalJsonMessage, char *modifiedJsonMessage, size_t modifiedBufferSize)
{

    bool result = false;

    // Verify that the incomming JSON is valid
    JSON_Value *rootProperties = NULL;
    rootProperties = json_parse_string(originalJsonMessage);
    if (rootProperties != NULL) {

        // Define the Json string format for sending telemetry to IoT Connect, note that the
        // actual telemetry data is inserted as the last string argument
        static const char IoTCTelemetryJson[] = "{\"sid\":\"%s\",\"dtg\":\"%s\",\"mt\":0,\"d\":[{\"d\":%s}]}";

        // Determine the largest message size needed.  We'll use this to validate the incoming target
        // buffer is large enough
        size_t maxModifiedMessageSize = strlen(originalJsonMessage) + DX_AVNET_IOT_CONNECT_METADATA;

        // Verify that the passed in buffer is large enough for the modified message
        if (maxModifiedMessageSize > modifiedBufferSize) {
            Log_Debug(
                "\n[AVT IoTConnect] "
                "ERROR: dx_avnetJsonSerializePayload() modified buffer size can't hold modified "
                "message\n");
            Log_Debug("                 Original message size: %d\n", strlen(originalJsonMessage));
            Log_Debug("Additional IoTConnect message overhead: %d\n", DX_AVNET_IOT_CONNECT_METADATA);
            Log_Debug("           Required target buffer size: %d\n", maxModifiedMessageSize);
            Log_Debug("            Actual target buffersize: %d\n\n", modifiedBufferSize);

            result = false;
            goto cleanup;
        }

        // Build up the IoTC message and insert the telemetry JSON
        snprintf(modifiedJsonMessage, maxModifiedMessageSize, IoTCTelemetryJson, sidString, dtgGUID, originalJsonMessage);
        result = true;

    }
    else{
        Log_Debug("[AVT IoTConnect] ERROR: dx_avnetJsonSerializePayload was passed invalid JSON\n");
    }

cleanup:
    // Release the allocated memory.
    json_value_free(rootProperties);

    return result;
}

bool dx_avnetJsonSerialize(char *jsonMessageBuffer, size_t bufferSize, int key_value_pair_count, ...)
{
    bool result = false;
    char *serializedJson = NULL;
    char *keyString = NULL;
    int dataType;

    // Prepare the JSON object for the telemetry data
    JSON_Value *root_value = json_value_init_object();
    JSON_Object *root_object = json_value_get_object(root_value);

    // Create a Json_Array
    JSON_Value *myArrayValue = json_value_init_array();
    JSON_Array *myArray = json_value_get_array(myArrayValue);

    // Prepare the JSON object for the telemetry data
    JSON_Value *array_value_object = json_value_init_object();
    JSON_Object *array_object = json_value_get_object(array_value_object);

    // Allocate a buffer that we used to dynamically create the list key names d.<keyname>
    char *keyBuffer = (char *)malloc(DX_AVNET_IOT_CONNECT_JSON_BUFFER_SIZE);
    if (keyBuffer == NULL) {
        Log_Debug("[AVT IoTConnect] ERROR: not enough memory to send telemetry.");
        return false;
    }

    // We need to format the data as shown below
    // "{\"sid\":\"%s\",\"dtg\":\"%s\",\"mt\": 0,\"dt\": \"%s\",\"d\":[{\"d\":<new telemetry "key": value pairs>}]}";

    serializedJson = NULL;
    json_object_dotset_string(root_object, "sid", sidString);
    json_object_dotset_string(root_object, "dtg", dtgGUID);
    json_object_dotset_number(root_object, "mt", 0);

    // Prepare the argument list
    va_list inputList;
    va_start(inputList, key_value_pair_count);

    // Consume the data in the argument list and build out the json
    while (key_value_pair_count--) {

        // Pull the data type from the list
        dataType = va_arg(inputList, int);

        // Pull the current "key"
        keyString = va_arg(inputList, char *);

        // "d.<newKey>: <value>"
        snprintf(keyBuffer, DX_AVNET_IOT_CONNECT_JSON_BUFFER_SIZE, "d.%s", keyString);
        switch (dataType) {

            // report current device twin data as reported properties to IoTHub
        case DX_JSON_BOOL:
            json_object_dotset_boolean(array_object, keyBuffer, va_arg(inputList, int));
            break;
        case DX_JSON_FLOAT:
        case DX_JSON_DOUBLE:
            json_object_dotset_number(array_object, keyBuffer, va_arg(inputList, double));
            break;
        case DX_JSON_INT:
            json_object_dotset_number(array_object, keyBuffer, va_arg(inputList, int));
            break;
        case DX_JSON_STRING:
            json_object_dotset_string(array_object, keyBuffer, va_arg(inputList, char *));
            break;
        default:
            result = false;
            goto cleanup;
        }
    }

    // Clean up the argument list
    va_end(inputList);

    // Add the telemetry data to the Json
    json_array_append_value(myArray, array_value_object);
    json_object_dotset_value(root_object, "d", myArrayValue);

    // Serialize the structure
    serializedJson = json_serialize_to_string(root_value);

    // Copy the new JSON into the buffer the calling routine passed in
    if (strlen(serializedJson) < bufferSize) {
        strncpy(jsonMessageBuffer, serializedJson, bufferSize);
        result = true;
    }

cleanup:
    // Clean up
    json_free_serialized_string(serializedJson);
    json_value_free(root_value);
    free(keyBuffer);

    return result;
}

bool dx_isAvnetConnected(void)
{
    return avnetConnected;
}

/*
static const char *ErrorCodeToString(int iotConnectErrorCode)
{
    switch (iotConnectErrorCode) {

    case AVT_errorCode_OK:
        return "OK";
    case AVT_errorCode_DEV_NOT_REGISTERED:
        return "device not registered";
    case AVT_errorCode_DEV_AUTO_REGISTERED:
        return "Device auto registered";
    case AVT_errorCode_DEV_NOT_FOUND:
        return "device not Found";
    case AVT_errorCode_DEV_INACTIVE:
        return "Inactive device";
    case AVT_errorCode_OBJ_MOVED:
        return "object moved";
    case AVT_errorCode_CPID_NOT_FOUND:
        return "CPID not found";
    case AVT_errorCode_COMPANY_NOT_FOUND:
        return "Company not found";
    default:
        return "Unknown error code";
    }
}
*/

static bool SyncStore(JSON_Object *dProperties)
{
    bool dtgFlag = false;
    if (json_object_has_value(dProperties, "sid") != 0) 
    {
        strncpy(syncInfo.sid, (char *)json_object_get_string(dProperties, "sid"), DX_AVNET_IOT_CONNECT_SID_LEN);
        strncpy(sidString,syncInfo.sid,DX_AVNET_IOT_CONNECT_SID_LEN);
        Log_Debug("sid: %s\n", syncInfo.sid);
    }
    
        // Check to see if the object contains a "meta" object 200 resp
    JSON_Object *metaProperties = json_object_dotget_object(dProperties, "meta");
    if (metaProperties == NULL) 
    {
        Log_Debug("metaProperties == NULL\n");
    }
    else
    {
        // The meta properties should have a "dtg" key
        if (json_object_has_value(metaProperties, "dtg") != 0) 
        {
            strncpy(syncInfo.dtg, (char *)json_object_get_string(metaProperties, "dtg"), DX_AVNET_IOT_CONNECT_GUID_LEN);
            Log_Debug("meta:dtg: %s\n", syncInfo.dtg);
            strncpy(dtgGUID,syncInfo.dtg, DX_AVNET_IOT_CONNECT_GUID_LEN);
            
            dtgFlag = true;
        }
        if (json_object_has_value(metaProperties, "g") != 0) 
        {
            strncpy(syncInfo.gGUID, (char *)json_object_get_string(metaProperties, "g"), DX_AVNET_IOT_CONNECT_GUID_LEN);
            Log_Debug("meta:g: %s\n", syncInfo.gGUID);
        }
        if (json_object_has_value(metaProperties, "edge") != 0) 
        {
            syncInfo.edge = (uint8_t)json_object_get_number(metaProperties, "edge");
            Log_Debug("meta:edge: %d\n", syncInfo.edge);
        }
        if (json_object_has_value(metaProperties, "df") != 0) 
        {
            syncInfo.df= (uint16_t)json_object_get_number(metaProperties, "df");
            Log_Debug("meta:df: %d\n", syncInfo.df);
        }
    }
    JSON_Object *childProperties = json_object_dotget_object(dProperties, "d");
    if(childProperties == NULL)
    {
        Log_Debug("childProperties == NULL\n");
    }
    else
    {
        //add here child data
        ;
    }

    JSON_Object *hasProperties = json_object_dotget_object(dProperties, "has");
    if (hasProperties == NULL) 
    {
        Log_Debug("hasProperties == NULL\n");
    }
    else
    {
        
        if (json_object_has_value(hasProperties, "d") != 0) 
        {
            syncInfo.hasChild = (uint8_t)json_object_get_number(hasProperties, "d");
            Log_Debug("has:child: %d\n", syncInfo.hasChild);
        } 
        if (json_object_has_value(hasProperties, "attr") != 0) 
        {
            syncInfo.hasAttr = (uint8_t)json_object_get_number(hasProperties, "attr");
            Log_Debug("has:attr: %d\n", syncInfo.hasAttr);
        } 
        if (json_object_has_value(hasProperties, "set") != 0) 
        {
                syncInfo.hasSet = (uint8_t)json_object_get_number(hasProperties, "set");
                Log_Debug("has:set: %d\n", syncInfo.hasSet);
        } 
        if (json_object_has_value(hasProperties, "r") != 0) 
        {
            syncInfo.hasRule = (uint8_t)json_object_get_number(hasProperties, "r");
            Log_Debug("has:r %d\n", syncInfo.hasRule);
        }
    }
    return dtgFlag;
}

static void IoTCdeviceallinfo(unsigned int CT)
{

    // Send the IoT Connect hello message to inform the platform that we're on-line!
    JSON_Value *rootValue = json_value_init_object();
    JSON_Object *rootObject = json_value_get_object(rootValue);

    json_object_dotset_number(rootObject, "mt", CT);
    json_object_dotset_number(rootObject, "v", IOT_CONNECT_API_VERSION);
    json_object_dotset_string(rootObject,"sid",syncInfo.sid);//"NWVlMzRhYmRmZDhlNGRmODg0N2Q2Y2Q4ZTBkZDY2NDU=UDI6MTQ6MDMuMDA="

    char *serializedTelemetryUpload = json_serialize_to_string(rootValue);

    if (!dx_azurePublish(serializedTelemetryUpload, strnlen(serializedTelemetryUpload, 64), NULL, 0, NULL)) {

        Log_Debug("[AVT IoTConnect] IoTCHello message send error: %s\n", "error");
    }

    Log_Debug("[AVT IoTConnect] TX: %s\n", serializedTelemetryUpload);
    json_free_serialized_string(serializedTelemetryUpload);
    json_value_free(rootValue);

}

// Object  - will send the command ACK from D2C
 // param  - @Ack_Data - data from firm with ACK_ID
 // 		- @msgType - ACK for OTA or device Command
 // return - None
 
static void SendAck(char *Ack_Data, char *Ack_time, int msgType)
{
    JSON_Value *ACK = json_value_init_object();
    JSON_Object *Ack_obj = json_value_get_object(ACK);

    JSON_Value *Ack_json = json_parse_string(Ack_Data);
    
	if(Ack_json == NULL)
    {
		Log_Debug("ERR_CM03 [ %s - %s ] : Invalid data type to send the acknowledgment. It should be in 'object' type\r\n", syncInfo.sid, syncInfo.dtg);	
		return ;
	}
    else
    {
        json_object_dotset_string(Ack_obj, "sid", sidString);
		json_object_dotset_number(Ack_obj, "mt", msgType);
		json_object_dotset_string(Ack_obj, "dt", Ack_time);
		json_object_set_value(Ack_obj, "d", Ack_json);
		
        char *serializedTelemetryUpload = json_serialize_to_string(ACK);
        if (!dx_azurePublish(serializedTelemetryUpload, strnlen(serializedTelemetryUpload, 256), NULL, 0, NULL)) {

            Log_Debug("WARNING: Could not send telemetry to cloud\n");
        }

        Log_Debug("send ACK message: %s\n",serializedTelemetryUpload);
        
    }

}

static void DeviceCallback(char *payload)
{
   Log_Debug("payload: %s\n",payload);
    int Status = 0; 
    int msgType = 0;
	//CMD = 0;
    char ackid[64+1],cmdvalue[64+1];
    bool CMD;
    char AckTime[24 + 1] = "2021-07-15T11:45:59.968Z";
	//char *value=NULL, *cmd_ackID=NULL, *cmd_UNI = "";
	//json_object *root_value, *in_url, *sub_value, *Ack_Json;
	 //root_value = cJSON_Parse(payload);
    JSON_Value *root_value = NULL;
    root_value = json_parse_string(payload);
        
    if (root_value == NULL)  
    {
        Log_Debug("Not able to get Payload \n");
        //const char *error_ptr = cJSON_GetErrorPtr();    
    }
    JSON_Object *rootObj = json_value_get_object(root_value);

    //JSON_Object *Properties = json_object_dotget_object(rootObj, "ackId");
    if (json_object_has_value(rootObj, "ackId") != 0) 
    {
    
        strncpy(ackid, (char *)json_object_get_string(rootObj, "ackId"), DX_AVNET_IOT_CONNECT);
        Log_Debug("data:ackid: %s\n", ackid);
        
    }
    if (json_object_has_value(rootObj, "cmdType") != 0) 
    {
    
        strncpy(cmdvalue, (char *)json_object_get_string(rootObj, "cmdType"), DX_AVNET_IOT_CONNECT);
        Log_Debug("data:cmdtype: %s\n", cmdvalue);
        
    }
    if(strcmp(cmdvalue,"0x16") ==0)
    {
        
        CMD = (uint8_t) json_object_get_boolean(rootObj, "command");
        if(CMD == true)
        {
        Log_Debug("\r\n\t ** Device Connected ** \r\n");
        }
        else if(CMD == false)
        {
        Log_Debug("\r\n\t ** Device Disconnected ** \r\n");
        }
    return ;
    }
    if(strcmp(cmdvalue,"0x01") ==0)
    {
    Status = 6; 
    msgType = 5;
    Log_Debug("inside status and MSG \n");
    }
    JSON_Value *ACK_C = json_value_init_object();
    JSON_Object *Ack_obj_C = json_value_get_object(ACK_C);
    
    json_object_dotset_string(Ack_obj_C, "ackId", ackid);
    
    json_object_dotset_number(Ack_obj_C, "st", Status);
    json_object_dotset_string(Ack_obj_C, "msg", "");

    char *Ack_Json_Data_C = json_serialize_to_string(ACK_C);
    Log_Debug("send Ack_Json_Data_C message: %s \n",Ack_Json_Data_C);
    
    SendAck(Ack_Json_Data_C,AckTime,msgType);
}

static void SetDeviceCallback(IOTConnectCallback callback)
{
   devicecallback = callback;
  
}

/// <summary>
/// This routine returns the frequency value from IoTConnect  
/// </summary>
/// <returns></returns>
int dx_avnetGetTelemetryPeriod(void){

    if(dx_isAvnetConnected()){
        return syncInfo.df;
    }
    else{
        return -1;
    }
}

