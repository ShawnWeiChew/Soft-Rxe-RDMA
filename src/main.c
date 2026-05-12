#include "../include/client.h"
#include "../include/config.h"
#include "../include/ib.h"
#include "../include/server.h"
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

    config_info.num_concurr_msgs = 20;
    config_info.msg_size = 64;

    ret = setup_ib(config_info.is_server);
    assert(ret == 0 && "Could not set up IB connection");

    if (config_info.is_server) {
        run_server();
    } else {
        run_client();
    }

    return 0;
}