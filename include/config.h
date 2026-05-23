#ifndef CONFIG_H_
#define CONFIG_H_

#include <inttypes.h>
#include <stdbool.h>

// you would normally do something like this to avoid false sharing
// but that is also not a super big concern here, since this data structure is likely read-only
struct ConfigInfo {
    bool is_server; /* if the current node is server */

    int msg_size;         /* the size of each echo message */
    int num_concurr_msgs; /* the number of messages can be sent concurrently */

    char *sock_port;   /* socket port number */
    char *server_name; /* server name */

    int batch_size;
} __attribute__((aligned(64)));

extern struct ConfigInfo config_info;

#endif /* CONFIG_H_*/