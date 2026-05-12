#include "../include/config.h"
#include "../include/ib.h"
#include <assert.h>
#include <stdio.h>

int main(int argc, char **argv) {
    int ret = 0;

    if (argc == 3) {
        config_info.is_server = false;
        config_info.sock_port = argv[1];
        config_info.server_name = argv[2];
    } else if (argc == 2) {
        config_info.is_server = true;
        config_info.sock_port = argv[1];
    } else {
        printf("Usage:\nClient: main <port> <server name>\nServer: main <port>\n");
        return 0;
    }

    ret = setup_ib(config_info.is_server);
    assert(ret == 0 && "Could not set up IB connection");

    return 0;
}