#ifndef IB_H_
#define IB_H_

#include <infiniband/verbs.h>
#include <stdbool.h>

#define SIG_INTERVAL 20

// things that have to be done to set up a basic connection
// 1. create your own ib structs

typedef struct {
    struct ibv_context *ibv_ctx;

    struct ibv_pd *pd;
    struct ibv_mr *mr;
    struct ibv_cq *cq;
    struct ibv_qp *qp;

    // TODO: not sure what the point of port attr and dev attr are...
    struct ibv_port_attr port_attr;
    struct ibv_device_attr dev_attr;

    char *ib_buf;
    size_t ib_buf_size;

    // store the remote host key
    uint32_t rkey;
    uint64_t raddr_base;

    // stuff for message batching
    struct ibv_send_wr *send_wrs;
    struct ibv_sge *send_sges;
} ib_context;

extern ib_context ib_res;

// Exchange QP info
typedef struct {
    union ibv_gid gid;
    uint32_t qp_num;

    uint32_t rkey;
    uint64_t raddr;
} QpInfo;

enum MsgType {
    MSG_CTL_START = 0,
    MSG_CTL_END,
    MSG_REGULAR,
};

#define SOCK_SYNC_MSG "sync"

int setup_ib(bool is_server);
void close_ib();

int modify_rts(struct ibv_qp *qp, uint32_t qp_num, union ibv_gid gid);
int post_send(uint32_t req_size, uint32_t lkey, uint64_t wr_id, uint32_t imm_data,
              struct ibv_qp *qp, char *buf);
int post_recv(uint32_t req_size, uint32_t lkey, uint64_t wr_id, struct ibv_qp *qp, char *buf);

int post_write(uint32_t req_size, uint32_t lkey, uint64_t wr_id, struct ibv_qp *qp, char *buf,
               uint64_t raddr, uint32_t rkey, bool is_signalled);

int connect_qp_server();
int connect_qp_client();

#endif