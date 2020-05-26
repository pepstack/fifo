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
 *   named pipe for Windows.
 *
 * @author     Liang Zhang <350137278@qq.com>
 * @version    0.0.17
 * @create     2020-01-22 18:20:46
 * @update     2020-02-20 00:21:46
 */
#include "fifo.h"

#include "memapi.h"
#include "unitypes.h"

#include <time.h>
#include <Windows.h>


#define FIFO_OPEN_MODE  (DWORD)(PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED)
#define FIFO_PIPE_MODE  (DWORD)(PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT)

#define FIFO_NAMELEN_MAX    255


typedef struct
{
    OVERLAPPED oOverlap;
    HANDLE     hPipeInst;

    fifo_pipemsg_t request;
    fifo_pipemsg_t reply;

    fifo_onpipemsg_cb  pipemsgcb;
    void *argument;
} pipe_instance_t;


VOID WINAPI CompletedWriteRoutine (DWORD, DWORD, LPOVERLAPPED);

VOID WINAPI CompletedReadRoutine (DWORD, DWORD, LPOVERLAPPED);


static BOOL ConnectToNewClient (HANDLE hPipe, LPOVERLAPPED lpo)
{
    BOOL fPendingIO = FALSE;

    // Start an overlapped connection for this pipe instance.
    if (ConnectNamedPipe(hPipe, lpo)) {
        // Overlapped ConnectNamedPipe should return zero.
        printf("ConnectNamedPipe failed with %d.\n", GetLastError());
        return 0;
    }

    switch (GetLastError()) {
    case ERROR_IO_PENDING:
        // The overlapped connection in progress.
        fPendingIO = TRUE;
        break;

    case ERROR_PIPE_CONNECTED:
        // Client is already connected, so signal an event.
        if (SetEvent(lpo->hEvent)) {
            break;
        }
    default:
        // If an error occurs during the connect operation
        printf("ConnectNamedPipe failed with %d.\n", GetLastError());
        return 0;
    }

    return fPendingIO;
}

// DisconnectAndClose(LPPIPEINST)
//   This routine is called when an error occurs or the client closes 
//     its handle to the pipe.
VOID DisconnectAndClose (pipe_instance_t *pPipeInst)
{
    // Disconnect the pipe instance.
    if (! DisconnectNamedPipe(pPipeInst->hPipeInst)) {
        printf("DisconnectNamedPipe failed with %d.\n", GetLastError());
    }

    // Close the handle to the pipe instance.
    CloseHandle(pPipeInst->hPipeInst);

    // Release the storage for the pipe instance.
    if (pPipeInst) {
        GlobalFree(pPipeInst);
    }
}


// CompletedWriteRoutine(DWORD, DWORD, LPOVERLAPPED)
//   This routine is called as a completion routine after writing to the pipe,
//     or when a new client has connected to a pipe instance.
//   It starts another read operation.
VOID WINAPI CompletedWriteRoutine (DWORD dwErr, DWORD cbWritten, LPOVERLAPPED lpOverLap)
{
    BOOL fRead = FALSE;

    // lpOverlap points to storage for this instance.
    pipe_instance_t *pPipeInst = (pipe_instance_t *)lpOverLap;

    // The write operation has finished, so read the next request (if there is no error).
    if (dwErr == 0) {
        DWORD cbToWrite = (pPipeInst->reply.msgsz > 0? (sizeof(int32_t) + pPipeInst->reply.msgsz) : 0);

        if (cbWritten == cbToWrite) {
            fRead = ReadFileEx(pPipeInst->hPipeInst, &pPipeInst->request, sizeof(pPipeInst->request),
                        (LPOVERLAPPED)pPipeInst, (LPOVERLAPPED_COMPLETION_ROUTINE) CompletedReadRoutine);
        }
    }

    // Disconnect if an error occurred.
    if (! fRead) {
        DisconnectAndClose(pPipeInst);
    }
}


// CompletedReadRoutine(DWORD, DWORD, LPOVERLAPPED)
// This routine is called as an I/O completion routine after reading 
// a request from the client.It gets data and writes it to the pipe.
VOID WINAPI CompletedReadRoutine (DWORD dwErr, DWORD cbBytesRead, LPOVERLAPPED lpOverLap)
{
    BOOL fWrite = FALSE;
    DWORD cbBytesToWtite = 0;

    // lpOverlap points to storage for this instance.
    pipe_instance_t * pPipeInst = (pipe_instance_t *)lpOverLap;

    // The read operation has finished, so write a response (if no error occurred).
    if ((dwErr == 0) && (cbBytesRead != 0)) {        
        pPipeInst->reply.msgsz = 0;

        pPipeInst->pipemsgcb(&pPipeInst->request, &pPipeInst->reply, pPipeInst->argument);

        if (pPipeInst->reply.msgsz > 0) {
            if (pPipeInst->reply.msgsz > sizeof(pPipeInst->reply.msgbuf)) {
                pPipeInst->reply.msgsz = (int) sizeof(pPipeInst->reply.msgbuf);
            }

            cbBytesToWtite = pPipeInst->reply.msgsz + sizeof(int32_t);
        }

        // Always write reply to pipe even if (outputlen = 0)
        fWrite = WriteFileEx(pPipeInst->hPipeInst,
                    &pPipeInst->reply, cbBytesToWtite, 
                    (LPOVERLAPPED)pPipeInst, 
                    (LPOVERLAPPED_COMPLETION_ROUTINE) CompletedWriteRoutine);
    }

    // Disconnect if an error occurred.
    if (! fWrite) {
        DisconnectAndClose(pPipeInst);
    }
}


typedef struct _fifo_server_t
{
    HANDLE hConnectEvent;

    OVERLAPPED oConnect;
    pipe_instance_t *pPipeInst;

    // config settings
    int client_timeout;
    int connect_timeout;

    // The entire pipe name string can be up to 256 characters long.
    // Pipe names are not case sensitive.
    int namelen;
    char pipename[0];
} fifo_server_t;


typedef struct _fifo_client_t
{
    HANDLE hNamedPipe;

    int wait_timeout;

    int namelen;
    char pipename[0];
} fifo_client_t;


int fifo_server_new (const char *pathname, int client_timeout, int connect_timeout, fifo_server *server)
{
    fifo_server_t *srvr;
    size_t namelen;
    const char *pipename;

    if (! pathname) {
        pipename = FIFO_NAME_WIN_DEFAULT;
    } else {
        pipename = pathname;
    }

    namelen = strnlen(pipename, FIFO_NAMELEN_MAX - 12);
    srvr = mem_alloc_zero(1, sizeof(*srvr)+ namelen + 1);

    srvr->namelen = (int) namelen;
    memcpy(srvr->pipename, pipename, srvr->namelen);

    // Create one event object for the connect operation
    srvr->hConnectEvent = CreateEvent(NULL, TRUE, TRUE, NULL);
    if (! srvr->hConnectEvent){
        printf("CreateEvent failed with %d.\n", GetLastError());
        mem_free(srvr);
        return FIFO_E_FAILED;
    }

    srvr->oConnect.hEvent = srvr->hConnectEvent;

    srvr->client_timeout = client_timeout;
    srvr->connect_timeout = connect_timeout;

    *server = srvr;
    return FIFO_S_OK;
}


void fifo_server_free (fifo_server server)
{
    CloseHandle(server->hConnectEvent);
    mem_free(server);
}


void fifo_server_runforever (fifo_server server, fifo_onpipemsg_cb pipemsgcb, void *argument, fifo_serverloop_cb servloopcb, void *loopcbarg)
{
    BOOL fPendingIO;
    pipe_instance_t *pPipeInst;

    DWORD cbRet;   // bytes transferred

    HANDLE hNamedPipe = CreateNamedPipeA(server->pipename,
                            FIFO_OPEN_MODE, FIFO_PIPE_MODE,
                            PIPE_UNLIMITED_INSTANCES, // unlimited instances
                            PIPEMSG_SIZE_MAX,         // output buffer size
                            PIPEMSG_SIZE_MAX,         // input buffer size
                            server->client_timeout,   // client timeout in milliseconds
                            NULL);                    // default security attributes

    if (hNamedPipe == INVALID_HANDLE_VALUE) {
        printf("CreateNamedPipe failed with %d.\n", GetLastError());
        exit(EXIT_FAILURE);
    }

    // Call a subroutine to connect to the new client.
    fPendingIO = ConnectToNewClient(hNamedPipe, &server->oConnect);

    while(! servloopcb || servloopcb(loopcbarg)) {
        // Wait for a client to connect, or for a read or write  operation to be completed,
        //   which causes a completion routine to be queued for execution.
        DWORD dwWait = WaitForSingleObjectEx(server->hConnectEvent, server->connect_timeout, TRUE);
 
        if (dwWait == WAIT_TIMEOUT) {
            printf(".");
        } else if (dwWait == WAIT_IO_COMPLETION) {
            // The wait is satisfied by a completed read or write operation.
            // This allows the system to execute the completion routine.
            continue;
        } else if (dwWait == 0) {
            // The wait conditions are satisfied by a completed connect operation.
            if (fPendingIO) {
                // If an operation is pending, get the result of the connect operation.
                if (! GetOverlappedResult(hNamedPipe, &server->oConnect, &cbRet, FALSE)) {
                    printf("ConnectNamedPipe failed (%d)\n", GetLastError());
                    exit(EXIT_FAILURE);
                }
            }

            // Allocate storage for this instance.
            pPipeInst = (pipe_instance_t *)GlobalAlloc(GPTR, sizeof(pipe_instance_t));
            if (pPipeInst == NULL) {
                printf("GlobalAlloc failed (%d)\n", GetLastError());
                exit(EXIT_FAILURE);
            }

            pPipeInst->hPipeInst = hNamedPipe;

            pPipeInst->request.msgsz = 0;
            pPipeInst->reply.msgsz = 0;

            pPipeInst->argument = argument;
            pPipeInst->pipemsgcb = pipemsgcb;

            // Start the read operation for this client.
            // Note that this same routine is later used as a completion routine after a write operation.
            CompletedWriteRoutine(0, 0, (LPOVERLAPPED)pPipeInst);

            // Create new pipe instance for the next client.
            hNamedPipe = CreateNamedPipeA(server->pipename,
                            FIFO_OPEN_MODE, FIFO_PIPE_MODE,
                            PIPE_UNLIMITED_INSTANCES, // unlimited instances
                            PIPEMSG_SIZE_MAX,         // output buffer size
                            PIPEMSG_SIZE_MAX,         // input buffer size
                            server->client_timeout,   // client timeout in milliseconds
                            NULL);                    // default security attributes

            if (hNamedPipe == INVALID_HANDLE_VALUE) {
                printf("CreateNamedPipe failed with %d.\n", GetLastError());
                exit(EXIT_FAILURE);
            }

            fPendingIO = ConnectToNewClient(hNamedPipe, &server->oConnect);
        } else {
            // An error occurred in the wait function.
            printf("WaitForSingleObjectEx (%d)\n", GetLastError());
            exit(EXIT_FAILURE);
        }
    }
}


const char * fifo_server_get_pipename (fifo_server server)
{
    return (server? server->pipename : FIFO_NAME_WIN_DEFAULT);
}


int fifo_client_new (const char *pathname, int wait_timeout, fifo_client *client)
{
    BOOL fWaitSuccess;
    HANDLE hNamedPipe;

    int namelen;
    const char *pipename;
    fifo_client_t *clnt;

    DWORD dwMode;

    pipename = pathname? pathname : FIFO_NAME_WIN_DEFAULT;
    namelen = (int) strnlen(pipename, FIFO_NAMELEN_MAX + 1);
    if (namelen > FIFO_NAMELEN_MAX) {
        printf("Invalid pipe name: %s.\n", pipename);
        return FIFO_E_FAILED;
    }

    while (1) {
        hNamedPipe = CreateFile(
            pipename,
            GENERIC_READ | GENERIC_WRITE, // read and write access 
            0,              // no sharing 
            NULL,           // default security attributes
            OPEN_EXISTING,  // opens existing pipe 
            0,              // default attributes 
            NULL);// no template file 
 
        // Break if the pipe handle is valid.
        if (hNamedPipe != INVALID_HANDLE_VALUE) {    
            break;
        }

        // Exit if an error other than ERROR_PIPE_BUSY occurs.
        if (GetLastError()!= ERROR_PIPE_BUSY) {
            printf("CreateFile error(%d): %.*s\n", GetLastError(), namelen, pipename);
            return FIFO_E_FAILED;
        }

        // All pipe instances are busy, so wait for wait_timeout milliseconds.
        if (wait_timeout < 0) {
            fWaitSuccess = WaitNamedPipeA(pipename, NMPWAIT_WAIT_FOREVER);
        } else {
            fWaitSuccess = WaitNamedPipeA(pipename, wait_timeout);
        }

        if (! fWaitSuccess) {
            printf("Could not open pipe: wait timeout.\n");
            return FIFO_E_FAILED;
        }
    }

    // The pipe connected;change to message-read mode.
    dwMode = PIPE_READMODE_MESSAGE;

    if (! SetNamedPipeHandleState(hNamedPipe, &dwMode, NULL, NULL)){
        printf("SetNamedPipeHandleState error: %d\n", GetLastError());
        CloseHandle(hNamedPipe);
        return FIFO_E_FAILED;
    }

    clnt = mem_alloc_zero(1, sizeof(*clnt) + namelen + 1);
    clnt->namelen = namelen;
    memcpy(clnt->pipename, pipename, clnt->namelen);

    clnt->hNamedPipe = hNamedPipe;

    if (wait_timeout < 0) {
        clnt->wait_timeout = FIFO_TIME_INFINITE;
    } else {
        clnt->wait_timeout = wait_timeout;
    }

    *client = clnt;
    return FIFO_S_OK;
}


void fifo_client_free (fifo_client client)
{
    CloseHandle(client->hNamedPipe);
    mem_free(client);
}


int fifo_client_write (fifo_client client, const fifo_pipemsg_t *msg)
{
    BOOL fSuccess;

    DWORD cbOffset = 0;
    DWORD cbWritten = 0;

    DWORD dwBytesToWrite = 0;

    if (msg->msgsz < 0 || msg->msgsz > sizeof(msg->msgbuf)) {
        printf("bad size for msg: msgsz=%d\n", msg->msgsz);
        return FIFO_E_BADARG;
    }

    dwBytesToWrite = (DWORD)(sizeof(int32_t) + msg->msgsz);

    while((fSuccess = WriteFile(client->hNamedPipe, (char*)msg + cbOffset, dwBytesToWrite - cbOffset, &cbWritten, NULL))== TRUE) {
        cbOffset += cbWritten;

        if (cbOffset == (DWORD) dwBytesToWrite) {
            // success
            break;
        }
    }

    if (! fSuccess){
        printf("WriteFile to pipe error: %d\n", GetLastError());
        return FIFO_E_FAILED;
    }

    return FIFO_S_OK;
}


// Read from the pipe. set nNumberOfBytesToRead with bufsize
int fifo_client_read (fifo_client client, fifo_pipemsg_t *msg)
{
    BOOL fSuccess = FALSE;
    DWORD dwNumberOfBytesRead = 0;
    int waitedms = 0;

    do {
        if (client->wait_timeout < 0) {
            // ReadFile blocks until it read requested amount of bytes or error/abort happen.            
            fSuccess = ReadFile(client->hNamedPipe, msg, (DWORD) sizeof(*msg), &dwNumberOfBytesRead, NULL);
        } else if (client->wait_timeout > 0) {
            while (waitedms < client->wait_timeout) {
                fSuccess = PeekNamedPipe(client->hNamedPipe, NULL, 0, NULL, NULL, NULL);
                if (fSuccess) {
                    fSuccess = ReadFile(client->hNamedPipe, msg, (DWORD) sizeof(*msg), &dwNumberOfBytesRead, NULL);
                    break;
                }
                Sleep(10);
                waitedms += 10;
            }
        } else {
            fSuccess = PeekNamedPipe(client->hNamedPipe, NULL, 0, NULL, NULL, NULL);
            if (fSuccess) {
                fSuccess = ReadFile(client->hNamedPipe, msg, (DWORD) sizeof(*msg), &dwNumberOfBytesRead, NULL);
            }
        }

        // If a named pipe is being read in message mode and the next message is longer
        //   than the nNumberOfBytesToRead parameter specifies, ReadFile returns FALSE
        //   and GetLastError() returns ERROR_MORE_DATA.
        // The remainder of the message can be read by a subsequent call to the ReadFile.
        if (! fSuccess && GetLastError() != ERROR_MORE_DATA) {
            break;
        }
    } while (! fSuccess);  // repeat loop if ERROR_MORE_DATA (message was truncated)

    if (! fSuccess) {
        printf("ReadFile from pipe error: %d\n", GetLastError());
        return FIFO_E_FAILED;
    }

    // If the lpNumberOfBytesRead parameter is zero when ReadFile returns TRUE on a pipe,
    //   the other end of the pipe called the WriteFile function with nNumberOfBytesToWrite
    //   set to zero.
    if (dwNumberOfBytesRead == 0) {
        msg->msgsz = 0;
    } else if (dwNumberOfBytesRead >= (DWORD)sizeof(int32_t)) {
        msg->msgsz = (int) dwNumberOfBytesRead - (int)sizeof(int32_t);
    } else {
        printf("fatal application error.\n");
        exit(EXIT_FAILURE);
    }

    return FIFO_S_OK;
}


const char * fifo_client_get_pipename (fifo_client client)
{
    return (client? client->pipename : FIFO_NAME_WIN_DEFAULT);
}
