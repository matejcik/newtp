#ifndef SERVER__H__
#define SERVER__H__

#include "structs.h"

/* the functions are supposed to do their own replying;
 * return value is success/failure
 */
int server_init (int socket);

int cmd_assign (int socket, struct command *cmd);
int cmd_list (int socket, struct command *cmd);
int cmd_list_cont (int socket, struct command *cmd);
int cmd_stat (int socket, struct command *cmd);
int cmd_read (int socket, struct command *cmd);
int cmd_write (int socket, struct command *cmd);
int cmd_delete (int socket, struct command *cmd);
int cmd_rename (int socket, struct command *cmd);


#endif
