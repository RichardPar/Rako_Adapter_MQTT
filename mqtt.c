
#include <sys/time.h>
#include <string.h>
#include <stdlib.h>
#include <syslog.h>

#include "mqtt.h"
#include "list.h"

void connlost(void* context, char* cause);
int messageArrived(void *context, char *topicName, int topicLen, MQTTAsync_message *message);
void onSubscribe(void* context, MQTTAsync_successData* response);
void onSubscribeFailure(void* context, MQTTAsync_failureData* response);

void mqtt_initfuncs(void)
{
   
   syslog (LOG_NOTICE, "RAKO_MQTT Init %d", getuid ());
   mqtt_funcs = 0;
}


void mqtt_subscribe(char *tag,void *ptr)
{
    int rc;
   

   	MQTTAsync_responseOptions ropts = MQTTAsync_responseOptions_initializer;
    
    ropts.onSuccess = onSubscribe;
	ropts.onFailure = onSubscribeFailure;
	ropts.context = ptr;

    rc = MQTTAsync_subscribe(client, tag, QOS, &ropts);
    
    syslog(LOG_NOTICE,"Subscribing to %s (rc %d)\r\n",tag,rc);
        
}


void mqtt_register_callback(char *inNode,void *func, void *ptr)
{
    mqtt_callback_ll *tmp;
    char node[255];
    
    sprintf(node,"homeassistant/%s",inNode);
    
    tmp = malloc(sizeof(mqtt_callback_ll));
    strcpy(tmp->node,node);
    tmp->functionPtr = func;
    tmp->dataPtr= ptr;
    tmp->subscribed=0;
    DL_APPEND(mqtt_funcs, tmp);
    
    syslog(LOG_NOTICE,"%s %s\n",__FUNCTION__,inNode);
    
    if (MQTTAsync_isConnected(client)) {
       mqtt_subscribe(node,tmp);
       syslog(LOG_NOTICE,"CONNECTED : Callback Registed %s\n",inNode);
    }
    
    return;
}

void onPublishFailure(void* context, MQTTAsync_failureData* response)
{
	syslog(LOG_NOTICE,"Publish failed, rc %d\n", response ? -1 : response->code);
	 
}


void onPublish(void* context, MQTTAsync_successData* response)
{
	MQTTAsync client = (MQTTAsync)context;
}


int mqtt_writeresponse(char *intag, char *message, int transaction)
{
    char outMessage[8192];
    char outTag[255];
    
    sprintf(outMessage,"%s",message);
    sprintf(outTag,"homeassistant/%s",intag);

    int rc = mqtt_writedata(outTag,outMessage);
    
    
    if (rc == MQTTASYNC_SUCCESS)
    {
      return 0;
    }

   syslog(LOG_NOTICE,"%s FAILED with code %d\n",__FUNCTION__,rc);
   return -1;  
}


int mqtt_writedata(char* tag, char* message)
{
   int rc;
   
    MQTTAsync_responseOptions pub_opts = MQTTAsync_responseOptions_initializer;
    pub_opts.onSuccess = onPublish;
    pub_opts.onFailure = onPublishFailure;
    
	rc = MQTTAsync_send(client, tag, strlen(message), message,QOS,2, &pub_opts);
    
    return rc;
}


void onSubscribe(void* context, MQTTAsync_successData* response)
{
    
    mqtt_callback_ll *tmp = context;

    tmp->subscribed=1;

}


void onSubscribeFailure(void* context, MQTTAsync_failureData* response)
{
   mqtt_callback_ll *tmp = context;

	syslog(LOG_NOTICE,"Subscribe failed, rc %d\n", response->code);
    tmp->subscribed=0;
}

void onConnect(void* context, MQTTAsync_successData* response)
{
    mqtt_callback_ll *tmp;

   syslog(LOG_NOTICE,"Connected to Host\r\n");

    DL_FOREACH(mqtt_funcs,tmp) {
        if (tmp->subscribed==0)
        {
           mqtt_subscribe(tmp->node,tmp);
           syslog(LOG_NOTICE,"%s -> Subscribing to %s\r\n",__FUNCTION__,tmp->node);
        }
    }
}

void onFailure(void *context, MQTTAsync_failureData *response)
{

   syslog(LOG_NOTICE,"FAILED to connect to MQTT - Check IP, username and password\r\n");

    exit(0);
}


int mqtt_connect(char* url, char* clientid, char *username, char *password)
{
    int rc;
    //========= MQTT ============================//
    //MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    MQTTAsync_connectOptions conn_opts = MQTTAsync_connectOptions_initializer;

    
    rc = MQTTAsync_create(&client,url,clientid, MQTTCLIENT_PERSISTENCE_DEFAULT, NULL);
    
    conn_opts.keepAliveInterval = 30;
    conn_opts.cleansession = 1;
    conn_opts.automaticReconnect=1;
    conn_opts.retryInterval = 5;
    conn_opts.username = username;
    conn_opts.password = password;
    conn_opts.onSuccess = onConnect;
    conn_opts.onFailure = onFailure;
    
	MQTTAsync_setCallbacks(client, client, connlost, messageArrived, NULL);
    
    if((rc = MQTTAsync_connect(client, &conn_opts)) != MQTTASYNC_SUCCESS) {
       syslog(LOG_NOTICE,"Failed to connect, return code %d\n", rc);
        return -1;
    }

    return 0;
}

void connlost(void* context, char* cause)
{
    
    mqtt_callback_ll *tmp;

    DL_FOREACH(mqtt_funcs,tmp) {
        tmp->subscribed=0;
    }
   syslog(LOG_NOTICE,"\nConnection lost\n");
   syslog(LOG_NOTICE,"     cause: %s\n", cause);
}

int messageArrived(void *context, char *topicName, int topicLen, MQTTAsync_message *message)
{
    int i;
    char* payloadptr;
    mqtt_callback_ll *elt;


   syslog(LOG_NOTICE,"%s START",__FUNCTION__);
  
   syslog(LOG_NOTICE,"Message arrived\n");
   syslog(LOG_NOTICE,"     topic: %s\n", topicName);
  // syslog(LOG_NOTICE,"   message: ");

  //  payloadptr = message->payload;
  //  for(i = 0; i < message->payloadlen; i++) {
  //      putchar(*payloadptr++);
  //  }
    
    DL_FOREACH(mqtt_funcs,elt) {
            elt->functionPtr(topicName,message->payload,message->payloadlen,elt->dataPtr);
    }
    
    MQTTAsync_freeMessage(&message);
    MQTTAsync_free(topicName);
   
    syslog(LOG_NOTICE,"%s EXIT",__FUNCTION__);
   
    
    return 1;
}


