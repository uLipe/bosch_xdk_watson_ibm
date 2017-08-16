/******************************************************************************
**	COPYRIGHT (c) 2016		Bosch Connected Devices and Solutions GmbH
**
**	The use of this software is subject to the XDK SDK EULA
**
*******************************************************************************
**
**	OBJECT NAME:	mqttClient.c
**
**	DESCRIPTION:	Source Code for the MQTT Paho Client
**
**	PURPOSE:        Initializes the Paho Client and sets up subscriptions,
**	                starts the task to pubish and receive data,
**	                initializes the timer to stream data,
**	                defines the callback function for subscibed topics
**
**	AUTHOR(S):		Bosch Connected Devices and Solutions GmbH (BCDS)
**
**	Revision History:
**
**	Date			 Name		Company      Description
**  2016.Apr         crk        BCDS         Initial Release
**
*******************************************************************************/

/* system header files */
#include <stdint.h>

/* own header files */
#include "mqttPahoClient.h"
#include "mqttConfig.h"
#include "mqttSensor.h"

/* additional interface header files */
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "PTD_portDriver_ph.h"
#include "PTD_portDriver_ih.h"
#include "WDG_watchdog_ih.h"

/* paho header files */
#include "MQTTConnect.h"
#include "cJSON.h"

/* constant definitions ***************************************************** */
#define MQTT_JSON_OFFSET


/* local variables ********************************************************** */
// Buffers
static unsigned char buf[CLIENT_BUFF_SIZE];
static unsigned char readbuf[CLIENT_BUFF_SIZE];


// Client Task/Timer Variables
static xTimerHandle     clientStreamTimerHandle      = POINTER_NULL;  // timer handle for data stream
static xTaskHandle      clientTaskHandler            = POINTER_NULL;  // task handle for MQTT Client
static uint8_t clientDataGetFlag = NUMBER_UINT8_ZERO;
static uint32_t clientMessageId = 0;

// Subscribe topics variables
char clientTopicRed[CLIENT_BUFF_SIZE];
char clientTopicOrange[CLIENT_BUFF_SIZE];
char clientTopicYellow[CLIENT_BUFF_SIZE];
char clientTopicDataGet[CLIENT_BUFF_SIZE];
char clientTopicVoice[CLIENT_BUFF_SIZE];

char clientTopicDataStream[CLIENT_BUFF_SIZE];
const char *clientTopicRed_ptr = TOPIC_LED_RED;
const char *clientTopicOrange_ptr = TOPIC_LED_ORANGE;
const char *clientTopicYellow_ptr = TOPIC_LED_YELLOW;
const char *clientTopicDataGet_ptr = TOPIC_DATA_GET;
const char *clientTopicVoice_ptr = COMMAND_VOICE;


const char *clientTopicDataStream_ptr = TOPIC_DATA_STREAM;


/* global variables ********************************************************* */
// Network and Client Configuration
Network n;
Client c;

/* inline functions ********************************************************* */

/* local functions ********************************************************** */
static void clientRecv(MessageData* md);
static void clientTask(void *pvParameters);

/**
 * @brief callback function for subriptions
 *        toggles LEDS or sets read data flag
 *
 * @param[in] md - received message from the MQTT Broker
 *
 * @return NONE
 */
static void clientRecv(MessageData* md)
{
	/* Initialize Variables */
	MQTTMessage* message = md->message;

	if((strncmp(md->topicName->lenstring.data, clientTopicVoice_ptr, md->topicName->lenstring.len) == 0)) {

		printf("Subscribed Topic, %.*s, Message Received: %.*s\r\n", md->topicName->lenstring.len, md->topicName->lenstring.data,
				                                                   (int)message->payloadlen, (char*)message->payload);

		/* find the action desired in JSON file */
		cJSON * root = cJSON_Parse((char *)message->payload);
		cJSON * data = cJSON_GetObjectItemCaseSensitive(root, "d");
		cJSON * action = cJSON_GetObjectItemCaseSensitive(data, "action");


		if(!strcmp(action->valuestring, "start")) {
			clientStartTimer();
		} else if (!strcmp(action->valuestring, "stop")) {
			clientStopTimer();
		} else {
			printf("Spurious event, discarding! \n\r");
		}
	}

}

/**
 * @brief publish sensor data, get sensor data, or
 *        yield mqtt client to check subscriptions
 *
 * @param[in] pvParameters UNUSED/PASSED THROUGH
 *
 * @return NONE
 */
static void clientTask(void *pvParameters)
{
	/* Initialize Variables */
	MQTTMessage msg;

	/* Forever Loop Necessary for freeRTOS Task */
    for(;;)
    {
    	WDG_feedingWatchdog();
    	/* Publish Live Data Stream */
        if(sensorStreamBuffer.length > NUMBER_UINT32_ZERO)
        {

        	printf("clientTask(): publishing sensor stream! \n\r");
            msg.id = clientMessageId++;
            msg.qos = 0;
            msg.payload = sensorStreamBuffer.data;
            msg.payloadlen = sensorStreamBuffer.length;
            MQTTPublish(&c, clientTopicDataStream_ptr, &msg);

        	memset(sensorStreamBuffer.data, 0x00, SENSOR_DATA_BUF_SIZE);
        	sensorStreamBuffer.length = NUMBER_UINT32_ZERO;
        }
        else if(clientDataGetFlag) {
        	printf("clientTask(): preparing stream! \n\r");
        	sensorStreamData(pvParameters);
        	clientDataGetFlag = DISABLED;
        }
        else {
            MQTTYield(&c, CLIENT_YIELD_TIMEOUT);
        }
    }
}

/* global functions ********************************************************* */

/**
 * @brief starts the data streaming timer
 *
 * @return NONE
 */
void clientStartTimer(void)
{
	/* Start the timers */
	xTimerStart(clientStreamTimerHandle, UINT32_MAX);
	return;
}
/**
 * @brief stops the data streaming timer
 *
 * @return NONE
 */
void clientStopTimer(void)
{
	/* Stop the timers */
	xTimerStop(clientStreamTimerHandle, UINT32_MAX);
	return;
}

/**
 * @brief Initializes the MQTT Paho Client, set up subscriptions and initializes the timers and tasks
 *
 * @return NONE
 */
void clientInit(void)
{
	/* Initialize Variables */
    int rc = 0;
	WDG_feedingWatchdog();

    NewNetwork(&n);
    ConnectNetwork(&n, MQTT_BROKER_NAME, MQTT_PORT);
    MQTTClient(&c, &n, 1000, buf, CLIENT_BUFF_SIZE, readbuf, CLIENT_BUFF_SIZE);

    /* Configure the MQTT Connection Data */
    MQTTPacket_connectData data = MQTTPacket_connectData_initializer;
    data.willFlag = 0;
    data.MQTTVersion = 3;
    data.clientID.cstring = MQTT_CLIENT_ID;
    data.keepAliveInterval = 100;
    data.cleansession = 1;
    data.password.cstring = IBM_BLUEMIX_DEVICE_TOKEN;
    data.username.cstring = IBM_BLUEMIX_USERNAME;

    printf("Connecting to %s %d\r\n", MQTT_BROKER_NAME, MQTT_PORT);

    /* Connect to the MQTT Broker */
	WDG_feedingWatchdog();
    rc = MQTTConnect(&c, &data);


	memset(clientTopicVoice, 0x00, CLIENT_BUFF_SIZE);
	sprintf((char*) clientTopicVoice, COMMAND_VOICE, (const char*) MQTT_CLIENT_ID);
	clientTopicVoice_ptr = (char*) clientTopicVoice;


	memset(clientTopicDataStream, 0x00, CLIENT_BUFF_SIZE);
	sprintf((char*) clientTopicDataStream, TOPIC_DATA_STREAM, (const char*) MQTT_CLIENT_ID);
	clientTopicDataStream_ptr = (char*) clientTopicDataStream;


	/* Subscribe to receive voice commands */
	rc = MQTTSubscribe(&c, clientTopicVoice_ptr, QOS0, clientRecv);


	/* Create Live Data Stream Timer */
    clientStreamTimerHandle = xTimerCreate(
			(const char * const) "Data Stream",
			STREAM_RATE,
			TIMER_AUTORELOAD_ON,
			NULL,
			sensorStreamData);

	/* Create MQTT Client Task */
    rc = xTaskCreate(clientTask, (const char * const) "Mqtt Client App",
                    		CLIENT_TASK_STACK_SIZE, NULL, CLIENT_TASK_PRIORITY, &clientTaskHandler);

    /* Error Occured Exit App */
    if(rc < 0)
    {
    	clientDeinit();
    }

    return;
}

/**
 * @brief Disconnect from the MQTT Client
 *
 * @return NONE
 */
void clientDeinit(void)
{
    MQTTDisconnect(&c);
    n.disconnect(&n);
}
