#define _GNU_SOURCE

#include "../include/server.h"
#include "../include/config.h"
#include "../include/ib.h"
#include <assert.h>
#include <bits/pthreadtypes.h>
#include <infiniband/verbs.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>

void *server_thread(void *args) {
    // usually, you want to pin the polling thread to the CPU core
    // this helps keep the cache hot with the polling thread's context
    // and also minimizes scheduling latency that would otherwise appear on the core

    cpu_set_t cpu_set;
    int ret = 0;
    int num_wc = 20;
    int num_concurrent_messages = config_info.num_concurr_msgs;
    int msg_size = config_info.msg_size;
    int buf_offset = 0;
    char *buf_ptr = ib_res.ib_buf;

    char *start_addr = ib_res.ib_buf;
    char *start_addr_base = ib_res.ib_buf;
    char *end_addr = start_addr + msg_size - 1;
    char *raddr = (char *)ib_res.raddr_base;

    CPU_ZERO(&cpu_set);
    CPU_SET((long)args, &cpu_set);
    pthread_t self = pthread_self();
    ret = pthread_setaffinity_np(self, sizeof(cpu_set), &cpu_set);
    assert(ret == 0 && "Cannot pin the current thread to the CPU");

    // pre post the recvs
    struct ibv_wc *wc = (struct ibv_wc *)calloc(num_wc, sizeof(struct ibv_wc));
    assert(wc != NULL && "Could not allocate receive work completion array");

    struct ibv_qp_attr attr;
    struct ibv_qp_init_attr init_attr;
    ibv_query_qp(ib_res.qp, &attr, IBV_QP_STATE, &init_attr);
    printf("QP state: %d\n", attr.qp_state);

    // prepost the first send to kickstart things
    ret =
        post_write(msg_size, ib_res.mr->lkey, (uintptr_t)start_addr, ib_res.qp,
                   start_addr + ib_res.ib_buf_size, // write buffer is at the end of the recvbuffer
                   (uintptr_t)raddr, ib_res.rkey, true);
    assert(ret == 0 && "Could not post the first write");
    sleep(1);
    struct ibv_wc wc2;
    int n = ibv_poll_cq(ib_res.cq, 1, &wc2);
    if (n == 1 && wc2.status != IBV_WC_SUCCESS) {
        printf("Server write failed: %s\n", ibv_wc_status_str(wc2.status));
    }

    puts("Starting server");
    printf("Polling on %p, to %p, sz: %lu\n", start_addr, end_addr,
           (uintptr_t)end_addr - (uintptr_t)start_addr);

    int message_recv_count = 0;
    while (true) {
        while ((*(volatile char *)start_addr != 'A') || (*(volatile char *)end_addr != 'A')) {
            // printf("%c %c\n", *start_addr, *end_addr);
        }

        ++message_recv_count;
        memset(start_addr, 0, msg_size);
        
                    buf_offset = (buf_offset + msg_size) % ib_res.ib_buf_size;
                    raddr = (char *)ib_res.raddr_base + buf_offset;
                    start_addr = start_addr_base + buf_offset;
                    end_addr = start_addr + msg_size - 1;

        post_write(msg_size, ib_res.mr->lkey, (uintptr_t)start_addr, ib_res.qp,
                   ib_res.ib_buf + ib_res.ib_buf_size, (uintptr_t)raddr, ib_res.rkey, true);
    

        int n = ibv_poll_cq(ib_res.cq, num_wc, wc);
        if (n < 0) {
            assert(0 && "failed to poll completion queue");
        }

        for (int i = 0; i < n; i++) {
            if (wc[i].status != IBV_WC_SUCCESS) {
                printf("Failure reason: %s\n", ibv_wc_status_str(wc[i].status));

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

int run_server() {
    int ret = 0;
    long num_threads = 1;

    pthread_t *threads = NULL;
    void *status;

    threads = (pthread_t *)calloc(num_threads, sizeof(pthread_t));
    assert(threads != NULL && "Could not allocate pthreads array");

    for (long i = 0; i < num_threads; i++) {
        ret = pthread_create(&threads[i], NULL, server_thread, (void *)i);
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