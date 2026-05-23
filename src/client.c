#define _GNU_SOURCE

#include "../include/client.h"
#include "../include/config.h"
#include "../include/ib.h"
#include <assert.h>
#include <infiniband/verbs.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>

void *client_function(void *args) {
    cpu_set_t cpu_set;
    int ret = 0;
    int num_wc = 20;
    int num_concurrent_messages = config_info.num_concurr_msgs;
    int msg_size = config_info.msg_size;
    int buf_offset = 0;
    int message_recv_count = 0;
    char *buf_ptr = ib_res.ib_buf;

    char *start_addr_base = buf_ptr;
    char *start_addr = start_addr_base;
    char *end_addr = start_addr_base + (config_info.msg_size * config_info.batch_size) - 1;

    struct ibv_send_wr *bad_wr;
    struct ibv_send_wr *send_wr = ib_res.send_wrs;

    CPU_ZERO(&cpu_set);
    CPU_SET((long)args, &cpu_set);
    pthread_t self = pthread_self();
    ret = pthread_setaffinity_np(self, sizeof(cpu_set_t), &cpu_set);
    assert(ret == 0 && "Could not set thread affinity");

    struct ibv_wc *wc = (struct ibv_wc *)calloc(num_wc, sizeof(struct ibv_wc));
    assert(wc != NULL && "Could not allocate receive work completion array");

    struct ibv_qp_attr attr;
    struct ibv_qp_init_attr init_attr;
    ibv_query_qp(ib_res.qp, &attr, IBV_QP_STATE, &init_attr);
    printf("QP state: %d\n", attr.qp_state);

    sleep(1);
    // set up the initial connection
    int send_wr_idx = 0;
    ret = ibv_post_send(ib_res.qp, &ib_res.send_wrs[send_wr_idx], &bad_wr);
    assert(ret == 0 && "Could not post the send");

    puts("Starting client");

    printf("Polling on %p, to %p, sz: %lu\n", start_addr, end_addr,
           (uintptr_t)end_addr - (uintptr_t)start_addr);
    while (true) {
        // manually poll the address to check that the write has been completed
        while ((*(volatile char *)start_addr != 'A') || (*(volatile char *)end_addr != 'A')) {
        }

        puts("Got something!");

        // clear the recv buffer
        memset(start_addr, 0, msg_size * config_info.batch_size);

        // send a message back the other party
        ++message_recv_count;

        buf_offset = (buf_offset + msg_size * config_info.batch_size) % ib_res.ib_buf_size;

        start_addr = start_addr_base + buf_offset;
        end_addr = start_addr + msg_size * config_info.batch_size - 1;
        send_wr_idx = (send_wr_idx + config_info.batch_size) % config_info.num_concurr_msgs;

        printf(
            "New start: %p, New end: %p, New Send WR idx: %d, New write dest: %p, Buf Offset: %d\n",
            start_addr, end_addr, send_wr_idx, ib_res.send_wrs[send_wr_idx].wr.rdma.remote_addr,
            buf_offset);

        int ret = ibv_post_send(ib_res.qp, &ib_res.send_wrs[send_wr_idx], &bad_wr);
        assert(ret == 0 && "Could not post send");

        // still have to poll here to clear the completed entries for
        // the write requests
        int n = ibv_poll_cq(ib_res.cq, num_wc, wc);
        if (n < 0) {
            assert(0 && "failed to poll completion queue");
        }

        for (int i = 0; i < n; i++) {
            if (wc[i].status != IBV_WC_SUCCESS) {
                if (wc[i].opcode == IBV_WC_SEND) {
                    assert(0 && "Failed send");
                } else if (wc[i].opcode == IBV_WC_RECV) {
                    assert(0 && "Recv failed");
                }
            }
        }

        if (message_recv_count == 100) {
            printf("RDMA success! 100 messages exchanged\n");
            pthread_exit((void *)0);
        }
    }
}

int run_client() {
    int ret = 0;
    long num_threads = 1;

    pthread_t *threads = NULL;
    void *status;

    threads = (pthread_t *)calloc(num_threads, sizeof(pthread_t));
    assert(threads != NULL && "Could not allocate pthreads array");

    for (long i = 0; i < num_threads; i++) {
        ret = pthread_create(&threads[i], NULL, client_function, (void *)i);
        assert(ret == 0 && "Could not create thread");
    }

    for (int i = 0; i < num_threads; i++) {
        ret = pthread_join(threads[i], &status);
        if (ret != 0) {
            fprintf(stderr, "Server thread %d had nonzero return status %d\n", i, *((int *)status));
        }
    }

    free(threads);
    return 0;
}