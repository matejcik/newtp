#ifndef CLIENTOPS__H__
#define CLIENTOPS__H__

#include <stdint.h>
#include <gsasl.h>
#include "structs.h"

extern char * inbuf;
extern char * outbuf;
extern char * data_in;
extern char * data_out;

void recv_reply (struct reply * reply);
void reply_for_command (uint8_t, uint8_t, uint16_t, uint16_t, struct reply *);

int newtp_client_connect (char const * host, char const * port, struct intro * intro, char ** extensions);
void newtp_client_sasl_auth (Gsasl * ctx, struct intro * intro);


#endif /* CLIENTOPS__H__ */
