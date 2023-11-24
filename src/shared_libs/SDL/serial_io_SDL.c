#include <stdint.h>
#include <string.h>

#include "../../non_core/serial_io_transfer.h"
#include "../../non_core/mobile.h"
#include "../../non_core/logger.h"

#ifdef THREE_DS
#include "SDL/SDL.h"
//#include "SDL/SDL_net.h"
uint32_t *SOCUBuffer = NULL;
#else
#include "SDL.h"
#endif

#if !defined(PSP) && !defined(EMSCRIPTEN) && !defined(DREAMCAST) && !defined(THREE_DS)
#include "SDL_net.h"
#endif

static int is_client = 0;
static int is_server = 0;
static int connection_up = 0;

#if !defined(PSP) && !defined(EMSCRIPTEN) && !defined(DREAMCAST) && !defined(THREE_DS)
static TCPsocket client = NULL;
static TCPsocket server = NULL;
static SDLNet_SocketSet socketset; 
#endif  
/* Setup TCP Client, and attempt to connect
 * to the server */
int setup_client(unsigned port) {

#if !defined(PSP) && !defined(EMSCRIPTEN) && !defined(DREAMCAST) && !defined(THREE_DS)
    is_client = 1;

    log_message(LOG_INFO, "Attempting to connect to server on port %u\n",port);
    //SDL_INIT(SDL_INIT_EVERYTHING); 
    SDLNet_Init();   

    IPaddress ip;
    //TODO, for now always connect to localhost, fix for any specified ip in
    //the future
    SDLNet_ResolveHost(&ip, "localhost", port);

    client =  SDLNet_TCP_Open(&ip);
    socketset = SDLNet_AllocSocketSet(1); 
    char buf[100];
    int i = SDLNet_TCP_Recv(client, buf, 100);
    for (int j = 0; j < i; j++) {
        printf("%c",buf[j]);
    }
    printf("\n");
    SDLNet_TCP_AddSocket(socketset, client);
    connection_up = 1;
    return 1;
#endif
    return 0;

}

/*  Setup TCP Server, and wait for a single
 *  client to connect */
int setup_server(unsigned port) { 

#if !defined(PSP) && !defined(EMSCRIPTEN) && !defined(DREAMCAST) && !defined(THREE_DS)
    is_server = 1;

    log_message(LOG_INFO, "Starting server on port %u\n",port);

    //SDL_INIT(SDL_INIT_EVERYTHING); 
    SDLNet_Init();   

    IPaddress ip;
    SDLNet_ResolveHost(&ip, NULL, port);

    server = SDLNet_TCP_Open(&ip);

    log_message(LOG_INFO, "Waiting for client to connect\n");
    
    while (client == NULL) {
        client = SDLNet_TCP_Accept(server);
        SDL_Delay(1000);
    }
    const char * message = "Welcome to GB server";
    SDLNet_TCP_Send(client, message, strlen(message) + 1);
    log_message(LOG_INFO, "Client successfully connected\n");
    socketset = SDLNet_AllocSocketSet(1); 
    SDLNet_TCP_AddSocket(socketset, client);
    connection_up = 1;
    return 1;
#endif
    return 0;
}

/*  Send and Recieved byte */
int transfer(uint8_t data, uint8_t *recv, int ext) {

#if !defined(PSP) && !defined(EMSCRIPTEN) && !defined(DREAMCAST) && !defined(THREE_DS)
    
    log_message(LOG_INFO, "Sending byte %x\n", data);
    if ((is_server || is_client) && !ext) {
        printf("using internal clock\n");    
        if (SDLNet_TCP_Send(client, &data, 1) != 1){
            log_message(LOG_ERROR, "Error sending byte to client: %s\n",SDLNet_GetError());
            return 1;
        } 
        if (SDLNet_TCP_Recv(client, recv, 1) != 1) {
            log_message(LOG_ERROR, "Error recieving data back from client: %s\n", SDLNet_GetError());
            return 1;
        } else {log_message(LOG_INFO, "Recieved byte %x\n", *recv);}

        return 0;

    } else if ((is_client || is_server) && ext) {
        printf("using external clock\n");
        int res;
        if ((res = SDLNet_TCP_Recv(client, recv, 1)) != 1){
            log_message(LOG_ERROR, "Error recieving data from the server: %d %s\n",
                res,  SDLNet_GetError());
            return 1;

        } else {
            log_message(LOG_INFO, "Recieved byte %x\n", *recv);
        }
        
        if (SDLNet_TCP_Send(client, &data, 1) != 1) {
            log_message(LOG_ERROR, "Error sending data back to server: %s\n", SDLNet_GetError());
            return 1;
        }

        return 0;
    } // else {return 1;} // No networking enabled
    
#endif    
    static uint8_t next_byte = MOBILE_SERIAL_IDLE_BYTE;
    if (!ext) {
        *recv = next_byte;
        next_byte = MobileTransfer(data);
        return 0;
    }
    return 1;
}


void quit_io() {
#if !defined(PSP) && !defined(EMSCRIPTEN) && !defined(DREAMCAST) && !defined(THREE_DS)
    client = NULL;
    server = NULL;
    SDLNet_Quit();
#endif
}


// Transfer when current GB is using external clock
// returns 1 if there is data to be recieved, 0 otherwise
int transfer_ext(uint8_t data, uint8_t *recv) {
#if !defined(PSP) && !defined(EMSCRIPTEN) && !defined(DREAMCAST) && !defined(THREE_DS)
    if ( (is_client || is_server) &&
         (SDLNet_CheckSockets(socketset, 0) > 0) &&
         (SDLNet_SocketReady(client) > 0)) {
        
        return !transfer(data, recv, 1);        
    }
#endif
    return 0;
}

// Transfer when current GB is using internal clock
// returns 0xFF if no external GB found
uint8_t transfer_int(uint8_t data) {

// #if !defined(PSP) && !defined(EMSCRIPTEN) && !defined(DREAMCAST) && !defined(THREE_DS)
    uint8_t res;
    if (transfer(data, &res, 0)) {
        return 0xFF;
    } else {
        return res;
    }
// #endif
//     return 0xFF;
}

