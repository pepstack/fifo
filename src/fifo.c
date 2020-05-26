/***********************************************************************
 * Copyright (c)2008-2080 pepstack.com, 350137278@qq.com
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
 * A PARTICULAR PURPOSE ARE DISCLAIMED.IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;LOSS OF USE,
 * DATA, OR PROFITS;OR BUSINESS INTERRUPTION)HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE)ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 **********************************************************************/

/**
 * @filename   fifo.c
 *   named pipe for Linux.
 *
 * refer:
 *   https://www.cnblogs.com/sylz/p/6022362.html
 *
 * @author     Liang Zhang <350137278@qq.com>
 * @version    0.0.17
 * @create     2020-01-22 18:20:46
 * @update     2020-05-20 00:21:46
 */
#include "fifo.h"

#include "memapi.h"
#include "unitypes.h"

#include <fcntl.h>
#include <pthread.h>


#define FIFO_NAMELEN_MAX    255
#define FIFO_FILE_MODE      (S_IWUSR|S_IRUSR|S_IRGRP|S_IROTH)

typedef struct _fifo_server_t
{
    // O_NONBLOCK|O_RDWR
    int accept_pipefd;

    // config settings
    struct timeval client_timeout;
    struct timeval connect_timeout;

    // The entire pipe name string can be up to 256 characters long.
    // Pipe names are not case sensitive.
    int namelen;
    char pipename[0];
} fifo_server_t;


typedef struct _fifo_client_t
{
    int readfd;
    int writefd;

    struct timeval wait_timeout;

    int namelen;
    char pipename[0];
} fifo_client_t;


typedef struct
{
    int requestfd;
    int replyfd;

    struct timeval timeout;

    fifo_pipemsg_t request;
    fifo_pipemsg_t reply;

    fifo_onpipemsg_cb pipemsgcb;
    void *argument;
} pipe_instance_t;


static pipe_instance_t * pipe_instance_new (int requestfd, int replyfd, fifo_onpipemsg_cb onmsgcb, void *cbarg, fifo_server server)
{
    pipe_instance_t *pipeinst = mem_alloc_zero(1, sizeof(*pipeinst));

    pipeinst->requestfd = requestfd;
    pipeinst->replyfd = replyfd;

    pipeinst->pipemsgcb = onmsgcb;
    pipeinst->argument = cbarg;

    pipeinst->timeout.tv_sec = server->client_timeout.tv_sec;
    pipeinst->timeout.tv_usec = server->client_timeout.tv_usec;

    return pipeinst;
}


static void pipe_instance_free (pipe_instance_t *pipeinst)
{
    close(pipeinst->requestfd);
    close(pipeinst->replyfd);
    mem_free(pipeinst);
}


static int readpipemsg_nb (int fd, fifo_pipemsg_t *msg)
{
    ssize_t count, msgsize, offset = 0;

    while (offset == 0) {
        offset = read(fd, (char *)msg, sizeof(msg->msgsz));

        if (offset == sizeof(msg->msgsz)) {
            break;
        }

        if (offset == -1 && errno == EAGAIN) {
            continue;
        }

        // error: read end of fd
        return (-1);
    }

    if (msg->msgsz == 0) {
        // success: reach the end of pipe msg
        return 0;
    }

    msgsize = offset + msg->msgsz;

    while ( offset < msgsize ) {
        count = read(fd, (char *)msg + offset, msgsize - offset);

        if (count > 0) {
            offset += count;

            if (offset == msgsize) {
                // success: reach the end of pipe msg
                return 0;
            }

            // read next bytes
            continue;
        }

        if (count == -1 && errno == EAGAIN) {
            printf("*");
            continue;
        }

        // error read pipe
        break;
    }

    // unexpect read pipe error
    return (-1);
}


static void * client_fifo_worker (void *arg)       
{
    int rc;
    fd_set rfds;

    pipe_instance_t *pipeinst = (pipe_instance_t *) arg;

    printf("client_fifo_worker(accept_pipefd=%d) start...\n", pipeinst->requestfd);

    while(1) {
        FD_ZERO(&rfds);
        FD_SET(pipeinst->requestfd, &rfds);

        if (pipeinst->timeout.tv_sec < 0) {
            rc = select(pipeinst->requestfd + 1, &rfds, NULL, NULL, NULL);
        } else {
            rc = select(pipeinst->requestfd + 1, &rfds, NULL, NULL, &pipeinst->timeout);
        }

        if (rc == 1) {
            if (readpipemsg_nb(pipeinst->requestfd, &pipeinst->request) == 0) {
                if (pipeinst->request.msgsz == 0) {
                    printf("client closed.\n");
                    break;
                }

                pipeinst->reply.msgsz = 0;

                pipeinst->pipemsgcb(&pipeinst->request, &pipeinst->reply, pipeinst->argument);

                if (pipeinst->reply.msgsz > 0) {
                    size_t cbwrite = sizeof(int32_t) + pipeinst->reply.msgsz;
                    if (cbwrite > sizeof(pipeinst->reply)) {
                        cbwrite = sizeof(pipeinst->reply);
                        pipeinst->reply.msgsz = cbwrite - sizeof(int32_t);
                    }

                    if (write(pipeinst->replyfd, &pipeinst->reply, cbwrite) != cbwrite) {
                        printf("write error: %s.\n", strerror(errno));
                        break;
                    }
                }

                continue;
            }
 
            // readpipemsg error
            break;
        } else if (rc == 0) {
            // timeout with no ready
            printf("*");
        } else {
            printf("select failed: %s.\n", strerror(errno));
            break;
        }
    }

    printf("client_fifo_worker(accept_pipefd=%d) exit.\n", pipeinst->requestfd);

    pipe_instance_free(pipeinst);
    return NULL;
}


int fifo_server_new (const char *pathname, int client_timeout, int connect_timeout, fifo_server *server)
{
    fifo_server_t *srvr;
    size_t namelen;
    const char *pipename;

    if (! pathname) {
        pipename = FIFO_NAME_LINUX_DEFAULT;
    } else {
        pipename = pathname;
    }

    namelen = strnlen(pipename, FIFO_NAMELEN_MAX - 20);
    srvr = mem_alloc_zero(1, sizeof(*srvr)+ namelen + 1);

    srvr->namelen = (int) namelen;
    memcpy(srvr->pipename, pipename, srvr->namelen);

    if (mkfifo(srvr->pipename, FIFO_FILE_MODE) < 0 && errno != EEXIST) {
        printf("mkfifo failed: %s.\n", strerror(errno));
        mem_free(srvr);
        return FIFO_E_FAILED;
    }

    printf("open O_NONBLOCK|O_RDWR fifo: %s.\n", srvr->pipename);
    srvr->accept_pipefd = open(srvr->pipename, O_NONBLOCK|O_RDWR);
    if (srvr->accept_pipefd == -1) {
        printf("open failed: %s.\n", strerror(errno));
        fifo_server_free(srvr);
        exit(EXIT_FAILURE);
    }

    if (client_timeout < 0) {
        // wait infinite
        srvr->client_timeout.tv_sec = -1;
    } else {
        srvr->client_timeout.tv_sec = client_timeout / 1000;
        srvr->client_timeout.tv_usec = (client_timeout % 1000) * 1000;
    }

    if (connect_timeout < 0) {
        // wait infinite
        srvr->connect_timeout.tv_sec = -1;
    } else {
        srvr->connect_timeout.tv_sec = connect_timeout / 1000;
        srvr->connect_timeout.tv_usec = (connect_timeout % 1000) * 1000;
    }

    *server = srvr;
    return FIFO_S_OK;
}


void fifo_server_free (fifo_server server)
{
    if (server->accept_pipefd && server->accept_pipefd != -1) {
        close(server->accept_pipefd);
    }

    unlink(server->pipename);
    mem_free(server);
}


const char * fifo_server_get_pipename (fifo_server server)
{
    return (server? server->pipename : FIFO_NAME_LINUX_DEFAULT);
}


void fifo_server_runforever (fifo_server server, fifo_onpipemsg_cb pipemsgcb, void *argument, fifo_serverloop_cb servloopcb, void *loopcbarg)
{
    int client_fifolen;
    char client_fifo[FIFO_NAMELEN_MAX + 1];

    int rc;
    fd_set rfds;

    fifo_pipemsg_t clientmsg;

    snprintf(client_fifo, FIFO_NAMELEN_MAX, "%.*s", server->namelen, server->pipename);
    client_fifo[FIFO_NAMELEN_MAX] = 0;

    // TODO: SIGPIPE

    while(! servloopcb || servloopcb(loopcbarg)) {
        FD_ZERO(&rfds);
        FD_SET(server->accept_pipefd, &rfds);

        if (server->connect_timeout.tv_sec < 0) {
            rc = select((int)server->accept_pipefd + 1, &rfds, NULL, NULL, NULL);
        } else {
            rc = select((int)server->accept_pipefd + 1, &rfds, NULL, NULL, &server->connect_timeout);
        }

        if (rc == 1) {
            if (readpipemsg_nb(server->accept_pipefd, &clientmsg) == 0 && clientmsg.msgsz > 0) {
                pthread_t thread;

                client_fifolen = snprintf(client_fifo + server->namelen, FIFO_NAMELEN_MAX - server->namelen,
                                    "%.*s", (int)clientmsg.msgsz, clientmsg.msgbuf);

                client_fifolen += server->namelen;

                printf("message from client: {%.*s}\n", client_fifolen, client_fifo);

                int requestfd = open(client_fifo, O_NONBLOCK|O_RDWR);
                if (requestfd == -1) {
                    printf("open fifo failed: %s - %s.\n", strerror(errno), client_fifo);
                    continue;
                }

                printf("client connect on pipe: {%.*s}\n", client_fifolen, client_fifo);

                strcat(client_fifo, "-read");
                int replyfd = open(client_fifo, O_WRONLY);
                if (replyfd == -1) {
                    printf("open fifo failed: %s - %s.\n", strerror(errno), client_fifo);
                    close(requestfd);
                    continue;
                }

                pipe_instance_t *pipeinst = pipe_instance_new(requestfd, replyfd, pipemsgcb, argument, server);

                if (pthread_create(&thread, NULL, client_fifo_worker, (void*)pipeinst) == -1) {
                    printf("pthread_create failed.\n");

                    close(requestfd);
                    close(replyfd);

                    pipe_instance_free(pipeinst);
                    break;
                }
            }
        } else if (rc == 0) {
            // timeout with no ready
            printf(".");
        } else {
            printf("select failed: %s.\n", strerror(errno));
            break;
        }
    }

    exit(EXIT_FAILURE);
}


int fifo_client_new (const char *pathname, int wait_timeout, fifo_client *client)
{
    fifo_client_t *clnt;

    int writefd, readfd, namelen, pipelen;

    char client_pipename[FIFO_NAMELEN_MAX + 1];
    const char *pipename;

    fifo_pipemsg_t clientmsg;

    // pid = 12345
    pid_t pid = getpid();

    // pipename = "/tmp/namedpipe-default"
    pipename = pathname? pathname : FIFO_NAME_LINUX_DEFAULT;
    namelen = (int) strnlen(pipename, FIFO_NAMELEN_MAX - 20);
    if (namelen == FIFO_NAMELEN_MAX - 20) {
        printf("Invalid pipe name: %s.\n", pipename);
        return FIFO_E_FAILED;
    }

    // client_pipename = "/tmp/namedpipe-default.12345"
    pipelen = snprintf(client_pipename, FIFO_NAMELEN_MAX, "%.*s.%d", namelen, pipename, pid);

    clnt = mem_alloc_zero(1, sizeof(*clnt) + pipelen + 10);
    clnt->namelen = pipelen;

    // "/tmp/namedpipe-default.12345"
    memcpy(clnt->pipename, client_pipename, clnt->namelen);
    if (mkfifo(clnt->pipename, FIFO_FILE_MODE) < 0 && errno != EEXIST) {
        printf("mkfifo failed: %s - %s.\n", strerror(errno), clnt->pipename);
        mem_free(clnt);
        return FIFO_E_FAILED;
    }

    // client_pipename = "/tmp/namedpipe-default.12345-read"
    snprintf(client_pipename, FIFO_NAMELEN_MAX, "%.*s-read", clnt->namelen, clnt->pipename);
    if (mkfifo(client_pipename, FIFO_FILE_MODE) < 0 && errno != EEXIST) {
        printf("mkfifo failed: %s - %s.\n", strerror(errno), client_pipename);
        unlink(client_pipename);
        mem_free(clnt);
        return FIFO_E_FAILED;
    }
    clnt->readfd = open(client_pipename, O_NONBLOCK|O_RDWR);
    if (clnt->readfd == -1) {
        printf("open failed: %s.\n", strerror(errno));
        fifo_client_free(clnt);
        return FIFO_E_FAILED;
    }

    // O_WRONLY: "/tmp/namedpipe-default"
    writefd = open(pipename, O_WRONLY);
    if (writefd == -1) {
        printf("open failed: %s.\n", strerror(errno));
        fifo_client_free(clnt);
        return FIFO_E_FAILED;
    }

    // .12345
    clientmsg.msgsz = clnt->namelen - namelen + 1;
    memcpy(clientmsg.msgbuf, clnt->pipename + namelen, clientmsg.msgsz);

    pipelen = (int) sizeof(clientmsg.msgsz) + (int) clientmsg.msgsz;

    // connect to server
    if (write(writefd, (char *)&clientmsg, pipelen) == pipelen) {
        close(writefd);

        // block here until server open the pipename
        writefd = open(clnt->pipename, O_WRONLY);
        if (writefd == -1) {
            printf("open failed: %s.\n", strerror(errno));
            fifo_client_free(clnt);
            return FIFO_E_FAILED;
        }

        clnt->writefd = writefd;

        if (wait_timeout < 0) {
            // wait infinite
            clnt->wait_timeout.tv_sec = FIFO_TIME_INFINITE;
        } else {
            clnt->wait_timeout.tv_sec = wait_timeout / 1000;
            clnt->wait_timeout.tv_usec = (wait_timeout % 1000) * 1000;
        }

        *client = clnt;
        return FIFO_S_OK;
    }

    printf("write failed: %s.\n", strerror(errno));
    close(writefd);
    fifo_client_free(clnt);
    return FIFO_E_FAILED;
}


void fifo_client_free (fifo_client client)
{
    if (client->readfd && client->readfd != -1) {
        close(client->readfd);
    }

    if (client->writefd && client->writefd != -1) {
        fifo_pipemsg_t eofmsg = {0};

        fifo_client_write(client, &eofmsg);

        close(client->writefd);
    }

    unlink(client->pipename);

    strcat(client->pipename, "-read");
    unlink(client->pipename);

    mem_free(client);
}


int fifo_client_write (fifo_client client, const fifo_pipemsg_t *msg)
{
    ssize_t cbwrite;

    if (msg->msgsz < 0 || msg->msgsz > sizeof(msg->msgbuf)) {
        printf("bad size for msg: msgsz=%d\n", msg->msgsz);
        return FIFO_E_BADARG;
    }

    cbwrite = sizeof(int32_t) + msg->msgsz;

    if (write(client->writefd, msg, cbwrite) == cbwrite) {
        return FIFO_S_OK;
    }

    return FIFO_E_FAILED;
}


int fifo_client_read (fifo_client client, fifo_pipemsg_t *msg)
{
    int rc;
    fd_set rfds;

    FD_ZERO(&rfds);
    FD_SET(client->readfd, &rfds);

    if (client->wait_timeout.tv_sec < 0) {
        rc = select(client->readfd + 1, &rfds, NULL, NULL, NULL);
    } else {
        rc = select(client->readfd + 1, &rfds, NULL, NULL, &client->wait_timeout);
    }

    if (rc == 1) {
        ssize_t count = read(client->readfd, msg, sizeof(*msg));

        if (count == 0) {
            msg->msgsz = 0;
        } else if (count > 0) {
            msg->msgsz = count - sizeof(msg->msgsz);
        } else {
            printf("Application fatal error.\n");
            exit(EXIT_FAILURE);
        }

        return FIFO_S_OK;
    } else if (rc == 0) {
        return FIFO_E_TIMEOUT;
    }

    return FIFO_E_FAILED;
}


const char * fifo_client_get_pipename (fifo_client client)
{
    return (client? client->pipename : FIFO_NAME_LINUX_DEFAULT);
}