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
    char *end_addr_base = buf_ptr + msg_size - 1;
    char *start_addr = start_addr_base;
    char *end_addr = end_addr_base;
    char *raddr = (char *)ib_res.raddr_base;

    CPU_ZERO(&cpu_set);
    CPU_SET((long)args, &cpu_set);
    pthread_t self = pthread_self();
    ret = pthread_setaffinity_np(self, sizeof(cpu_set_t), &cpu_set);
    assert(ret == 0 && "Could not set thread affinity");

    struct ibv_wc *wc = (struct ibv_wc *)calloc(num_wc, sizeof(struct ibv_wc));
    assert(wc != NULL && "Could not allocate receive work completion array");

    // for (int i = 0; i < num_concurrent_messages; i++) {
    //     ret = post_recv(msg_size, ib_res.mr->lkey, (uint64_t)buf_ptr, ib_res.qp, buf_ptr);
    //     assert(ret == 0 && "Thread failed to post recv");
    //     buf_offset = (buf_offset + msg_size) % ib_res.ib_buf_size;
    //     buf_ptr = ib_res.ib_buf + buf_offset;
    // }

    struct ibv_qp_attr attr;
    struct ibv_qp_init_attr init_attr;
    ibv_query_qp(ib_res.qp, &attr, IBV_QP_STATE, &init_attr);
    printf("QP state: %d\n", attr.qp_state);

    sleep(1);
    // set up the initial connection
    ret =
        post_write(msg_size, ib_res.mr->lkey, (uintptr_t)start_addr, ib_res.qp,
                   start_addr + ib_res.ib_buf_size, // write buffer is at the end of the recvbuffer
                   (uintptr_t)raddr, ib_res.rkey, true);
    assert(ret == 0 && "Could not post write");
    sleep(1);
    struct ibv_wc wc2;
    int n = ibv_poll_cq(ib_res.cq, 1, &wc2);
    if (n == 1 && wc2.status != IBV_WC_SUCCESS) {
        printf("Client write failed: %s\n", ibv_wc_status_str(wc2.status));
    }
    puts("Starting client");

    // buf_offset = 0;
    printf("Polling on %p, to %p, sz: %lu\n", start_addr, end_addr,
           (uintptr_t)end_addr - (uintptr_t)start_addr);
    while (true) {
        // manually poll the address to check that the write has been completed
        while ((*(volatile char *)start_addr != 'A') || (*(volatile char *)end_addr != 'A')){
        }

        // clear the recv buffer
        memset(start_addr, 0, msg_size);

        // send a message back the other party
        ++message_recv_count;

        buf_offset = (buf_offset + msg_size) % ib_res.ib_buf_size;
            raddr = (char *)ib_res.raddr_base + buf_offset;
            start_addr = start_addr_base + buf_offset;
            end_addr = start_addr + msg_size - 1;

        post_write(msg_size, ib_res.mr->lkey, (uintptr_t)start_addr, ib_res.qp,
                   ib_res.ib_buf + ib_res.ib_buf_size, // write buffer is at the end of the recv buffer
                   (uintptr_t)raddr, ib_res.rkey, message_recv_count % 20 == 0);

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