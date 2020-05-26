/***********************************************************************
 * Copyright (c) 2008-2080 pepstack.com, 350137278@qq.com
 *
 * ALL RIGHTS RESERVED.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 **********************************************************************/

/**
 * @filename   fifo.h
 *    named pipe for Windows and Linux.
 *
 * @author     Liang Zhang <350137278@qq.com>
 * @version    0.0.1
 * @create     2020-01-22 18:20:46
 * @update     2020-02-09 00:21:46
 */
#ifndef _FIFO_H_
#define _FIFO_H_

#if defined(__cplusplus)
extern "C"
{
#endif

#include <stdio.h>
#include <stdint.h>

/**
 * fifo result
 */
#define FIFO_S_OK         0
#define FIFO_E_FAILED   (-1)
#define FIFO_E_BADARG   (-2)
#define FIFO_E_TIMEOUT  (-3)

/**
 * fifo timeout
 */
#define FIFO_TIME_INFINITE   (-1)
#define FIFO_TIME_NOWAIT       0


/**
 * fifo pipe names sample
 */
#ifndef FIFO_NAME_WIN_DEFAULT
    # define FIFO_NAME_WIN_DEFAULT     "\\\\.\\pipe\\namedpipe-default"
#endif

#ifndef FIFO_NAME_LINUX_DEFAULT
    # define FIFO_NAME_LINUX_DEFAULT   "/tmp/namedpipe-default"
#endif


/**
 * fifo timeout in milliseconds
 */

// client timeout
#ifndef FIFO_TIMEOUT
    # define FIFO_TIMEOUT    6000
#endif

// connect timeout
#ifndef FIFO_CONNECT_TIMEOUT
    # define FIFO_CONNECT_TIMEOUT  FIFO_TIMEOUT
#endif


/**
 * fifo size for atomic pipe message buffer
 */
#ifdef PIPE_BUF
    # define PIPEMSG_SIZE_MAX    PIPE_BUF
#else
    # define PIPEMSG_SIZE_MAX    4096
#endif


/**
 * fifo atomic pipe message buffer
 */
typedef struct
{
    // bytes size of msg
    int32_t msgsz;

    // msg body with max size up to: PIPEMSG_SIZE_MAX - 4
    char msgbuf[PIPEMSG_SIZE_MAX - sizeof(int32_t)];
} fifo_pipemsg_t;


typedef struct _fifo_server_t * fifo_server;
typedef struct _fifo_client_t * fifo_client;

typedef void (*fifo_onpipemsg_cb)(const fifo_pipemsg_t *request, fifo_pipemsg_t *reply, void *argument);

typedef int (*fifo_serverloop_cb)(void *argument);


/**
 * fifo_onpipemsg_cb sample
 */
static void fifoReplyForRequest (const fifo_pipemsg_t *request, fifo_pipemsg_t *reply, void *argument)
{
    printf("request={%.*s}\n", request->msgsz, request->msgbuf);

    reply->msgsz = snprintf(reply->msgbuf, sizeof(reply->msgbuf), "Answer from server");
}


/**
 * fifo api
 *   https://linux.die.net/man/7/pipe
 *   https://blog.csdn.net/ljianhui/article/details/10202699
 *   https://docs.microsoft.com/en-us/windows/win32/ipc/multithreaded-pipe-server
 *   https://docs.microsoft.com/en-us/windows/win32/ipc/named-pipe-client
 */
const char * fifo_server_get_pipename (fifo_server server);
int fifo_server_new (const char *pipename, int client_timeout, int connect_timeout, fifo_server *server);
void fifo_server_runforever (fifo_server server, fifo_onpipemsg_cb pipemsgcb, void *msgcbarg, fifo_serverloop_cb servloopcb, void *loopcbarg);
void fifo_server_free (fifo_server server);

const char * fifo_client_get_pipename (fifo_client client);
int fifo_client_new (const char *pipename, int wait_timeout, fifo_client *client);
void fifo_client_free (fifo_client client);
int fifo_client_write (fifo_client client, const fifo_pipemsg_t *msg);
int fifo_client_read (fifo_client client, fifo_pipemsg_t *msg);

#ifdef __cplusplus
}
#endif
#endif /* _FIFO_H_ */
