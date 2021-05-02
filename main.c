/*

Copyright 2021 Richard Parsons

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated 
documentation files (the "Software"), to deal in the Software without restriction, including without limitation 
the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, 
and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE 
WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR 
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR 
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

/* Dependencies
   json-c
   https://github.com/json-c/json-c

   mosquito mqtt client
   https://github.com/eclipse/paho.mqtt.c/archive/v1.3.8.tar.gz
       

  Usage
  rako_adapter -r <RAKO ip address> -m <MQTT IP> -u <MQTT Username> -p <MQTT Password
*/
 

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <getopt.h>
#include <syslog.h>

#include <json-c/json.h>
#include "socketclient.h"
#include "mqtt.h"

#define MAX_ROOMS 32
#define MAX_CHANNELS 16


// --------------- Forward prototypes -----------------------//
void setup_socket(struct socket_client_t *rako_sock, void *pvt);
int rako_idle_callback(void *pvt,struct socket_client_t* sp);
int rako_connect_callback(void *pvt, struct socket_client_t* sp, int fd);
int rako_parse_callback(void *pvt,struct socket_client_t* sp, int fd, char* buffer, int len);
int mqtt_homeassistant_callback(char *node,char *msg, int len, void *p);
unsigned int tokenize(char **result, unsigned int reslen, char *str, char delim);
void send_level(struct socket_client_t* sp, int roomid, int channel, int level);
void send_scene(struct socket_client_t* sp, int roomid, int scene);
int parse_status(void *pvt, struct socket_client_t *sp);
void send_room_request(struct socket_client_t* sp);
void send_channel_request(struct socket_client_t* sp);
void send_level_request(struct socket_client_t* sp);
//----------------------------------------------------------//

struct channels_t {
    char enabled;
    char channel_name[32];
    char channel_type[32];
};

struct rooms_t {
    int  enabled;
    int  current_scene;
    char room_name[64];
    char device_type[32];
    struct channels_t channels[MAX_CHANNELS];
};


struct rako_data_t {
    char state;
    int counter;
    int last_send;
    int keepalive_counter;
    char buffer[32768*4];
    int buffer_ptr;
    json_object *rx_json;

    char rako_address[64];
    char product_type[32];
    char hub_id[48];
    char hub_mac[20];
    char hub_version[16];

    struct rooms_t rooms[MAX_ROOMS];
    void *socket_pvt;

};


void dump_settings(struct rako_data_t *rako_data)
{

   syslog(LOG_NOTICE,"Product_Type:\t\t%s\r\n",rako_data->product_type);
   syslog(LOG_NOTICE,"Product_HubId:\t\t%s\r\n",rako_data->hub_id);
   syslog(LOG_NOTICE,"Product_MAC:\t\t%s\r\n",rako_data->hub_mac);
   syslog(LOG_NOTICE,"Product_Version:\t%s\r\n",rako_data->hub_version);

    int a;
    for (a=0; a<MAX_ROOMS; a++) {
        if (rako_data->rooms[a].enabled == 1) {
           syslog(LOG_NOTICE,"Room %d [%s] %s\r\n",a,rako_data->rooms[a].device_type,rako_data->rooms[a].room_name);
            int b;
            for (b=0; b<MAX_CHANNELS; b++) {
                if (rako_data->rooms[a].channels[b].enabled == 1) {
                   syslog(LOG_NOTICE,"\tChannel %d\t%s\r\n",b,rako_data->rooms[a].channels[b].channel_name);
                }
            }
           syslog(LOG_NOTICE,"\r\n");
        }
    }
    return;
}


int isThisAHub(struct rako_data_t *rako_data)
{
    
    
    if (strcmp(rako_data->product_type,"Hub") == 0)
        return 0;
        
   return -1; 
}

void print_usage(void)
{
   syslog(LOG_NOTICE,"Usage\r\n");
   syslog(LOG_NOTICE,"rako_adapter -r <RAKO ip address> -m <MQTT IP> -u <MQTT Username> -p <MQTT Password\r\n");

    return;
}




int main(int argc, char **argv)
{
    struct socket_client_t rako_client;
    struct rako_data_t rako_data;

    char mqtt_user[64] = {0};
    char mqtt_password[64] = {0};
    char mqtt_address[64] = {0};
    char rako_address[64];

    int option;

    while ((option = getopt(argc, argv,"r:m:u:p:")) != -1) {
        switch (option) {
        case 'u' :
            strncpy(mqtt_user,optarg,63);
            break;
        case 'p' :
            strncpy(mqtt_password,optarg,63);
            break;
        case 'm' :
            strncpy(mqtt_address,optarg,63);
            break;
        case 'r' :
            strncpy(rako_address,optarg,63);
            break;
        default:
            print_usage();
            exit(EXIT_FAILURE);
        }
    }

    if (strlen(mqtt_user) == 0) {
        print_usage();
        exit(0);
    }

    if (strlen(mqtt_password) == 0) {
        print_usage();
        exit(0);
    }

    if (strlen(mqtt_address) == 0) {
        print_usage();
        exit(0);
    }

    if (strlen(rako_address) == 0) {
        print_usage();
        exit(0);
    }

    openlog ("RAKO_MQTT", LOG_CONS | LOG_PID | LOG_NDELAY, LOG_LOCAL1);

    rako_data.last_send = -1;
    rako_data.keepalive_counter=0;
    strncpy(rako_data.rako_address,rako_address,63);

   syslog(LOG_NOTICE,"Connecting to MQTT %s [Username=%s]\r\n",mqtt_address,mqtt_user);



    mqtt_initfuncs();
    int rc = mqtt_connect(mqtt_address,CLIENTID,mqtt_user,mqtt_password);

    if (rc < 0) {
       syslog(LOG_NOTICE,"Could not connect to MQTT\r\n");
        exit(0);
    }


    sleep(1);
    mqtt_register_callback("light/+/set",mqtt_homeassistant_callback,&rako_data);


    rako_data.buffer_ptr=0;
    rako_data.state=0;
    setup_socket(&rako_client, (void *)&rako_data);

    sleep(5);
    dump_settings(&rako_data);
  

    while (1) {
        sleep(1);
    }
    return 0;
}

int mqtt_homeassistant_callback(char *node,char *msg, int len, void *p)
{

    struct rako_data_t *param = p;

    char *tokens[10];
    char *rako_tokens[5];
    json_object* rx_json;
    json_object* returnObj;
    char *state;

    int rc = tokenize(tokens,10,node,'/');

    if (rc == 4) {
        char newstring[128];
        int level;


        rx_json = json_tokener_parse(msg);
        if (json_object_get_type(rx_json) != json_type_object)
            return -1;

        if(json_object_object_get_ex(rx_json, "state", &returnObj)) {
            state = json_object_get_string(returnObj);
        }

        strcpy(newstring,tokens[2]);
        int rc2 = tokenize(rako_tokens,5,newstring,'_');
        
        if (rc2 < 3)
             return -1;

        if (strcmp(rako_tokens[0],"rako")!=0) {
            return -1;
        }


        int scene=-1;
        int room = atol(rako_tokens[1]);
        int channel = atol(rako_tokens[2]);
        if (channel == 0) {
            scene = atol(rako_tokens[3]);

            if (strcmp(state,"OFF")==0) {
                scene=0;
            }

            param->rooms[room].current_scene=scene;
            send_scene(param->socket_pvt,room,scene);
            send_scene(param->socket_pvt,room,scene);
  
        } else {

            if (strcmp(state,"OFF")==0) {
                level=0;
            } else {
                level=0;
                if(json_object_object_get_ex(rx_json, "brightness", &returnObj)) {
                    level = json_object_get_int(returnObj);
                }
                if (level==0)
                    level=255;
            }

            send_level((struct socket_client_t*) param->socket_pvt,room,channel,level);
            send_level((struct socket_client_t*) param->socket_pvt,room,channel,level);
        }

       syslog(LOG_NOTICE,"Room %d - Channel %d [scene=%d]\r\n",room,channel,scene);
    }
}

void setup_socket(struct socket_client_t *rako_sock, void *pvt)
{
    memset(rako_sock,0,sizeof(struct socket_client_t));
    rako_sock->pvt = pvt;

    struct rako_data_t *param = pvt;


    rako_sock->port = 9762;
    strcpy(rako_sock->host,param->rako_address);

    rako_sock->func_idle=(void *)rako_idle_callback;
    rako_sock->func_connected=(void *)rako_connect_callback;
    rako_sock->func_parse=(void *)rako_parse_callback;

    socket_client_start(rako_sock);

    return;
}



int rako_idle_callback(void *pvt,struct socket_client_t* sp)
{

    struct rako_data_t *param = pvt;


    if (param->last_send > 0)
    {
       param->last_send--; 
    } else if (param->last_send==0)
    {
        param->last_send=-1;
        return -1;
    }


    if (param->state == 1) {
        socket_client_write(sp,"\r\n{\"name\":\"status\",\"payload\":{}}\r\n",33);
        param->state++;
    } else if (param->state==2) {
        send_room_request(sp);
        param->state++;
    } else if (param->state==3) {
        send_channel_request(sp);
        param->state++;
    } else if (param->state==4) {
        send_level_request(sp);
        param->state++;
    }


    param->counter++;
    param->keepalive_counter++;
    
    if (param->keepalive_counter == 1000) {
        socket_client_write(sp,"\r\n{\"name\":\"status\",\"payload\":{}}\r\n",33);
        param->keepalive_counter=0;
        param->last_send=500;
    }

    if (param->counter == 30000) {
        param->state=4;
        param->counter=0;
    }


    usleep(10000);
    return 0;
}


int rako_connect_callback(void *pvt, struct socket_client_t* sp, int fd)
{
    struct rako_data_t *param = pvt;

    param->socket_pvt=sp;
    char conn[] = {"SUB,JSON,{\"version\": 2, \"client_name\":\"HA_CLIENT\", \"subscriptions\":[\"TRACKER\",\"FEEDBACK\"] }\r\n\0" };

    socket_client_write(sp,conn,strlen(conn)+2);
    param->state = 1;
    return 0;
}



int rako_parse_callback(void *pvt,struct socket_client_t* sp,int fd, char* buffer, int len)
{
    struct rako_data_t *param = pvt;
    json_object* returnObj;
    char *name;


    if ((buffer[0] != 0x0d) && (buffer[0] != 0x0a)) {
        memcpy(param->buffer+param->buffer_ptr,buffer,len);
        param->buffer_ptr = param->buffer_ptr+len;
    }
    if ((buffer[0] == 0x0d) || (buffer[0] == 0x0a)) {
        if (param->buffer_ptr < 5) {
            param->buffer_ptr=0;
            return -1;
        }

        param->rx_json = json_tokener_parse(param->buffer);
        if (json_object_get_type(param->rx_json) != json_type_object) {
            param->buffer_ptr=0;
            return -1;
        }


        if(json_object_object_get_ex(param->rx_json, "name", &returnObj)) {
            name = json_object_get_string(returnObj);
            if (name == NULL) {

                json_object_put(param->rx_json);
                param->buffer_ptr=0;
                return -1;
            }
        }


        if (strcmp(name,"status") == 0)
            parse_status(pvt,sp);
        if (strcmp(name,"query_ROOM") == 0)
            parse_query_room(pvt,sp);
        if (strcmp(name,"query_CHANNEL") == 0)
            parse_query_channel(pvt,sp);
        if (strcmp(name,"query_LEVEL") == 0)
            parse_query_levels(pvt,sp);
        if (strcmp(name,"tracker") == 0)
            parse_tracker(pvt,sp);
        if (strcmp(name,"feedback") == 0)
            parse_feedback(pvt,sp);

        memset(param->buffer,0,32768*4);
        param->buffer_ptr=0;
        json_object_put(param->rx_json);
    }


    return 0;
}

void send_level(struct socket_client_t* sp, int roomid, int channel, int level)
{
    char roomdata[256];

    sprintf(roomdata,"\r\n{\"name\": \"send\",\"payload\": {\"room\": %d,\"channel\": %d,\"action\": {\"command\": \"levelrate\",\"level\": %d}}}\r\n",roomid,channel,level);
    socket_client_write(sp,roomdata,strlen(roomdata)+2);
    return;
}

void send_scene(struct socket_client_t* sp, int roomid, int scene)
{
    char roomdata[256];

    sprintf(roomdata,"\r\n{\"name\": \"send\",\"payload\": {\"room\": %d,\"channel\": 0,\"action\": {\"command\": \"scene\",\"scene\": %d}}}\r\n",roomid,scene);
    socket_client_write(sp,roomdata,strlen(roomdata)+2);
    return;
}



void send_room_request(struct socket_client_t* sp)
{
    char roomid[] = { "\r\n{ \"name\": \"query\",\"payload\": { \"queryType\": \"ROOM\",\"roomId\": 0}}\r\n\0" };
    socket_client_write(sp,roomid,strlen(roomid)+2);
    return;
}

void send_channel_request(struct socket_client_t* sp)
{
    char channel[] = { "\r\n{ \"name\": \"query\",\"payload\": { \"queryType\": \"CHANNEL\",\"roomId\": 0}}\r\n"};
    socket_client_write(sp,channel,strlen(channel)+2);
    return;
}

void send_level_request(struct socket_client_t* sp)
{
    char channel[] = { "\r\n{ \"name\": \"query\",\"payload\": { \"queryType\": \"LEVEL\",\"roomId\": 0}}\r\n"};
    socket_client_write(sp,channel,strlen(channel)+2);
    return;
}


int parse_query_levels(void *pvt, struct socket_client_t *sp)
{
    int rc = -1;
    int i,p;

    struct rako_data_t *param = pvt;
    json_object *returnObj;
    json_object *itemObj;
    json_object *valueObj;
    json_object *levelsArrayObj;
    json_object *levelsObj;

    rc = json_object_object_get_ex(param->rx_json, "payload", &returnObj);
    if (rc == 1) {
        int arraylen = json_object_array_length(returnObj);
        for (i = 0; i < arraylen; i++) {
            itemObj  = json_object_array_get_idx(returnObj, i);
            json_object_object_get_ex(itemObj, "roomId", &valueObj);
            int index = json_object_get_int(valueObj);

            json_object_object_get_ex(itemObj, "currentScene", &valueObj);
            int scene = json_object_get_int(valueObj);

            update_scene(index,0,scene);


            if (index > MAX_ROOMS)
                continue;
            if (param->rooms[index].enabled!=1)
                continue;
            json_object_object_get_ex(itemObj, "channel", &levelsArrayObj);
            int levelArrayCount = json_object_array_length(levelsArrayObj);
           syslog(LOG_NOTICE,"Room %d  %s\r\n",index,param->rooms[index].room_name);

            for (p=0; p<levelArrayCount; p++) {
                levelsObj  = json_object_array_get_idx(levelsArrayObj, p);
                json_object_object_get_ex(levelsObj, "channelId", &valueObj);
                int channelid = json_object_get_int(valueObj);
                if (param->rooms[index].channels[channelid].enabled != 1)
                    continue;
                json_object_object_get_ex(levelsObj, "currentLevel", &valueObj);
                int level = json_object_get_int(valueObj);
               syslog(LOG_NOTICE,"\tChannel %d Level=%d\r\n",channelid,level);
                publish_state(index,channelid,level);
            }
        }
    }

    return rc;
}

int parse_query_channel(void *pvt, struct socket_client_t *sp)
{
    int rc = -1;
    int i,p;
    char *value;

    struct rako_data_t *param = pvt;
    json_object *returnObj;
    json_object *itemObj;
    json_object *channel_itemObj;
    json_object *chObj;
    json_object *channelObj;
    json_object *valueObj;

    rc = json_object_object_get_ex(param->rx_json, "payload", &returnObj);
    if (rc == 1) {
        int arraylen = json_object_array_length(returnObj);
        for (i = 0; i < arraylen; i++) {
            itemObj  = json_object_array_get_idx(returnObj, i);
            json_object_object_get_ex(itemObj, "roomId", &valueObj);
            int index = json_object_get_int(valueObj);

            publish_scene(index,0,param->rooms[index].room_name,param->rooms[index].room_name);



            json_object_object_get_ex(itemObj, "channel", &channelObj);
            int channel_count = json_object_array_length(channelObj);
            if (channel_count > 0) {
                for (p=0; p<channel_count; p++) {

                    channel_itemObj  = json_object_array_get_idx(channelObj, p);
                    json_object_object_get_ex(channel_itemObj, "channelId", &chObj);
                    int channel_num = json_object_get_int(chObj);

                    param->rooms[index].channels[channel_num].enabled=1;

                    json_object_object_get_ex(channel_itemObj, "title", &chObj);
                    char *v = json_object_get_string(chObj);
                    strncpy(param->rooms[index].channels[channel_num].channel_name,v,31);

                    json_object_object_get_ex(channel_itemObj, "type", &chObj);
                    v = json_object_get_string(chObj);
                    strncpy(param->rooms[index].channels[channel_num].channel_type,v,31);

                    publish_discovery(index,channel_num,param->rooms[index].room_name,param->rooms[index].channels[channel_num].channel_name);

                }
            }
        }
        rc = 0;
    }
}

int parse_query_room(void *pvt, struct socket_client_t *sp)
{
    int rc = -1;
    int i;
    char *value;

    struct rako_data_t *param = pvt;
    json_object *returnObj;
    json_object *itemObj;
    json_object *valueObj;

    memset(&param->rooms,0,sizeof(struct rooms_t)* MAX_ROOMS);
    rc = json_object_object_get_ex(param->rx_json, "payload", &returnObj);
    if (rc == 1) {
        int arraylen = json_object_array_length(returnObj);
        for (i = 0; i < arraylen; i++) {
            itemObj  = json_object_array_get_idx(returnObj, i);
            json_object_object_get_ex(itemObj, "roomId", &valueObj);
            int index = json_object_get_int(valueObj);

            param->rooms[index].enabled = 1;
            json_object_object_get_ex(itemObj, "title", &valueObj);
            value = json_object_get_string(valueObj);
            strncpy(param->rooms[index].room_name,value,63);

            json_object_object_get_ex(itemObj, "type", &valueObj);
            value = json_object_get_string(valueObj);
            strncpy(param->rooms[index].device_type,value,31);
        }
        rc = 0;
    }

    return rc;
}

int parse_status(void *pvt, struct socket_client_t *sp)
{

    int rc = -1;
    struct rako_data_t *param = pvt;
    json_object *returnObj;
    json_object *valueObj;

   syslog(LOG_NOTICE,"Got Status....its ALIVE!\r\n");
    rc = json_object_object_get_ex(param->rx_json, "payload", &returnObj);
    if (rc == 1) {
        char *value;
        json_object_object_get_ex(returnObj, "productType", &valueObj);
        value = json_object_get_string(valueObj);
        strncpy(param->product_type,value,31);

        json_object_object_get_ex(returnObj, "hubId", &valueObj);
        value = json_object_get_string(valueObj);
        strncpy(param->hub_id,value,47);


        json_object_object_get_ex(returnObj, "mac;", &valueObj);
        value = json_object_get_string(valueObj);
        strncpy(param->hub_mac,value,19);

        json_object_object_get_ex(returnObj, "hubVersion", &valueObj);
        value = json_object_get_string(valueObj);
        strncpy(param->hub_version,value,15);

        param->last_send=-1;
        rc = 0;
    }

    return rc;
}


//
//{"name":"tracker","type":"level","payload":{"roomId":13,"channelId":12,"currentLevel":0,"targetLevel":255,"timeToTake":1591,"temporary":false}}
//Parse the entire packet [tracker]
int parse_tracker(void *pvt, struct socket_client_t *sp)
{
    int rc = -1;
    int i;
    char *value;

    struct rako_data_t *param = pvt;
    json_object *returnObj;
    //json_object *itemObj;
    json_object *valueObj;


    json_object_object_get_ex(param->rx_json, "payload", &returnObj);
    if (json_object_get_type(returnObj) == json_type_object) {
        json_object_object_get_ex(returnObj, "roomId", &valueObj);
        int index = json_object_get_int(valueObj);

        json_object_object_get_ex(returnObj, "channelId", &valueObj);
        int channel = json_object_get_int(valueObj);

        json_object_object_get_ex(returnObj, "targetLevel", &valueObj);

        int level = json_object_get_int(valueObj);
       syslog(LOG_NOTICE,"Room %d - Channel %d - Target %d\r\n",index,channel,level);
        publish_state(index,channel,level);
        rc=0;
    }

    return rc;
}


//{"name":"feedback","payload":{"action":{"actUniqueId":-1,"defaultFadeRate":true,"decay":0,"expFadeRate":false,"scene":1,"command":49},"room":19,"channel":0,"description":"[Rm:19 outside lights] Scene 1"}}
int parse_feedback(void *pvt, struct socket_client_t *sp)
{
    int rc = -1;
    int i;
    char *value;

    struct rako_data_t *param = pvt;
    json_object *returnObj;
    //json_object *itemObj;
    json_object *valueObj;
    json_object *actionObj;


    json_object_object_get_ex(param->rx_json, "payload", &returnObj);
    if (json_object_get_type(returnObj) == json_type_object) {
        json_object_object_get_ex(returnObj, "room", &valueObj);
        int index = json_object_get_int(valueObj);

        json_object_object_get_ex(returnObj, "channel", &valueObj);
        int channel = json_object_get_int(valueObj);

        json_object_object_get_ex(returnObj, "action", &actionObj);
        if (json_object_get_type(actionObj) == json_type_object) {
            json_object_object_get_ex(actionObj, "scene", &valueObj);
            int scene = json_object_get_int(valueObj);

            json_object_object_get_ex(actionObj, "command", &valueObj);
            int command = json_object_get_int(valueObj);

           syslog(LOG_NOTICE,"Setting scene %d on Room %d\r\n",scene,index);

            update_scene(index,0,scene);
        }

        rc=0;
    }

    return rc;
}




#if 0

room_number, Fiendly name, unique
"{\"~\": \"homeassistant/light/%d\",\"name\": \"%s\",\"unique_id\":\"%s\",\"cmd_t\":\"~/set\",\"stat_t\":\"~/state\",\"schema\":\"json\",\"brightness\":true}\0"

homeassistant/light/kitchen/config
#endif

void publish_discovery(int roomid,int channel_id,char *name, char *unique_name)
{

    char discover[512];
    char tag[512];

    sprintf(tag,"homeassistant/light/rako_%d_%d/config",roomid,channel_id);

    sprintf(discover,"{\"~\": \"homeassistant/light/rako_%d_%d\",\"name\": \"%s_ch%d\",\"unique_id\":\"rako_%d_%d\",\"cmd_t\":\"~/set\",\"stat_t\":\"~/state\",\"schema\":\"json\",\"brightness\":true}\0",
            roomid,channel_id,name,channel_id,roomid,channel_id);

    mqtt_writedata(tag,discover);


}

void publish_scene(int roomid,int channel_id,char *name, char *unique_name)
{

    char discover[512];
    char tag[512];
    int  scene;

    for (scene=0; scene<6; scene++) {
        sprintf(tag,"homeassistant/light/rako_%d_%d_%d/config",roomid,channel_id,scene);
        sprintf(discover,"{\"~\": \"homeassistant/light/rako_%d_%d_%d\",\"name\": \"%s_scene_%d\",\"unique_id\":\"rako_%d_%d_%d\",\"cmd_t\":\"~/set\",\"stat_t\":\"~/state\",\"schema\":\"json\",\"brightness\":false}\0",
                roomid,channel_id,scene,name,scene,roomid,channel_id,scene);
        mqtt_writedata(tag,discover);
    }

}

void update_scene(int roomid,int channel_id,int scene)
{

    char discover[512];
    char tag[512];
    int a;


    for (a=0; a<6; a++) {
        sprintf(tag,"homeassistant/light/rako_%d_%d_%d/state",roomid,channel_id,a);

        char onoff[5];
        if (scene != a) {
            sprintf(onoff,"OFF");
        } else {
            sprintf(onoff,"ON");
        }

        sprintf(discover," { \"state\" : \"%s\" } ",onoff,onoff);
        //printf("TAG : %s [State->%s]\r\n",tag,discover);
        mqtt_writedata(tag,discover);
        usleep(1000);
    }
}


void publish_state(int roomid,int channel_id,int level)
{

    char discover[512];
    char tag[512];

    sprintf(tag,"homeassistant/light/rako_%d_%d/state",roomid,channel_id);
    char onoff[5];

    if (level ==0) {
        sprintf(onoff,"OFF");
    } else {
        sprintf(onoff,"ON");
    }
    sprintf(discover,"{\"state\": \"%s\", \"brightness\": %d}",onoff,level);
    mqtt_writedata(tag,discover);
}


unsigned int tokenize(char **result, unsigned int reslen, char *str, char delim)
{
    char *p, *n;
    unsigned int i = 0;

    if(!str)
        return 0;
    for(n = str; *n == ' '; n++);
    p = n;
    for(i = 0; *n != 0;) {
        if(i == reslen)
            return i;
        if(*n == delim) {
            *n = 0;
            if(strlen(p))
                result[i++] = p;
            p = ++n;
        } else
            n++;
    }
    if(strlen(p))
        result[i++] = p;
    return i;                   /* number of tokens */
}

/*

 * RX -> {"name":"tracker","type":"level","payload":{"roomId":13,"channelId":12,"currentLevel":0,"targetLevel":255,"timeToTake":1591,"temporary":false}}
Parse the entire packet [tracker]
RX -> {"name":"tracker","type":"level","payload":{"roomId":13,"channelId":13,"currentLevel":0,"targetLevel":255,"timeToTake":1591,"temporary":false}}
Parse the entire packet [tracker]
RX -> {"name":"tracker","type":"level","payload":{"roomId":13,"channelId":14,"currentLevel":0,"targetLevel":255,"timeToTake":1591,"temporary":false}}
Parse the entire packet [tracker]
RX -> {"name":"tracker","type":"level","payload":{"roomId":13,"channelId":15,"currentLevel":0,"targetLevel":255,"timeToTake":1591,"temporary":false}}
Parse the entire packet [tracker]

 *
 *
 * {"name":"status","payload":{"productType":"Hub","protocolVersion":2,"hubId":"03559cad-254f-3c85-97b6-4d6398c03511","mac;":"70:B3:D5:08:45:6B","hubVersion":"3.1.6"}}

{"name":"query_ROOM","payload":[{"roomId":0,"title":"House Master","type":"LIGHT"},
                               {"roomId":1,"title":"Hallway","type":"LIGHT"},
                               {"roomId":3,"title":"plant room","type":"LIGHT"},
                               {"roomId":5,"title":"dining","type":"LIGHT"},
                               {"roomId":6,"title":"kitchen","type":"LIGHT"},
                               {"roomId":7,"title":"pantry","type":"LIGHT"},
                               {"roomId":17,"title":"boiler room","type":"LIGHT"},
                               {"roomId":10,"title":"upstairs hallway","type":"LIGHT"},
                               {"roomId":11,"title":"Bernies Bathroom","type":"LIGHT"},
                               {"roomId":12,"title":"Bernies dressing","type":"LIGHT"},
                               {"roomId":13,"title":"roger Dressing","type":"LIGHT"},
                               {"roomId":14,"title":"master bed","type":"LIGHT"},
                               {"roomId":15,"title":"rogers bathroom","type":"LIGHT"},
                               {"roomId":16,"title":"gym","type":"LIGHT"},
                               {"roomId":2,"title":"downstairs bathroom","type":"LIGHT"},
                               {"roomId":21,"title":"Lounge","type":"LIGHT"},
                               {"roomId":19,"title":"outside lights","type":"LIGHT"},
                               {"roomId":25,"title":"Upstairs","type":"SWITCH"},
                               {"roomId":26,"title":"Downstairs","type":"SWITCH"}]}

{"name":"query_CHANNEL","payload":[{"roomId":0,"title":"House Master","type":"LIGHT","channel":[]},
 * {"roomId":1,"title":"Hallway","type":"LIGHT","channel":[
 *      {"channelId":1,"title":"Channel 1","type":"SLIDER","sceneLevels":[0,255,191,127,63,0,0,0,0,0,0,0,0,0,0,0,0]},
 *      {"channelId":2,"title":"test","type":"SLIDER","sceneLevels":[0,255,191,127,63,0,0,0,0,0,0,0,0,0,0,0,0]}]},
 * {"roomId":3,"title":"plant room","type":"LIGHT","channel":[
 *      {"channelId":1,"title":"Channel 1","type":"SLIDER","sceneLevels":[0,255,191,127,63,0,0,0,0,0,0,0,0,0,0,0,0]}]},
 * {"roomId":5,"title":"dining","type":"LIGHT","channel":[
 *      {"channelId":2,"title":"Dining room pendants","type":"SLIDER","sceneLevels":[0,255,191,255,64,0,0,0,0,0,0,0,0,0,0,0,0]},
 *      {"channelId":3,"title":"dining downlights","type":"SLIDER","sceneLevels":[0,255,191,0,64,0,0,0,0,0,0,0,0,0,0,0,0]}]},
 * {"roomId":6,"title":"kitchen","type":"LIGHT","channel":[
 *      {"channelId":1,"title":"Kitchen Pendants","type":"SLIDER","sceneLevels":[0,129,121,128,64,255,0,0,0,0,0,0,0,0,0,0,0]},
 *      {"channelId":2,"title":"Spot lights","type":"SLIDER","sceneLevels":[0,181,191,0,0,255,0,0,0,0,0,0,0,0,0,0,0]},
 *      {"channelId":3,"title":"Ceiling spots","type":"SLIDER","sceneLevels":[0,181,191,0,0,255,0,0,0,0,0,0,0,0,0,0,0]},
 *      {"channelId":4,"title":"Air con area downlights","type":"SLIDER","sceneLevels":[0,181,191,127,64,255,0,0,0,0,0,0,0,0,0,0,0]},
 *      {"channelId":5,"title":"Cill uplights","type":"SLIDER","sceneLevels":[0,181,191,127,64,255,0,0,0,0,0,0,0,0,0,0,0]},
 *      {"channelId":6,"title":"Floor uplights","type":"SLIDER","sceneLevels":[0,181,191,127,64,255,0,0,0,0,0,0,0,0,0,0,0]},
 *      {"channelId":7,"title":"Spare","type":"SLIDER","sceneLevels":[0,181,191,127,124,255,158,0,0,0,0,0,0,0,0,0,0]},
 *      {"channelId":8,"title":"Spare","type":"SLIDER","sceneLevels":[0,181,191,127,64,255,0,0,0,0,0,0,0,0,0,0,0]}]},
 * {"roomId":7,"title":"pantry","type":"LIGHT","channel":[]},
 * {"roomId":17,"title":"boiler room","type":"LIGHT","channel":[
 *      {"channelId":1,"title":"Channel 1","type":"SLIDER","sceneLevels":[0,255,191,127,63,0,0,0,0,0,0,0,0,0,0,0,0]}]},
 * {"roomId":10,"title":"upstairs hallway","type":"LIGHT","channel":[
 *      {"channelId":1,"title":"Spots","type":"SLIDER","sceneLevels":[0,255,191,127,63,0,0,0,0,0,0,0,0,0,0,0,0]}]},
 * {"roomId":11,"title":"Bernies Bathroom","type":"LIGHT","channel":[
 *      {"channelId":1,"title":"Downlights","type":"SLIDER","sceneLevels":[0,0,64,144,194,0,0,0,0,0,0,0,0,0,0,0,0]},
 *      {"channelId":2,"title":"Chandelier","type":"SLIDER","sceneLevels":[0,0,191,134,134,0,0,0,0,0,0,0,0,0,0,0,0]},
 *      {"channelId":3,"title":"Alcove lights","type":"SLIDER","sceneLevels":[0,0,191,255,255,0,0,0,0,0,0,0,0,0,0,0,0]},
 *      {"channelId":4,"title":"Bath up lights","type":"SLIDER","sceneLevels":[0,255,191,255,255,0,0,0,0,0,0,0,0,0,0,0,0]},
 *      {"channelId":5,"title":"Fan + mirror demister","type":"SLIDER","sceneLevels":[0,0,191,255,191,0,0,0,0,0,0,0,0,0,0,0,0]}]},
 * {"roomId":12,"title":"Bernies dressing","type":"LIGHT","channel":[
 *      {"channelId":2,"title":"Downlights","type":"SLIDER","sceneLevels":[0,255,191,127,63,0,0,0,0,0,0,0,0,0,0,0,0]}]},
 * {"roomId":13,"title":"roger Dressing","type":"LIGHT","channel":[
 *      {"channelId":1,"title":"Downlights","type":"SLIDER","sceneLevels":[0,0,191,127,63,0,0,0,0,0,0,0,0,0,0,0,0]},
 *      {"channelId":2,"title":"Wall lights left","type":"SLIDER","sceneLevels":[0,0,191,127,63,0,0,0,0,0,0,0,0,0,0,0,0]},
 *      {"channelId":3,"title":"Wall lights right","type":"SLIDER","sceneLevels":[0,0,191,127,63,0,0,0,0,0,0,0,0,0,0,0,0]}]},
 * {"roomId":14,"title":"master bed","type":"LIGHT","channel":[
 *      {"channelId":1,"title":"downlights sitting area","type":"SLIDER","sceneLevels":[0,255,191,127,0,0,0,0,0,0,0,0,0,0,0,0,0]},
 *      {"channelId":2,"title":"Down lights bed area","type":"SLIDER","sceneLevels":[0,255,191,127,0,0,0,0,0,0,0,0,0,0,0,0,0]},
 *      {"channelId":3,"title":"Chandeliers","type":"SLIDER","sceneLevels":[0,255,191,127,220,0,0,0,0,0,0,0,0,0,0,0,0]},
 *      {"channelId":4,"title":"Wall light 1","type":"SLIDER","sceneLevels":[0,255,191,127,0,0,0,0,0,0,0,0,0,0,0,0,0]},
 *      {"channelId":5,"title":"Wall light 2","type":"SLIDER","sceneLevels":[0,255,191,127,0,0,0,0,0,0,0,0,0,0,0,0,0]},
 *      {"channelId":6,"title":"Wall light 3","type":"SLIDER","sceneLevels":[0,255,191,127,0,0,0,0,0,0,0,0,0,0,0,0,0]},
 *      {"channelId":7,"title":"Wall light 4","type":"SLIDER","sceneLevels":[0,255,191,127,0,0,0,0,0,0,0,0,0,0,0,0,0]}]},
 * {"roomId":15,"title":"rogers bathroom","type":"LIGHT","channel":[
 *      {"channelId":2,"title":"Downlights","type":"SLIDER","sceneLevels":[0,255,191,127,64,0,0,0,0,0,0,0,0,0,0,0,0]},
 *      {"channelId":5,"title":"LED strip sink","type":"SLIDER","sceneLevels":[0,255,191,127,64,0,0,0,0,0,0,0,0,0,0,0,0]},
 *      {"channelId":6,"title":"Alcove lights","type":"SLIDER","sceneLevels":[0,255,191,127,64,0,0,0,0,0,0,0,0,0,0,0,0]},
 *      {"channelId":7,"title":"Window cill uplights","type":"SLIDER","sceneLevels":[0,255,191,127,64,0,0,0,0,0,0,0,0,0,0,0,0]},
 *      {"channelId":8,"title":"Fan","type":"SLIDER","sceneLevels":[0,255,191,0,0,0,0,0,0,0,0,0,0,0,0,0,0]}]},
 * {"roomId":16,"title":"gym","type":"LIGHT","channel":[
 *      {"channelId":1,"title":"Downlights","type":"SLIDER","sceneLevels":[0,255,191,127,63,0,0,0,0,0,0,0,0,0,0,0,0]},
 *      {"channelId":2,"title":"Downlights","type":"SLIDER","sceneLevels":[0,255,191,127,63,0,0,0,0,0,0,0,0,0,0,0,0]}]},
 * {"roomId":2,"title":"downstairs bathroom","type":"LIGHT","channel":[
 *      {"channelId":1,"title":"Channel 1","type":"SLIDER","sceneLevels":[0,255,117,83,0,0,0,0,0,0,0,0,0,0,0,0,0]}]},
 * {"roomId":21,"title":"Lounge","type":"LIGHT","channel":[
 *      {"channelId":1,"title":"Seating Area","type":"SLIDER","sceneLevels":[0,135,0,0,64,0,0,0,0,0,0,0,0,0,0,0,0]},
 *      {"channelId":2,"title":"Perimeter Downlights","type":"SLIDER","sceneLevels":[0,135,140,40,64,0,0,0,0,0,0,0,0,0,0,0,0]},
 *      {"channelId":3,"title":"Floor Uplighters","type":"SLIDER","sceneLevels":[0,135,191,127,64,0,0,0,0,0,0,0,0,0,0,0,0]}]},
 * {"roomId":19,"title":"outside lights","type":"LIGHT","channel":[
 *      {"channelId":1,"title":"1","type":"SLIDER","sceneLevels":[0,255,191,127,63,0,0,0,0,0,0,0,0,0,0,0,0]}]},
 * {"roomId":25,"title":"Upstairs","type":"SWITCH","channel":[]},
 * {"roomId":26,"title":"Downstairs","type":"SWITCH","channel":[]}]}
Parse the entire packet [query_CHANNEL]



{"name":"query_LEVEL","payload":[{"channel":[{"channelId":0,"currentLevel":0,"targetLevel":null},
  {"channelId":1,"currentLevel":0,"targetLevel":null},
  {"channelId":2,"currentLevel":0,"targetLevel":null},{"channelId":3,"currentLevel":0,"targetLevel":null},{"channelId":4,"currentLevel":0,"targetLevel":null},{"channelId":5,"currentLevel":0,"targetLevel":null},{"channelId":6,"currentLevel":0,"targetLevel":null},{"channelId":7,"currentLevel":0,"targetLevel":null},{"channelId":8,"currentLevel":0,"targetLevel":null},{"channelId":9,"currentLevel":0,"targetLevel":null},{"channelId":10,"currentLevel":0,"targetLevel":null},{"channelId":11,"currentLevel":0,"targetLevel":null},{"channelId":12,"currentLevel":0,"targetLevel":null},{"channelId":13,"currentLevel":0,"targetLevel":null},{"channelId":14,"currentLevel":0,"targetLevel":null},{"channelId":15,"currentLevel":0,"targetLevel":null}],"roomId":1,"currentScene":0},{"channel":[{"channelId":0,"currentLevel":0,"targetLevel":null},{"channelId":1,"currentLevel":0,"targetLevel":null},{"channelId":2,"currentLevel":0,"targetLevel":null},{"channelId":3,"currentLevel":0,"targetLevel":null},{"channelId":4,"currentLevel":0,"targetLevel":null},{"channelId":5,"currentLevel":0,"targetLevel":null},{"channelId":6,"currentLevel":0,"targetLevel":null},{"channelId":7,"currentLevel":0,"targetLevel":null},{"channelId":8,"currentLevel":0,"targetLevel":null},{"channelId":9,"currentLevel":0,"targetLevel":null},{"channelId":10,"currentLevel":0,"targetLevel":null},{"channelId":11,"currentLevel":0,"targetLevel":null},{"channelId":12,"currentLevel":0,"targetLevel":null},{"channelId":13,"currentLevel":0,"targetLevel":null},{"channelId":14,"currentLevel":0,"targetLevel":null},{"channelId":15,"currentLevel":0,"targetLevel":null}],"roomId":2,"currentScene":0},{"channel":[{"channelId":0,"currentLevel":0,"targetLevel":null},{"channelId":1,"currentLevel":0,"targetLevel":null},{"channelId":2,"currentLevel":0,"targetLevel":null},{"channelId":3,"currentLevel":0,"targetLevel":null},{"channelId":4,"currentLevel":0,"targetLevel":null},{"channelId":5,"currentLevel":0,"targetLevel":null},{"channelId":6,"currentLevel":0,"targetLevel":null},{"channelId":7,"currentLevel":0,"targetLevel":null},{"channelId":8,"currentLevel":0,"targetLevel":null},{"channelId":9,"currentLevel":0,"targetLevel":null},{"channelId":10,"currentLevel":0,"targetLevel":null},{"channelId":11,"currentLevel":0,"targetLevel":null},{"channelId":12,"currentLevel":0,"targetLevel":null},{"channelId":13,"currentLevel":0,"targetLevel":null},{"channelId":14,"currentLevel":0,"targetLevel":null},{"channelId":15,"currentLevel":0,"targetLevel":null}],"roomId":3,"currentScene":0},{"channel":[{"channelId":0,"currentLevel":0,"targetLevel":null},{"channelId":1,"currentLevel":0,"targetLevel":null},{"channelId":2,"currentLevel":0,"targetLevel":null},{"channelId":3,"currentLevel":0,"targetLevel":null},{"channelId":4,"currentLevel":0,"targetLevel":null},{"channelId":5,"currentLevel":0,"targetLevel":null},{"channelId":6,"currentLevel":0,"targetLevel":null},{"channelId":7,"currentLevel":0,"targetLevel":null},{"channelId":8,"currentLevel":0,"targetLevel":null},{"channelId":9,"currentLevel":0,"targetLevel":null},{"channelId":10,"currentLevel":0,"targetLevel":null},{"channelId":11,"currentLevel":0,"targetLevel":null},{"channelId":12,"currentLevel":0,"targetLevel":null},{"channelId":13,"currentLevel":0,"targetLevel":null},{"channelId":14,"currentLevel":0,"targetLevel":null},{"channelId":15,"currentLevel":0,"targetLevel":null}],"roomId":4,"currentScene":0},{"channel":[{"channelId":0,"currentLevel":0,"targetLevel":null},{"channelId":1,"currentLevel":0,"targetLevel":null},{"channelId":2,"currentLevel":0,"targetLevel":null},{"channelId":3,"currentLevel":0,"targetLevel":null},{"channelId":4,"currentLevel":0,"targetLevel":null},{"channelId":5,"currentLevel":0,"targetLevel":null},{"channelId":6,"currentLevel":0,"targetLevel":null},{"channelId":7,"currentLevel":0,"targetLevel":null},{"channelId":8,"currentLevel":0,"targetLevel":null},{"channelId":9,"currentLevel":0,"targetLevel":null},{"channelId":10,"currentLevel":0,"targetLevel":null},{"channelId":11,"currentLevel":0,"targetLevel":null},{"channelId":12,"currentLevel":0,"targetLevel":null},{"channelId":13,"currentLevel":0,"targetLevel":null},{"channelId":14,"currentLevel":0,"targetLevel":null},{"channelId":15,"currentLevel":0,"targetLevel":null}],"roomId":5,"currentScene":0},{"channel":[{"channelId":0,"currentLevel":0,"targetLevel":null},{"channelId":1,"currentLevel":0,"targetLevel":null},{"channelId":2,"currentLevel":0,"targetLevel":null},{"channelId":3,"currentLevel":0,"targetLevel":null},{"channelId":4,"currentLevel":0,"targetLevel":null},{"channelId":5,"currentLevel":0,"targetLevel":null},{"channelId":6,"currentLevel":0,"targetLevel":null},{"channelId":7,"currentLevel":0,"targetLevel":null},{"channelId":8,"currentLevel":0,"targetLevel":null},{"channelId":9,"currentLevel":0,"targetLevel":null},{"channelId":10,"currentLevel":0,"targetLevel":null},{"channelId":11,"currentLevel":0,"targetLevel":null},{"channelId":12,"currentLevel":0,"targetLevel":null},{"channelId":13,"currentLevel":0,"targetLevel":null},{"channelId":14,"currentLevel":0,"targetLevel":null},{"channelId":15,"currentLevel":0,"targetLevel":null}],"roomId":6,"currentScene":0},{"channel":[{"channelId":0,"currentLevel":0,"targetLevel":null},{"channelId":1,"currentLevel":0,"targetLevel":null},{"channelId":2,"currentLevel":0,"targetLevel":null},{"channelId":3,"currentLevel":0,"targetLevel":null},{"channelId":4,"currentLevel":0,"targetLevel":null},{"channelId":5,"currentLevel":0,"targetLevel":null},{"channelId":6,"currentLevel":0,"targetLevel":null},{"channelId":7,"currentLevel":0,"targetLevel":null},{"channelId":8,"currentLevel":0,"targetLevel":null},{"channelId":9,"currentLevel":0,"targetLevel":null},{"channelId":10,"currentLevel":0,"targetLevel":null},{"channelId":11,"currentLevel":0,"targetLevel":null},{"channelId":12,"currentLevel":0,"targetLevel":null},{"channelId":13,"currentLevel":0,"targetLevel":null},{"channelId":14,"currentLevel":0,"targetLevel":null},{"channelId":15,"currentLevel":0,"targetLevel":null}],"roomId":903,"currentScene":0},{"channel":[{"channelId":0,"currentLevel":0,"targetLevel":null},{"channelId":1,"currentLevel":0,"targetLevel":null},{"channelId":2,"currentLevel":0,"targetLevel":null},{"channelId":3,"currentLevel":0,"targetLevel":null},{"channelId":4,"currentLevel":0,"targetLevel":null},{"channelId":5,"currentLevel":0,"targetLevel":null},{"channelId":6,"currentLevel":0,"targetLevel":null},{"channelId":7,"currentLevel":0,"targetLevel":null},{"channelId":8,"currentLevel":0,"targetLevel":null},{"channelId":9,"currentLevel":0,"targetLevel":null},{"channelId":10,"currentLevel":0,"targetLevel":null},{"channelId":11,"currentLevel":0,"targetLevel":null},{"channelId":12,"currentLevel":0,"targetLevel":null},{"channelId":13,"currentLevel":0,"targetLevel":null},{"channelId":14,"currentLevel":0,"targetLevel":null},{"channelId":15,"currentLevel":0,"targetLevel":null}],"roomId":8,"currentScene":0},{"channel":[{"channelId":0,"currentLevel":0,"targetLevel":null},{"channelId":1,"currentLevel":0,"targetLevel":null},{"channelId":2,"currentLevel":0,"targetLevel":null},{"channelId":3,"currentLevel":0,"targetLevel":null},{"channelId":4,"currentLevel":0,"targetLevel":null},{"channelId":5,"currentLevel":0,"targetLevel":null},{"channelId":6,"currentLevel":0,"targetLevel":null},{"channelId":7,"currentLevel":0,"targetLevel":null},{"channelId":8,"currentLevel":0,"targetLevel":null},{"channelId":9,"currentLevel":0,"targetLevel":null},{"channelId":10,"currentLevel":0,"targetLevel":null},{"channelId":11,"currentLevel":0,"targetLevel":null},{"channelId":12,"currentLevel":0,"targetLevel":null},{"channelId":13,"currentLevel":0,"targetLevel":null},{"channelId":14,"currentLevel":0,"targetLevel":null},{"channelId":15,"currentLevel":0,"targetLevel":null}],"roomId":10,"currentScene":0},{"channel":[{"channelId":0,"currentLevel":0,"targetLevel":null},{"channelId":1,"currentLevel":0,"targetLevel":null},{"channelId":2,"currentLevel":0,"targetLevel":null},{"channelId":3,"currentLevel":0,"targetLevel":null},{"channelId":4,"currentLevel":0,"targetLevel":null},{"channelId":5,"currentLevel":0,"targetLevel":null},{"channelId":6,"currentLevel":0,"targetLevel":null},{"channelId":7,"currentLevel":0,"targetLevel":null},{"channelId":8,"currentLevel":0,"targetLevel":null},{"channelId":9,"currentLevel":0,"targetLevel":null},{"channelId":10,"currentLevel":0,"targetLevel":null},{"channelId":11,"currentLevel":0,"targetLevel":null},{"channelId":12,"currentLevel":0,"targetLevel":null},{"channelId":13,"currentLevel":0,"targetLevel":null},{"channelId":14,"currentLevel":0,"targetLevel":null},{"channelId":15,"currentLevel":0,"targetLevel":null}],"roomId":11,"currentScene":0},{"channel":[{"channelId":0,"currentLevel":0,"targetLevel":null},{"channelId":1,"currentLevel":0,"targetLevel":null},{"channelId":2,"currentLevel":0,"targetLevel":null},{"channelId":3,"currentLevel":0,"targetLevel":null},{"channelId":4,"currentLevel":0,"targetLevel":null},{"channelId":5,"currentLevel":0,"targetLevel":null},{"channelId":6,"currentLevel":0,"targetLevel":null},{"channelId":7,"currentLevel":0,"targetLevel":null},{"channelId":8,"currentLevel":0,"targetLevel":null},{"channelId":9,"currentLevel":0,"targetLevel":null},{"channelId":10,"currentLevel":0,"targetLevel":null},{"channelId":11,"currentLevel":0,"targetLevel":null},{"channelId":12,"currentLevel":0,"targetLevel":null},{"channelId":13,"currentLevel":0,"targetLevel":null},{"channelId":14,"currentLevel":0,"targetLevel":null},{"channelId":15,"currentLevel":0,"targetLevel":null}],"roomId":12,"currentScene":0},{"channel":[{"channelId":0,"currentLevel":0,"targetLevel":null},{"channelId":1,"currentLevel":0,"targetLevel":null},{"channelId":2,"currentLevel":0,"targetLevel":null},{"channelId":3,"currentLevel":0,"targetLevel":null},{"channelId":4,"currentLevel":0,"targetLevel":null},{"channelId":5,"currentLevel":0,"targetLevel":null},{"channelId":6,"currentLevel":0,"targetLevel":null},{"channelId":7,"currentLevel":0,"targetLevel":null},{"channelId":8,"currentLevel":0,"targetLevel":null},{"channelId":9,"currentLevel":0,"targetLevel":null},{"channelId":10,"currentLevel":0,"targetLevel":null},{"channelId":11,"currentLevel":0,"targetLevel":null},{"channelId":12,"currentLevel":0,"targetLevel":null},{"channelId":13,"currentLevel":0,"targetLevel":null},{"channelId":14,"currentLevel":0,"targetLevel":null},{"channelId":15,"currentLevel":0,"targetLevel":null}],"roomId":13,"currentScene":0},{"channel":[{"channelId":0,"currentLevel":0,"targetLevel":null},{"channelId":1,"currentLevel":0,"targetLevel":null},{"channelId":2,"currentLevel":0,"targetLevel":null},{"channelId":3,"currentLevel":0,"targetLevel":null},{"channelId":4,"currentLevel":0,"targetLevel":null},{"channelId":5,"currentLevel":0,"targetLevel":null},{"channelId":6,"currentLevel":0,"targetLevel":null},{"channelId":7,"currentLevel":0,"targetLevel":null},{"channelId":8,"currentLevel":0,"targetLevel":null},{"channelId":9,"currentLevel":0,"targetLevel":null},{"channelId":10,"currentLevel":0,"targetLevel":null},{"channelId":11,"currentLevel":0,"targetLevel":null},{"channelId":12,"currentLevel":0,"targetLevel":null},{"channelId":13,"currentLevel":0,"targetLevel":null},{"channelId":14,"currentLevel":0,"targetLevel":null},{"channelId":15,"currentLevel":0,"targetLevel":null}],"roomId":14,"currentScene":0},{"channel":[{"channelId":0,"currentLevel":0,"targetLevel":null},{"channelId":1,"currentLevel":0,"targetLevel":null},{"channelId":2,"currentLevel":0,"targetLevel":null},{"channelId":3,"currentLevel":0,"targetLevel":null},{"channelId":4,"currentLevel":0,"targetLevel":null},{"channelId":5,"currentLevel":0,"targetLevel":null},{"channelId":6,"currentLevel":0,"targetLevel":null},{"channelId":7,"currentLevel":0,"targetLevel":null},{"channelId":8,"currentLevel":0,"targetLevel":null},{"channelId":9,"currentLevel":0,"targetLevel":null},{"channelId":10,"currentLevel":0,"targetLevel":null},{"channelId":11,"currentLevel":0,"targetLevel":null},{"channelId":12,"currentLevel":0,"targetLevel":null},{"channelId":13,"currentLevel":0,"targetLevel":null},{"channelId":14,"currentLevel":0,"targetLevel":null},{"channelId":15,"currentLevel":0,"targetLevel":null}],"roomId":15,"currentScene":0},{"channel":[{"channelId":0,"currentLevel":0,"targetLevel":null},{"channelId":1,"currentLevel":0,"targetLevel":null},{"channelId":2,"currentLevel":0,"targetLevel":null},{"channelId":3,"currentLevel":0,"targetLevel":null},{"channelId":4,"currentLevel":0,"targetLevel":null},{"channelId":5,"currentLevel":0,"targetLevel":null},{"channelId":6,"currentLevel":0,"targetLevel":null},{"channelId":7,"currentLevel":0,"targetLevel":null},{"channelId":8,"currentLevel":0,"targetLevel":null},{"channelId":9,"currentLevel":0,"targetLevel":null},{"channelId":10,"currentLevel":0,"targetLevel":null},{"channelId":11,"currentLevel":0,"targetLevel":null},{"channelId":12,"currentLevel":0,"targetLevel":null},{"channelId":13,"currentLevel":0,"targetLevel":null},{"channelId":14,"currentLevel":0,"targetLevel":null},{"channelId":15,"currentLevel":0,"targetLevel":null}],"roomId":16,"currentScene":0},{"channel":[{"channelId":0,"currentLevel":0,"targetLevel":null},{"channelId":1,"currentLevel":0,"targetLevel":null},{"channelId":2,"currentLevel":0,"targetLevel":null},{"channelId":3,"currentLevel":0,"targetLevel":null},{"channelId":4,"currentLevel":0,"targetLevel":null},{"channelId":5,"currentLevel":0,"targetLevel":null},{"channelId":6,"currentLevel":0,"targetLevel":null},{"channelId":7,"currentLevel":0,"targetLevel":null},{"channelId":8,"currentLevel":0,"targetLevel":null},{"channelId":9,"currentLevel":0,"targetLevel":null},{"channelId":10,"currentLevel":0,"targetLevel":null},{"channelId":11,"currentLevel":0,"targetLevel":null},{"channelId":12,"currentLevel":0,"targetLevel":null},{"channelId":13,"currentLevel":0,"targetLevel":null},{"channelId":14,"currentLevel":0,"targetLevel":null},{"channelId":15,"currentLevel":0,"targetLevel":null}],"roomId":17,"currentScene":0},{"channel":[{"channelId":0,"currentLevel":0,"targetLevel":null},{"channelId":1,"currentLevel":0,"targetLevel":null},{"channelId":2,"currentLevel":0,"targetLevel":null},{"channelId":3,"currentLevel":0,"targetLevel":null},{"channelId":4,"currentLevel":0,"targetLevel":null},{"channelId":5,"currentLevel":0,"targetLevel":null},{"channelId":6,"currentLevel":0,"targetLevel":null},{"channelId":7,"currentLevel":0,"targetLevel":null},{"channelId":8,"currentLevel":0,"targetLevel":null},{"channelId":9,"currentLevel":0,"targetLevel":null},{"channelId":10,"currentLevel":0,"targetLevel":null},{"channelId":11,"currentLevel":0,"targetLevel":null},{"channelId":12,"currentLevel":0,"targetLevel":null},{"channelId":13,"currentLevel":0,"targetLevel":null},{"channelId":14,"currentLevel":0,"targetLevel":null},{"channelId":15,"currentLevel":0,"targetLevel":null}],"roomId":19,"currentScene":0},{"channel":[{"channelId":0,"currentLevel":0,"targetLevel":null},{"channelId":1,"currentLevel":0,"targetLevel":null},{"channelId":2,"currentLevel":0,"targetLevel":null},{"channelId":3,"currentLevel":0,"targetLevel":null},{"channelId":4,"currentLevel":0,"targetLevel":null},{"channelId":5,"currentLevel":0,"targetLevel":null},{"channelId":6,"currentLevel":0,"targetLevel":null},{"channelId":7,"currentLevel":0,"targetLevel":null},{"channelId":8,"currentLevel":0,"targetLevel":null},{"channelId":9,"currentLevel":0,"targetLevel":null},{"channelId":10,"currentLevel":0,"targetLevel":null},{"channelId":11,"currentLevel":0,"targetLevel":null},{"channelId":12,"currentLevel":0,"targetLevel":null},{"channelId":13,"currentLevel":0,"targetLevel":null},{"channelId":14,"currentLevel":0,"targetLevel":null},{"channelId":15,"currentLevel":0,"targetLevel":null}],"roomId":21,"currentScene":0},{"channel":[{"channelId":0,"currentLevel":0,"targetLevel":null},{"channelId":1,"currentLevel":0,"targetLevel":null},{"channelId":2,"currentLevel":0,"targetLevel":null},{"channelId":3,"currentLevel":0,"targetLevel":null},{"channelId":4,"currentLevel":0,"targetLevel":null},{"channelId":5,"currentLevel":0,"targetLevel":null},{"channelId":6,"currentLevel":0,"targetLevel":null},{"channelId":7,"currentLevel":0,"targetLevel":null},{"channelId":8,"currentLevel":0,"targetLevel":null},{"channelId":9,"currentLevel":0,"targetLevel":null},{"channelId":10,"currentLevel":0,"targetLevel":null},{"channelId":11,"currentLevel":0,"targetLevel":null},{"channelId":12,"currentLevel":0,"targetLevel":null},{"channelId":13,"currentLevel":0,"targetLevel":null},{"channelId":14,"currentLevel":0,"targetLevel":null},{"channelId":15,"currentLevel":0,"targetLevel":null}],"roomId":22,"currentScene":0},{"channel":[{"channelId":0,"currentLevel":0,"targetLevel":null},{"channelId":1,"currentLevel":0,"targetLevel":null},{"channelId":2,"currentLevel":0,"targetLevel":null},{"channelId":3,"currentLevel":0,"targetLevel":null},{"channelId":4,"currentLevel":0,"targetLevel":null},{"channelId":5,"currentLevel":0,"targetLevel":null},{"channelId":6,"currentLevel":0,"targetLevel":null},{"channelId":7,"currentLevel":0,"targetLevel":null},{"channelId":8,"currentLevel":0,"targetLevel":null},{"channelId":9,"currentLevel":0,"targetLevel":null},{"channelId":10,"currentLevel":0,"targetLevel":null},{"channelId":11,"currentLevel":0,"targetLevel":null},{"channelId":12,"currentLevel":0,"targetLevel":null},{"channelId":13,"currentLevel":0,"targetLevel":null},{"channelId":14,"currentLevel":0,"targetLevel":null},{"channelId":15,"currentLevel":0,"targetLevel":null}],"roomId":23,"currentScene":0},{"channel":[{"channelId":0,"currentLevel":0,"targetLevel":null},{"channelId":1,"currentLevel":0,"targetLevel":null},{"channelId":2,"currentLevel":0,"targetLevel":null},{"channelId":3,"currentLevel":0,"targetLevel":null},{"channelId":4,"currentLevel":0,"targetLevel":null},{"channelId":5,"currentLevel":0,"targetLevel":null},{"channelId":6,"currentLevel":0,"targetLevel":null},{"channelId":7,"currentLevel":0,"targetLevel":null},{"channelId":8,"currentLevel":0,"targetLevel":null},{"channelId":9,"currentLevel":0,"targetLevel":null},{"channelId":10,"currentLevel":0,"targetLevel":null},{"channelId":11,"currentLevel":0,"targetLevel":null},{"channelId":12,"currentLevel":0,"targetLevel":null},{"channelId":13,"currentLevel":0,"targetLevel":null},{"channelId":14,"currentLevel":0,"targetLevel":null},{"channelId":15,"currentLevel":0,"targetLevel":null}],"roomId":25,"currentScene":0},{"channel":[{"channelId":0,"currentLevel":0,"targetLevel":null},{"channelId":1,"currentLevel":0,"targetLevel":null},{"channelId":2,"currentLevel":0,"targetLevel":null},{"channelId":3,"currentLevel":0,"targetLevel":null},{"channelId":4,"currentLevel":0,"targetLevel":null},{"channelId":5,"currentLevel":0,"targetLevel":null},{"channelId":6,"currentLevel":0,"targetLevel":null},{"channelId":7,"currentLevel":0,"targetLevel":null},{"channelId":8,"currentLevel":0,"targetLevel":null},{"channelId":9,"currentLevel":0,"targetLevel":null},{"channelId":10,"currentLevel":0,"targetLevel":null},{"channelId":11,"currentLevel":0,"targetLevel":null},{"channelId":12,"currentLevel":0,"targetLevel":null},{"channelId":13,"currentLevel":0,"targetLevel":null},{"channelId":14,"currentLevel":0,"targetLevel":null},{"channelId":15,"currentLevel":0,"targetLevel":null}],"roomId":26,"currentScene":0},{"channel":[{"channelId":0,"currentLevel":0,"targetLevel":null},{"channelId":1,"currentLevel":0,"targetLevel":null},{"channelId":2,"currentLevel":0,"targetLevel":null},{"channelId":3,"currentLevel":0,"targetLevel":null},{"channelId":4,"currentLevel":0,"targetLevel":null},{"channelId":5,"currentLevel":0,"targetLevel":null},{"channelId":6,"currentLevel":0,"targetLevel":null},{"channelId":7,"currentLevel":0,"targetLevel":null},{"channelId":8,"currentLevel":0,"targetLevel":null},{"channelId":9,"currentLevel":0,"targetLevel":null},{"channelId":10,"currentLevel":0,"targetLevel":null},{"channelId":11,"currentLevel":0,"targetLevel":null},{"channelId":12,"currentLevel":0,"targetLevel":null},{"channelId":13,"currentLevel":0,"targetLevel":null},{"channelId":14,"currentLevel":0,"targetLevel":null},{"channelId":15,"currentLevel":0,"targetLevel":null}],"roomId":29,"currentScene":0}]}
Parse the entire packet [query_LEVEL]




{"name": "tracker","payload": {"roomId": 85,"channelId": 4,"currentLevel": 127,"targetLevel": 90,"timeToTake": 230,"temporary": false}}

 * 
 * 
 * 
Product_Type:           Hub
Product_HubId:          12345cad-254f-0000-beef-4d63deadbeef
Product_MAC:            70:B3:D5:--:--:--
Product_Version:        3.1.6


*/
