#ifndef _MQTT_H
#define _MQTT_H

#include "MQTTAsync.h"
#include "MQTTClientPersistence.h"
//#include "MQTTClient.h"

typedef struct  mqtt_callback_ll{
    char node[128];
    void *dataPtr;
    int  subscribed;
    int (*functionPtr)(char *,char *,int, void *);
    struct mqtt_callback_ll *next, *prev;
} mqtt_callback_ll;


mqtt_callback_ll *mqtt_funcs;

void mqtt_initfuncs(void);
int mqtt_writedata(char *tag, char *message);
int mqtt_writeresponse(char *intag, char *message, int transaction);
int mqtt_connect(char* url, char* clientid, char *username, char *password);
void mqtt_register_callback(char *node,void *func, void *ptr);

MQTTAsync client;
#define CLIENTID "AABBCCDDEEFF"
#define QOS 1



#endif