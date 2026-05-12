#define _GNU_SOURCE

#include "../include/server.h"
#include "../include/config.h"
#include "../include/ib.h"
#include <assert.h>
#include <bits/pthreadtypes.h>
#include <infiniband/verbs.h>
#include <poll.h>
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

    CPU_ZERO(&cpu_set);
    CPU_SET((long)args, &cpu_set);
    pthread_t self = pthread_self();
    ret = pthread_setaffinity_np(self, sizeof(cpu_set), &cpu_set);
    assert(ret == 0 && "Cannot pin the current thread to the CPU");

    // pre post the recvs
    struct ibv_wc *wc = (struct ibv_wc *)calloc(num_wc, sizeof(struct ibv_wc));
    assert(wc != NULL && "Could not allocate receive work completion array");

    for (int i = 0; i < num_concurrent_messages; i++) {
        ret = post_recv(msg_size, ib_res.mr->lkey, (uint64_t)buf_ptr, ib_res.qp, buf_ptr);
        assert(ret == 0 && "Thread failed to post recv");
        buf_offset = (buf_offset + msg_size) % ib_res.ib_buf_size;
        buf_ptr = ib_res.ib_buf + buf_offset;
    }

    ret = post_send(msg_size, ib_res.mr->lkey, 0, MSG_CTL_START, ib_res.qp, buf_ptr);
    assert(ret == 0 && "Failed to signal the client to start");

    int message_recv_count = 0;
    while (true) {
        ret = ibv_req_notify_cq(ib_res.cq, 0);
        assert(ret == 0 && "Could not arm the CQ");

        struct pollfd to_poll[] = {{.fd = ib_res.comp_channel->fd, .events = POLLIN}};

        int n = poll(to_poll, 1, 3000);

        if (n < 0) {
            assert(0 && "failed to poll completion queue");
        } else if (n == 0) {
            printf("Could not find anything in the completion queue...\n");
            continue;
        } else {
            printf("Got a completion queue event!\n");
            ibv_ack_cq_events(ib_res.cq, 1);
        }

        n = ibv_poll_cq(ib_res.cq, 1, wc);
        for (int i = 0; i < n; i++) {
            if (wc[i].status != IBV_WC_SUCCESS) {
                printf("Failure reason: %s\n", ibv_wc_status_str(wc[i].status));

                if (wc[i].opcode == IBV_WC_SEND) {
                    assert(0 && "Failed send");
                } else if (wc[i].opcode == IBV_WC_RECV) {
                    assert(0 && "Recv failed");
                }
            }

            if (wc[i].opcode == IBV_WC_RECV) {
                printf("received a message from the other side\n");
                message_recv_count++;
                char *msg_ptr = (char *)wc[i].wr_id;
                post_send(msg_size, ib_res.mr->lkey, (uintptr_t)msg_ptr, MSG_REGULAR, ib_res.qp,
                          msg_ptr);

                post_recv(msg_size, ib_res.mr->lkey, (uintptr_t)msg_ptr, ib_res.qp, msg_ptr);
            }

            if (message_recv_count == 100) {
                printf("RDMA success! 100 messages exchanged\n");
                pthread_exit((void *)0);
            }
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