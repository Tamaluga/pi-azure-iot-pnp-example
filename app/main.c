// This sample implements an IoT Plug and Play based mini weather station (Temperature/Humidity/Preassure)

// Standard C header files
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// BCM
#include <sys/types.h>
#include <stdio.h>
#include <math.h>
#include "bcm2835.h"
#include "SHTC3.h"
#include "LPS22HB.h"

// IoTHub Device Client and IoT core utility related header files
#include "iothub.h"
#include "iothub_device_client_ll.h"
#include "iothub_message.h"
#include "iothub_client_options.h"
#include "iothubtransportmqtt.h"
#include "azure_c_shared_utility/threadapi.h"
#include "azure_c_shared_utility/xlogging.h"

#ifdef SET_TRUSTED_CERT_IN_SAMPLES
#include "azure_c_shared_utility/shared_util_options.h"
#include "certs.h"
#endif // SET_TRUSTED_CERT_IN_SAMPLES

#ifdef USE_PROV_MODULE_FULL
#include "pnp_dps_ll.h"
#endif // USE_PROV_MODULE_FULL

// JSON parser library
#include "parson.h"

// Environment variable used to specify how app connects to hub and the two possible values
static const char g_securityTypeEnvironmentVariable[] = "IOTHUB_DEVICE_SECURITY_TYPE";
static const char g_securityTypeConnectionStringValue[] = "connectionString";
static const char g_securityTypeDpsValue[] = "DPS";

// Environment variable used to specify this application's connection string
static const char g_connectionStringEnvironmentVariable[] = "IOTHUB_DEVICE_CONNECTION_STRING";

#ifdef USE_PROV_MODULE_FULL
// Environment variable used to specify this application's DPS id scope
static const char g_dpsIdScopeEnvironmentVariable[] = "IOTHUB_DEVICE_DPS_ID_SCOPE";

// Environment variable used to specify this application's DPS device id
static const char g_dpsDeviceIdEnvironmentVariable[] = "IOTHUB_DEVICE_DPS_DEVICE_ID";

// Environment variable used to specify this application's DPS device key
static const char g_dpsDeviceKeyEnvironmentVariable[] = "IOTHUB_DEVICE_DPS_DEVICE_KEY";

// Environment variable used to optionally specify this application's DPS id scope
static const char g_dpsEndpointEnvironmentVariable[] = "IOTHUB_DEVICE_DPS_ENDPOINT";

// Global provisioning endpoint for DPS if one is not specified via the environment
static const char g_dps_DefaultGlobalProvUri[] = "global.azure-devices-provisioning.net";

// Values of connection / security settings read from environment variables and/or DPS runtime
PNP_DEVICE_CONFIGURATION g_pnpDeviceConfiguration;
#endif // USE_PROV_MODULE_FULL

// Connection string used to authenticate device when connection strings are used
const char* g_pnpDeviceConnectionString;

// Amount of time to sleep between polling hub, in milliseconds.  Set to wake up every 100 milliseconds.
static unsigned int g_sleepBetweenPollsMs = 100;

// Every time the main loop wakes up, on the g_sendTelemetryPollInterval(th) pass will send a telemetry message.
// So we will send telemetry every (g_sendTelemetryPollInterval * g_sleepBetweenPollsMs) milliseconds; 60 seconds as currently configured.
static const int g_sendTelemetryPollInterval = 600;

// Whether verbose tracing at the IoTHub client is enabled or not.
static bool g_hubClientTraceEnabled = true;

// This device's PnP ModelId.
static const char g_RaspiModelId[] = "dtmi:quickstartapp:RaspberryPiSenseHatBSimple;1";

// Return codes for device methods and desired property responses
static int g_statusSuccess = 200;
static int g_statusBadFormat = 400;
static int g_statusNotFoundStatus = 404;
static int g_statusInternalError = 500;

// Current temperature of the SHTC3T.
static float g_shtTemperature = 0.0;
// Current humidity of the SHTC3T.
static float g_shtHumidity = 0.0;
// Current temperature of the LPS22HB.
static float g_lpsTemperature = 0.0;
// Current pressure of the LPS22HB.
static float g_lpsPressure = 0.0;

// Current location.
static double g_latitude = 46.94808603191136;
static double g_longitude = 7.459615618377996;
static double g_altitude = 0.0;

// Format string for sending sensor telemetry
static const char g_sensorTelemetryBodyFormat[] = "{\"temperature\":%.02f},{\"humidity\":%.02f},{\"pressure\":%.02f}";
// Format string for sending location telemetry
static const char g_locationTelemetryBodyFormat[] = "{\"location\":{\"lat\":%.010f,\"lon\":%.010f,\"alt\":%.010f}}";

// Size of buffer to store ISO 8601 time.
#define TIME_BUFFER_SIZE 128
// Format string to create an ISO 8601 time.  This corresponds to the DTDL datetime schema item.
static const char g_ISO8601Format[] = "%Y-%m-%dT%H:%M:%SZ";
// Start time of the program, stored in ISO 8601 format string for UTC.
char g_ProgramStartTime[TIME_BUFFER_SIZE];

//
// CopyTwinPayloadToString takes the twin payload data, which arrives as a potentially non-NULL terminated string, and creates
// a new copy of the data with a NULL terminator.  The JSON parser this sample uses, parson, only operates over NULL terminated strings.
//
static char* CopyTwinPayloadToString(const unsigned char* payload, size_t size)
{
    char* jsonStr;

    if ((jsonStr = (char*)malloc(size+1)) == NULL)
    {
        LogError("Unable to allocate %lu size buffer", (unsigned long)(size+1));
    }
    else
    {
        memcpy(jsonStr, payload, size);
        jsonStr[size] = '\0';
    }

    return jsonStr;
}

//
// BuildUtcTimeFromCurrentTime writes the current time, in ISO 8601 format, into the specified buffer
//
static bool BuildUtcTimeFromCurrentTime(char* utcTimeBuffer, size_t utcTimeBufferSize)
{
    bool result;
    time_t currentTime;
    struct tm * currentTimeTm;

    time(&currentTime);
    currentTimeTm = gmtime(&currentTime);

    if (strftime(utcTimeBuffer, utcTimeBufferSize, g_ISO8601Format, currentTimeTm) == 0)
    {
        LogError("snprintf on UTC time failed");
        result = false;
    }
    else
    {
        result = true;
    }

    return result;
}

//
// GetDesiredJson retrieves JSON_Object* in the JSON tree corresponding to the desired payload.
//
static JSON_Object* GetDesiredJson(DEVICE_TWIN_UPDATE_STATE updateState, JSON_Value* rootValue)
{
    JSON_Object* rootObject = NULL;
    JSON_Object* desiredObject;

    if ((rootObject = json_value_get_object(rootValue)) == NULL)
    {
        LogError("Unable to get root object of JSON");
        desiredObject = NULL;
    }
    else
    {
        if (updateState == DEVICE_TWIN_UPDATE_COMPLETE)
        {
            // For a complete update, the JSON from IoTHub will contain both "desired" and "reported" - the full twin.
            // We only care about "desired" in this sample, so just retrieve it.
            desiredObject = json_object_get_object(rootObject, "desired");
        }
        else
        {
            // For a patch update, IoTHub only sends "desired" portion of twin and does not explicitly put a "desired:" JSON envelope.
            // So here we simply need the root of the JSON itself.
            desiredObject = rootObject;
        }
    }

    return desiredObject;
}

//
// Raspi_DeviceTwinCallback is invoked by the IoT SDK when a twin - either full twin or a PATCH update - arrives.
//
static void Raspi_DeviceTwinCallback(DEVICE_TWIN_UPDATE_STATE updateState, const unsigned char* payload, size_t size, void* userContextCallback)
{
    // The device handle associated with this request is passed as the context, since we will need to send reported events back.
    IOTHUB_DEVICE_CLIENT_LL_HANDLE deviceClientLL = (IOTHUB_DEVICE_CLIENT_LL_HANDLE)userContextCallback;

    char* jsonStr = NULL;
    JSON_Value* rootValue = NULL;
    JSON_Object* desiredObject;
    JSON_Value* versionValue = NULL;
    JSON_Value* targetTemperatureValue = NULL;

    LogInfo("DeviceTwin callback invoked");

    if ((jsonStr = CopyTwinPayloadToString(payload, size)) == NULL)
    {
        LogError("Unable to allocate twin buffer");
    }
    else if ((rootValue = json_parse_string(jsonStr)) == NULL)
    {
        LogError("Unable to parse twin JSON");
    }
    else if ((desiredObject = GetDesiredJson(updateState, rootValue)) == NULL)
    {
        LogError("Cannot retrieve desired JSON object");
    }

    json_value_free(rootValue);
    free(jsonStr);
}

//
// Raspi_SendCurrentLocation sends a PnP telemetry indicating the current location
//
void Raspi_SendCurrentLocation(IOTHUB_DEVICE_CLIENT_LL_HANDLE deviceClientLL) 
{
    IOTHUB_MESSAGE_HANDLE messageHandle = NULL;
    IOTHUB_CLIENT_RESULT iothubResult;

    // Send location
    char stringBuffer[64];

    if (snprintf(stringBuffer, sizeof(stringBuffer), g_locationTelemetryBodyFormat, g_latitude, g_longitude, g_altitude) < 0)
    {
        LogError("snprintf of current location telemetry failed");
    }
    else if ((messageHandle = IoTHubMessage_CreateFromString(stringBuffer)) == NULL)
    {
        LogError("IoTHubMessage_CreateFromString failed");
    }
    else if ((iothubResult = IoTHubDeviceClient_LL_SendEventAsync(deviceClientLL, messageHandle, NULL, NULL)) != IOTHUB_CLIENT_OK)
    {
        LogError("Unable to send telemetry message, error=%d", iothubResult);
    }

    IoTHubMessage_Destroy(messageHandle);
}

//
// Raspi_SendCurrentTemperature sends a PnP telemetry indicating the current temperature
//
void Raspi_SendCurrentTemperature(IOTHUB_DEVICE_CLIENT_LL_HANDLE deviceClientLL) 
{
    IOTHUB_MESSAGE_HANDLE messageHandle = NULL;
    IOTHUB_CLIENT_RESULT iothubResult;

    // Get sensor values
    shtc3GetSensorValues(&g_shtTemperature, &g_shtHumidity);
    lpsGetSensorValues(&g_lpsPressure, &g_lpsTemperature);
    lpsGetSensorValues(&g_lpsPressure, &g_lpsTemperature);
    printf("Humidity: %6.2f%% | Temperature: %6.2f°C | Pressure = %6.2fhPa | Temperature = %6.2f°C\r\n", g_shtHumidity, g_shtTemperature, g_lpsPressure, g_lpsTemperature);

    // Send sensor data
    char stringBuffer[64];

    if (snprintf(stringBuffer, sizeof(stringBuffer), g_sensorTelemetryBodyFormat, g_shtTemperature, g_shtHumidity, g_lpsPressure) < 0)
    {
        LogError("snprintf of current temperature telemetry failed");
    }
    else if ((messageHandle = IoTHubMessage_CreateFromString(stringBuffer)) == NULL)
    {
        LogError("IoTHubMessage_CreateFromString failed");
    }
    else if ((iothubResult = IoTHubDeviceClient_LL_SendEventAsync(deviceClientLL, messageHandle, NULL, NULL)) != IOTHUB_CLIENT_OK)
    {
        LogError("Unable to send telemetry message, error=%d", iothubResult);
    }

    IoTHubMessage_Destroy(messageHandle);
}

//
// GetConnectionStringFromEnvironment retrieves the connection string based on environment variable
//
static bool GetConnectionStringFromEnvironment()
{
    bool result;

    if ((g_pnpDeviceConnectionString = getenv(g_connectionStringEnvironmentVariable)) == NULL)
    {
        LogError("Cannot read environment variable=%s", g_connectionStringEnvironmentVariable);
        result = false;
    }
    else
    {
#ifdef USE_PROV_MODULE_FULL
        g_pnpDeviceConfiguration.securityType = PNP_CONNECTION_SECURITY_TYPE_CONNECTION_STRING;
#endif
        result = true;    
    }

    return result;
}

//
// GetDpsFromEnvironment retrieves DPS configuration for a symmetric key based connection
// from environment variables
//
static bool GetDpsFromEnvironment()
{
#ifndef USE_PROV_MODULE_FULL
    // Explain to user misconfiguration.  The "run_e2e_tests" must be set to OFF because otherwise
    // the e2e's test HSM layer and symmetric key logic will conflict.
    LogError("DPS based authentication was requested via environment variables, but DPS is not enabled.");
    LogError("DPS is an optional component of the Azure IoT C SDK.  It is enabled with symmetric keys at cmake time by");
    LogError("passing <-Duse_prov_client=ON -Dhsm_type_symm_key=ON -Drun_e2e_tests=OFF> to cmake's command line");
    return false;
#else
    bool result;

    if ((g_pnpDeviceConfiguration.u.dpsConnectionAuth.endpoint = getenv(g_dpsEndpointEnvironmentVariable)) == NULL)
    {
        // We will fall back to standard endpoint if one is not specified
        g_pnpDeviceConfiguration.u.dpsConnectionAuth.endpoint = g_dps_DefaultGlobalProvUri;
    }

    if ((g_pnpDeviceConfiguration.u.dpsConnectionAuth.idScope = getenv(g_dpsIdScopeEnvironmentVariable)) == NULL)
    {
        LogError("Cannot read environment variable=%s", g_dpsIdScopeEnvironmentVariable);
        result = false;
    }
    else if ((g_pnpDeviceConfiguration.u.dpsConnectionAuth.deviceId = getenv(g_dpsDeviceIdEnvironmentVariable)) == NULL)
    {
        LogError("Cannot read environment variable=%s", g_dpsDeviceIdEnvironmentVariable);
        result = false;
    }
    else if ((g_pnpDeviceConfiguration.u.dpsConnectionAuth.deviceKey = getenv(g_dpsDeviceKeyEnvironmentVariable)) == NULL)
    {
        LogError("Cannot read environment variable=%s", g_dpsDeviceKeyEnvironmentVariable);
        result = false;
    }
    else
    {
        g_pnpDeviceConfiguration.securityType = PNP_CONNECTION_SECURITY_TYPE_DPS;
        result = true;    
    }

    return result;
#endif // USE_PROV_MODULE_FULL
}


//
// GetConfigurationFromEnvironment reads how to connect to the IoT Hub (using 
// either a connection string or a DPS symmetric key) from the environment.
//
static bool GetConnectionSettingsFromEnvironment()
{
    const char* securityTypeString;
    bool result;

    if ((securityTypeString = getenv(g_securityTypeEnvironmentVariable)) == NULL)
    {
        LogError("Cannot read environment variable=%s", g_securityTypeEnvironmentVariable);
        result = false;
    }
    else
    {
        if (strcmp(securityTypeString, g_securityTypeConnectionStringValue) == 0)
        {
            result = GetConnectionStringFromEnvironment();
        }
        else if (strcmp(securityTypeString, g_securityTypeDpsValue) == 0)
        {
            result = GetDpsFromEnvironment();
        }
        else
        {
            LogError("Environment variable %s must be either %s or %s", g_securityTypeEnvironmentVariable, g_securityTypeConnectionStringValue, g_securityTypeDpsValue);
            result = false;
        }
    }

    return result;    
}

//
// CreateDeviceClientLLHandle performs actual handle creation (but nothing more), depending
// on whether connection strings or DPS is used.
//
static IOTHUB_DEVICE_CLIENT_LL_HANDLE CreateDeviceClientLLHandle(void)
{
#ifdef USE_PROV_MODULE_FULL
    if (g_pnpDeviceConfiguration.securityType == PNP_CONNECTION_SECURITY_TYPE_DPS)
    {
        // Pass the modelId to DPS here AND later on to IoT Hub (see SetOption on OPTION_MODEL_ID) when
        // that connection is created.  We need to do both because DPS does not auto-populate the modelId
        // it receives on DPS connection to the IoT Hub.
        g_pnpDeviceConfiguration.modelId = g_RaspiModelId;
        g_pnpDeviceConfiguration.enableTracing = g_hubClientTraceEnabled;
        return PnP_CreateDeviceClientLLHandle_ViaDps(&g_pnpDeviceConfiguration);
    }
#endif
    return IoTHubDeviceClient_LL_CreateFromConnectionString(g_pnpDeviceConnectionString, MQTT_Protocol);
}

//
// CreateAndConfigureDeviceClientHandleForPnP creates a IOTHUB_DEVICE_CLIENT_LL_HANDLE for this application, setting its
// ModelId along with various callbacks.
//
static IOTHUB_DEVICE_CLIENT_LL_HANDLE CreateAndConfigureDeviceClientHandleForPnP(void)
{
    IOTHUB_DEVICE_CLIENT_LL_HANDLE deviceHandle = NULL;
    IOTHUB_CLIENT_RESULT iothubResult;
    bool urlAutoEncodeDecode = true;
    int iothubInitResult;
    bool result;

    // Before invoking ANY IoTHub Device SDK functionality, IoTHub_Init must be invoked.
    if ((iothubInitResult = IoTHub_Init()) != 0)
    {
        LogError("Failure to initialize client.  Error=%d", iothubInitResult);
        result = false;
    }
    // Create the deviceHandle itself.
    else if ((deviceHandle = CreateDeviceClientLLHandle()) == NULL)
    {
        LogError("Failure creating IotHub client.  Hint: Check your connection string or DPS configuration");
        result = false;
    }
    // Sets verbosity level
    else if ((iothubResult = IoTHubDeviceClient_LL_SetOption(deviceHandle, OPTION_LOG_TRACE, &g_hubClientTraceEnabled)) != IOTHUB_CLIENT_OK)
    {
        LogError("Unable to set logging option, error=%d", iothubResult);
        result = false;
    }
    // Sets the name of ModelId for this PnP device.
    // This *MUST* be set before the client is connected to IoTHub.  We do not automatically connect when the 
    // handle is created, but will implicitly connect to subscribe for device method and device twin callbacks below.
    else if ((iothubResult = IoTHubDeviceClient_LL_SetOption(deviceHandle, OPTION_MODEL_ID, g_RaspiModelId)) != IOTHUB_CLIENT_OK)
    {
        LogError("Unable to set the ModelID, error=%d", iothubResult);
        result = false;
    }
    // Sets the callback function that processes device twin changes from the IoTHub, which is the channel that PnP Properties are 
    // transferred over.  This will also automatically retrieve the full twin for the application. 
    else if ((iothubResult = IoTHubDeviceClient_LL_SetDeviceTwinCallback(deviceHandle, Raspi_DeviceTwinCallback, (void*)deviceHandle)) != IOTHUB_CLIENT_OK)
    {
        LogError("Unable to set device twin callback, error=%d", iothubResult);
        result = false;
    }
    // Enabling auto url encode will have the underlying SDK perform URL encoding operations automatically.
    else if ((iothubResult = IoTHubDeviceClient_LL_SetOption(deviceHandle, OPTION_AUTO_URL_ENCODE_DECODE, &urlAutoEncodeDecode)) != IOTHUB_CLIENT_OK)
    {
        LogError("Unable to set auto Url encode option, error=%d", iothubResult);
        result = false;
    }
#ifdef SET_TRUSTED_CERT_IN_SAMPLES
    // Setting the Trusted Certificate.  This is only necessary on systems without built in certificate stores.
    else if ((iothubResult = IoTHubDeviceClient_LL_SetOption(deviceHandle, OPTION_TRUSTED_CERT, certificates)) != IOTHUB_CLIENT_OK)
    {
        LogError("Unable to set the trusted cert, error=%d", iothubResult);
        result = false;
    }
#endif // SET_TRUSTED_CERT_IN_SAMPLES
    else
    {
        result = true;
    }

    if ((result == false) && (deviceHandle != NULL))
    {
        IoTHubDeviceClient_LL_Destroy(deviceHandle);
        deviceHandle = NULL;
    }

    if ((result == false) &&  (iothubInitResult == 0))
    {
        IoTHub_Deinit();
    }

    return deviceHandle;
}

int main(void)
{
    // Init BCM

    if (!bcm2835_init()) return 1;
	bcm2835_i2c_setSlaveAddress(SHTC3_I2C_ADDRESS);
	bcm2835_i2c_set_baudrate(10000);
    SHTC_SOFT_RESET();

    // Init Azure

    IOTHUB_DEVICE_CLIENT_LL_HANDLE deviceClientLL = NULL;

    if (GetConnectionSettingsFromEnvironment() == false)
    {
        LogError("Cannot read required environment variable(s)");
    }
    else if (BuildUtcTimeFromCurrentTime(g_ProgramStartTime, sizeof(g_ProgramStartTime)) == false)
    {
        LogError("Unable to output the program start time");
    }
    else if ((deviceClientLL = CreateAndConfigureDeviceClientHandleForPnP()) == NULL)
    {
        LogError("Failed creating IotHub device client");
    }
    else
    {
        LogInfo("Successfully created device client handle.  Hit Control-C to exit program\n");

        int numberOfIterations = 0;

        Raspi_SendCurrentLocation(deviceClientLL);

        while (true)
        {
            // Wake up periodically to poll.  Even if we do not plan on sending telemetry, we still need to poll periodically in order to process
            // incoming requests from the server and to do connection keep alives.
            if ((numberOfIterations % g_sendTelemetryPollInterval) == 0)
            {
                Raspi_SendCurrentTemperature(deviceClientLL);
            }

            IoTHubDeviceClient_LL_DoWork(deviceClientLL);
            ThreadAPI_Sleep(g_sleepBetweenPollsMs);
            numberOfIterations++;
        }

        // Clean up the iothub sdk handle
        IoTHubDeviceClient_LL_Destroy(deviceClientLL);
        // Free all the IoT SDK subsystem
        IoTHub_Deinit();        
    }

    return 0;
}
