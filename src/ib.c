#include "../include/ib.h"
#include "../include/config.h"
#include "../include/io.h"
#include "../include/util.h"
#include "infiniband/verbs.h"
#include <arpa/inet.h>
#include <assert.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define MESSAGE_SIZE 64
#define MAX_NUM_MESSAGES 20

ib_context ib_res;

// exchange qp information
// do client syncing to make sure that both clients have been initialized
int setup_ib(bool is_server) {
    int ret = 0;
    struct ibv_device **dev_list = NULL;
    memset(&ib_res, 0, sizeof(ib_context));

    int num_devices;
    dev_list = ibv_get_device_list(&num_devices);
    assert(num_devices > 0 && "No RDMA devices detected");

    ib_res.ibv_ctx = ibv_open_device(*dev_list);
    assert(ib_res.ibv_ctx != NULL && "Failed to open IB device");

    ib_res.pd = ibv_alloc_pd(ib_res.ibv_ctx);
    assert(ib_res.pd != NULL && "Failed to alloc PD");

    ret = ibv_query_port(ib_res.ibv_ctx, 1, &ib_res.port_attr);
    assert(ret == 0 && "Could not query device");

    // setup the buffer
    // we add 1 after configuring the max number of messages becuase the last one is used
    // for the send buffer
    ib_res.ib_buf_size = MESSAGE_SIZE * MAX_NUM_MESSAGES;

    ret = posix_memalign((void **)&ib_res.ib_buf, 4096, ib_res.ib_buf_size + MESSAGE_SIZE);
    assert(ib_res.ib_buf != NULL && "Failed to allocate ib buf");

    ib_res.mr =
        ibv_reg_mr(ib_res.pd, (void *)ib_res.ib_buf, ib_res.ib_buf_size + MESSAGE_SIZE,
                   IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ);
    assert(ib_res.mr != NULL && "Could not register MR");

    // clear the recv buffer
    memset(ib_res.ib_buf, 0, MESSAGE_SIZE * MAX_NUM_MESSAGES);
    // set the send buffer
    memset(ib_res.ib_buf + (MESSAGE_SIZE * MAX_NUM_MESSAGES), 'A', MESSAGE_SIZE);

    ret = ibv_query_device(ib_res.ibv_ctx, &ib_res.dev_attr);
    assert(ret == 0 && "Failed to query device");

    // TODO: test by creating completion channels after this
    ib_res.cq = ibv_create_cq(ib_res.ibv_ctx, ib_res.dev_attr.max_cqe, NULL, NULL, 0);
    assert(ib_res.cq != NULL && "Could not create completion queue");

    // create the QP
    struct ibv_qp_init_attr qp_init_attr = {
        .send_cq = ib_res.cq,
        .recv_cq = ib_res.cq,
        .cap =
            {
                .max_send_wr = ib_res.dev_attr.max_qp_wr,
                .max_recv_wr = ib_res.dev_attr.max_qp_wr,
                .max_send_sge = 1,
                .max_recv_sge = 1,
                .max_inline_data = config_info.msg_size,
            },
        .qp_type = IBV_QPT_RC,
    };

    ib_res.qp = ibv_create_qp(ib_res.pd, &qp_init_attr);
    assert(ib_res.qp != NULL && "Failed to create a QP");

    if (config_info.is_server) {
        ret = connect_qp_server();
    } else {
        ret = connect_qp_client();
    }
    assert(ret == 0 && "Failed to exchange QP information");

    ibv_free_device_list(dev_list);
    return 0;
}

void close_ib() {
    if (ib_res.qp) {
        ibv_destroy_qp(ib_res.qp);
    }

    if (ib_res.cq) {
        ibv_destroy_cq(ib_res.cq);
    }

    if (ib_res.mr) {
        ibv_dereg_mr(ib_res.mr);
    }

    if (ib_res.pd) {
        ibv_dealloc_pd(ib_res.pd);
    }

    if (ib_res.ibv_ctx) {
        ibv_close_device(ib_res.ibv_ctx);
    }

    if (ib_res.ib_buf) {
        free(ib_res.ib_buf);
    }

    return;
}

int sock_get_qp_info(int peer_sock_fd, QpInfo *qp_info) {
    QpInfo tmp_info;

    ssize_t ret = io_read(peer_sock_fd, (uint8_t *)&tmp_info, sizeof(QpInfo));
    assert(ret > 0 && "Could not properly read QP info information");

    qp_info->gid = tmp_info.gid;
    qp_info->qp_num = ntohl(tmp_info.qp_num);
    qp_info->rkey = ntohl(tmp_info.rkey);
    qp_info->raddr = ntohll(tmp_info.raddr);

    return 0;
}

int sock_send_qp_info(int peer_sock_fd, QpInfo *qp_info) {
    QpInfo tmp_info;

    memcpy(&tmp_info, qp_info, sizeof(QpInfo));
    tmp_info.qp_num = htonl(qp_info->qp_num);
    tmp_info.rkey = htonl(qp_info->rkey);
    tmp_info.raddr = htonll(qp_info->raddr);

    ssize_t ret = io_write(peer_sock_fd, (uint8_t *)&tmp_info, sizeof(QpInfo));
    assert(ret > 0 && "Could not write QP informaion");

    return 0;
}

static int get_socket_and_bind() {
    struct addrinfo hints;
    struct addrinfo *result, *rp;
    int ret = 0, sockfd = 0;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;
    hints.ai_flags = AI_PASSIVE;

    ret = getaddrinfo(NULL, config_info.sock_port, &hints, &result);
    assert(ret == 0 && "getaddrinfo error.");

    for (rp = result; rp != NULL; rp = rp->ai_next) {
        sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sockfd < 0) {
            continue;
        }

        ret = bind(sockfd, rp->ai_addr, rp->ai_addrlen);
        if (ret == 0) {
            /* bind success */
            break;
        }

        close(sockfd);
        sockfd = -1;
    }

    assert(rp != NULL && "creating socket.");
    freeaddrinfo(result);

    return sockfd;
}

int connect_qp_server() {
    int ret = 0;
    int sockfd = 0;
    int peer_sockfd = 0;

    struct sockaddr_in peer_addr;
    socklen_t peer_addr_len = sizeof(peer_addr);
    char sock_buf[64] = {0};
    QpInfo local_qp_info, remote_qp_info;

    sockfd = get_socket_and_bind();

    assert(sockfd > 0 && "Could not get a proper fd to bind");
    listen(sockfd, 5);

    peer_sockfd = accept(sockfd, (struct sockaddr *)&peer_addr, &peer_addr_len);
    assert(peer_sockfd > 0 && "Could not get proper client connection");

    // in this case, port num is relevant when the same device has multiple ports
    // that might not be the case for soft roce, but there are cards that can have more than
    // 1 port
    ret = ibv_query_gid(ib_res.ibv_ctx, 1, 2, &local_qp_info.gid);
    assert(ret == 0 && "Could not query device GID");
    local_qp_info.qp_num = ib_res.qp->qp_num;
    local_qp_info.rkey = ib_res.mr->rkey;
    local_qp_info.raddr = (uintptr_t)ib_res.ib_buf;

    // server will send its qp information first, then receive
    sock_send_qp_info(peer_sockfd, &local_qp_info);
    sock_get_qp_info(peer_sockfd, &remote_qp_info);

    ib_res.rkey = remote_qp_info.rkey;
    ib_res.raddr_base = remote_qp_info.raddr;

    // move the QP into RTS state
    ret = modify_rts(ib_res.qp, remote_qp_info.qp_num, remote_qp_info.gid);
    assert(ret == 0 && "Failed to modify QP to RTS");

    // put up a barrier to ensure that both sides have reached this stage
    ret = io_read(peer_sockfd, (uint8_t *)sock_buf, sizeof(SOCK_SYNC_MSG));
    assert(ret == sizeof(SOCK_SYNC_MSG) && "Could not get connection from peer");

    ret = io_write(peer_sockfd, (uint8_t *)sock_buf, sizeof(SOCK_SYNC_MSG));
    assert(ret == sizeof(SOCK_SYNC_MSG) && "Could not write to peer connection");

    printf("Connection between both peers has been set up: local qp: %d <-> remote qp: %d\n",
           local_qp_info.qp_num, remote_qp_info.qp_num);
    printf("Buf addr: %p <-> Remote addr: %p\n", ib_res.ib_buf, (char *)ib_res.raddr_base);
    printf("Local rkey: %u <-> Rmote rkey: %u\n", local_qp_info.rkey, ib_res.rkey);
    close(peer_sockfd);
    close(sockfd);

    return 0;
}

static int get_socket_and_connect(const char *server_name) {
    struct addrinfo hints;
    struct addrinfo *result, *rp;
    int sock_fd = -1, ret = 0;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;

    ret = getaddrinfo(server_name, config_info.sock_port, &hints, &result);
    assert(ret == 0 && "Could not get addr info");

    for (rp = result; rp != NULL; rp = rp->ai_next) {
        sock_fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sock_fd == -1) {
            continue;
        }

        ret = connect(sock_fd, rp->ai_addr, rp->ai_addrlen);
        if (ret == 0) {
            /* connection success */
            break;
        }

        close(sock_fd);
        sock_fd = -1;
    }

    assert(rp != NULL && "Could not connect");

    freeaddrinfo(result);
    return sock_fd;
}

int connect_qp_client() {
    int ret = 0;
    int sockfd = 0;

    char sock_buf[64] = SOCK_SYNC_MSG;
    QpInfo local_qp_info, remote_qp_info;

    sockfd = get_socket_and_connect(config_info.server_name);
    assert(sockfd > 0 && "Could not get a proper fd");

    ret = ibv_query_gid(ib_res.ibv_ctx, 1, 2, &local_qp_info.gid);
    local_qp_info.qp_num = ib_res.qp->qp_num;
    local_qp_info.rkey = ib_res.mr->rkey;
    local_qp_info.raddr = (uintptr_t)ib_res.ib_buf;

    // exchange qp info
    sock_get_qp_info(sockfd, &remote_qp_info);
    sock_send_qp_info(sockfd, &local_qp_info);

    ib_res.rkey = remote_qp_info.rkey;
    ib_res.raddr_base = remote_qp_info.raddr;

    ret = modify_rts(ib_res.qp, remote_qp_info.qp_num, remote_qp_info.gid);
    assert(ret == 0 && "Failed to modify QP to RTS");

    ret = io_write(sockfd, (uint8_t *)sock_buf, sizeof(SOCK_SYNC_MSG));
    assert(ret == sizeof(SOCK_SYNC_MSG) && "Could not write sync message");

    ret = io_read(sockfd, (uint8_t *)sock_buf, sizeof(SOCK_SYNC_MSG));
    assert(ret == sizeof(SOCK_SYNC_MSG) && "Could not read socket message");

    printf("Connection between both peers has been set up: local qp: %d <-> remote qp: %d\n",
           local_qp_info.qp_num, remote_qp_info.qp_num);
    printf("Buf addr: %p <--> Remote addr: %p\n", ib_res.ib_buf, (char *)ib_res.raddr_base);
    printf("Local rkey: %u <-> Rmote rkey: %u\n", local_qp_info.rkey, ib_res.rkey);
    close(sockfd);
    return 0;
}

int modify_rts(struct ibv_qp *qp, uint32_t qp_num, union ibv_gid gid) {
    int ret = 0;
    {
        struct ibv_qp_attr qp_attr = {
            .qp_state = IBV_QPS_INIT,
            .pkey_index = 0,
            .port_num = 1,
            .qp_access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ |
                               IBV_ACCESS_REMOTE_ATOMIC | IBV_ACCESS_REMOTE_WRITE,
        };
        ret = ibv_modify_qp(qp, &qp_attr,
                            IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS);
        assert(ret == 0 && "Could not bring QP to init");
    }

    {
        struct ibv_qp_attr qp_attr = {.qp_state = IBV_QPS_RTR,
                                      .path_mtu = IBV_MTU_1024,
                                      .dest_qp_num = qp_num,
                                      .rq_psn = 0,
                                      .max_dest_rd_atomic = 1,
                                      .min_rnr_timer = 12,
                                      .ah_attr = {.is_global = 1,
                                                  .sl = 0,
                                                  .src_path_bits = 0,
                                                  .port_num = 1,
                                                  .grh = {
                                                      .dgid = gid,
                                                      .sgid_index = 2,
                                                      .hop_limit = 0xFF,
                                                  }}};

        ret = ibv_modify_qp(qp, &qp_attr,
                            IBV_QP_STATE | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN | IBV_QP_RQ_PSN |
                                IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER | IBV_QP_AV);
        assert(ret == 0 && "Could not bring QP to RTR");
    }

    {
        struct ibv_qp_attr qp_attr = {
            .qp_state = IBV_QPS_RTS,
            .timeout = 14,
            .retry_cnt = 7,
            .rnr_retry = 7,
            .sq_psn = 0,
            .max_rd_atomic = 1,
        };

        ret = ibv_modify_qp(ib_res.qp, &qp_attr,
                            IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY |
                                IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC);
        assert(ret == 0 && "Could not bring QP to RTS");
    }

    return 0;
}

int post_send(uint32_t req_size, uint32_t lkey, uint64_t wr_id, uint32_t imm_data,
              struct ibv_qp *qp, char *buf) {
    int ret = 0;
    struct ibv_send_wr *bad_send_wr;

    struct ibv_sge list = {.addr = (uintptr_t)buf, .length = req_size, .lkey = lkey};
    struct ibv_send_wr send_wr = {.wr_id = wr_id,
                                  .sg_list = &list,
                                  .num_sge = 1,
                                  .opcode = IBV_WR_SEND_WITH_IMM,
                                  .send_flags = IBV_SEND_SIGNALED,
                                  .imm_data = htonl(imm_data)};
    ret = ibv_post_send(qp, &send_wr, &bad_send_wr);
    return ret;
}

int post_recv(uint32_t req_size, uint32_t lkey, uint64_t wr_id, struct ibv_qp *qp, char *buf) {
    int ret = 0;
    struct ibv_recv_wr *bad_send_wr;

    struct ibv_sge list = {.addr = (uintptr_t)buf, .length = req_size, lkey = lkey};
    struct ibv_recv_wr recv_wr = {.wr_id = wr_id, .sg_list = &list, .num_sge = 1};

    ret = ibv_post_recv(qp, &recv_wr, &bad_send_wr);
    return ret;
}

int post_write(uint32_t req_size, uint32_t lkey, uint64_t wr_id, struct ibv_qp *qp, char *buf,
               uint64_t raddr, uint32_t rkey, bool is_signalled) {
    int ret = 0;
    struct ibv_send_wr *bad_wr;

    struct ibv_sge list = {
        .addr = (uintptr_t)buf,
        .length = req_size,
        .lkey = lkey,
    };

    struct ibv_send_wr send_wr = {
        .wr_id = wr_id,
        .sg_list = &list,
        .num_sge = 1,
        .opcode = IBV_WR_RDMA_WRITE,
        .send_flags = is_signalled ? IBV_SEND_SIGNALED : 0 | IBV_SEND_INLINE,
        .wr.rdma.remote_addr = raddr,
        .wr.rdma.rkey = rkey,
    };

    ret = ibv_post_send(qp, &send_wr, &bad_wr);
    return ret;
}