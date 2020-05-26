/**
 * @filename   fifoclient.c
 *   Sample client shows how to use fifo api.
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


#define  APPNAME     "fifoclient"
#define  APPVER      "0.0.1"

#define APP_BUFSIZE   512

fifo_client client = 0;


static void appexit_cleanup(void)
{
    if (client) {
        fifo_client_free(client);
    }
}


int main (int argc, char *argv[])
{
    WINDOWS_CRTDBG_ON

    int i = 0, len;

    fifo_pipemsg_t msg;

    /* register on exit cleanup */
    atexit(appexit_cleanup);

    if (fifo_client_new(0, 3000, &client) == 0) {
        while(i++ < 1000) {
            len = snprintf(msg.msgbuf, sizeof(msg.msgbuf), "[%d] hello from client", i);
            msg.msgsz = len;

            if (fifo_client_write(client, &msg) == FIFO_S_OK) {
                printf("sent to server msg:{%.*s}\n", len, msg.msgbuf);

                if (fifo_client_read(client, &msg) == FIFO_S_OK) {
                    printf("recv from server msg: {%.*s}\n", msg.msgsz, msg.msgbuf);
                } else {
                    printf("**** no reply or error ****\n");            
                }
            } else {
                break;
            }
        }
    }

    return 0;
}
