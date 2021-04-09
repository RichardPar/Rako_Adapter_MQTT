#include "socketclient.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>


void* socket_client_main_thread(void* paramPtr);

void socket_client_start(struct socket_client_t* s)
{
    pthread_t socket_thread;

    pthread_attr_t attributes;
    pthread_attr_init(&attributes);
    pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
    s->sock = -1;
    pthread_create(&socket_thread, &attributes, socket_client_main_thread, s);

    return;
}

void socket_client_write(struct socket_client_t* s,  char *buffer, int len)
{
    if (s->RUNNING == 1) {
        send(s->sock,buffer,len, 0);
        usleep(100);
    }
    return;
}


#if 0
int main(int argc, char *argv[])
{
    int socket_desc;
    struct sockaddr_in server;

    //Create socket
    socket_desc = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_desc == -1) {
        printf("Could not create socket");
    }

    server.sin_addr.s_addr = inet_addr("74.125.235.20");
    server.sin_family = AF_INET;
    server.sin_port = htons( 80 );

    //Connect to remote server
    if (connect(socket_desc, (struct sockaddr *)&server, sizeof(server)) < 0) {
        puts("connect error");
        return 1;
    }

    puts("Connected");
    return 0;
}
#endif

void* socket_client_main_thread(void* paramPtr)
{

    struct socket_client_t* params = paramPtr;
    struct sockaddr_in server;

    int socket_desc;


    params->RUNNING=1;
    params->state = 0;

    while(params->RUNNING == 1) {

        if(params->state == 0) {
            params->sock = socket(AF_INET, SOCK_STREAM, 0);
            if(params->sock == -1) {
                printf("Could not create socket");
                pthread_exit(NULL);
            }
            fcntl(params->sock, F_SETFL, O_NONBLOCK);

            server.sin_addr.s_addr = inet_addr(params->host);
            server.sin_family = AF_INET;
            server.sin_port = htons( params->port );

            params->state = 1;
            continue;
        } // State == 0

        if(params->state == 1) {
            int rc;
            rc = connect(params->sock, (struct sockaddr*)&server, sizeof(server));
            if(rc < 0) {
                perror("connect failed. Error");
                sleep(1);
            } else {
                if(params->func_connected != NULL) {
                    params->func_connected(params->pvt,params,params->sock);
                }
                params->state = 2;
                continue;
            }
        } // State == 1

        if(params->state == 2) {
            int rc;
            rc = recv(params->sock, params->buffer, MAX_BUFFER_SIZE, 0);
            if(rc == 0) {
                close(params->sock);
                params->sock = -1;
                params->state = 0;
                continue;
            }

            if(rc > 0) {
                if(params->func_parse != NULL) {
                    params->func_parse(params->pvt,params,params->sock, params->buffer, rc);
                }
            } else { // Got a reponse for something!
                if (params->func_idle != NULL) {
                    rc = params->func_idle(params->pvt,params);
                    if (rc < 0 )
                    {
                        close(params->sock);
                        params->sock = -1;
                        params->state = 0;
                        continue;
                    }
                    
                }
            }
        } // End of state == 2

    } // End of RUNNING == 1;

    pthread_exit(0);
}
