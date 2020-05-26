/**
 * @filename   fifoserver.c
 *   Sample server shows how to use fifo api.
 *
 * @author     Liang Zhang <350137278@qq.com>
 * @version    0.0.1
 * @create     2020-01-21 21:09:05
 * @update     2020-01-22 15:09:05
 */
#include "../src/fifo.h"

#include "../src/unitypes.h"

/* using pthread or pthread-w32 */
#include <sched.h>
#include <pthread.h>

#ifdef __WINDOWS__
    # include <Windows.h>

    # include "win32/getoptw.h"

    // link to pthread-w32 lib for MS Windows
    # pragma comment(lib, "pthreadVC2.lib")
#else
    // Linux: see Makefile
    # include <getopt.h>
#endif


#define APPNAME        "fifoserver"
#define APPVER         "0.0.1"

fifo_server server = 0;

static void appexit_cleanup(void)
{
    if (server) {
        fifo_server_free(server);
    }
}


int main (int argc, char *argv[])
{
    WINDOWS_CRTDBG_ON

    /* register on exit cleanup */
    atexit(appexit_cleanup);

    if (fifo_server_new(0, FIFO_TIMEOUT, FIFO_CONNECT_TIMEOUT, &server) == 0) {
        fifo_server_runforever(server, fifoReplyForRequest, 0, 0, 0);
    }

    return 0;
}
