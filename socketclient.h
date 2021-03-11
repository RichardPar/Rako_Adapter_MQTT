#ifndef SOCKETCLIENT_H
#define SOCKETCLIENT_H

#include <pthread.h>
 
#define MAX_BUFFER_SIZE 1
struct socket_client_t {
    void *pvt;
    int sock;
    int state; 
    int port;
    char host[32];
    int RUNNING;
    char buffer[MAX_BUFFER_SIZE];
    
    int (*func_connected)(void *,struct socket_client_t*, int);
    int (*func_disconnected)(int);
    int (*func_parse)(void *,struct socket_client_t*,int, char*, int);
    int (*func_idle)(void *pvt,struct socket_client_t*);
};


void socket_client_start(struct socket_client_t* s);
void socket_client_write(struct socket_client_t* s,  char *buffer, int len);


#endif