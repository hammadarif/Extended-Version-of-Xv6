#include "kernel/types.h"
#include "kernel/socket.h"
#include "user/user.h"
#include "kernel/param.h"

#define SERVER_PORT 5000
#define htons(x) ((((x)&0xff)<<8) | (((x)>>8)&0xff))
#define ntohs(x) htons(x)

#define htonl(x) ((((x)&0xff)<<24) | (((x)&0xff00)<<8) | (((x)>>8)&0xff00) | (((x)>>24)&0xff))
#define ntohl(x) htonl(x)
int
main(int argc, char *argv[])
{
    int sockfd, connfd;
    struct sockaddr addr;

    // 1. Create socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        printf("server: socket creation failed\n");
        exit(1);
    }
    printf("server: socket created with fd %d\n", sockfd);
    // 2. Bind to port 5000
    addr.sa_family = AF_INET;
    addr.sin_port = htons(SERVER_PORT);
    addr.sin_addr = 0; // INADDR_ANY (0.0.0.0)
    //addr.sin_addr = htonl(0x0a00020f);
    memset(addr.sin_zero, 0, sizeof(addr.sin_zero));

    if (bind(sockfd, &addr, sizeof(addr)) < 0) {
        printf("server: bind failed\n");
        close(sockfd);
        exit(1);
    }

    // 3. Listen
    if (listen(sockfd, 1) < 0) {
        printf("server: listen failed\n");
        close(sockfd);
        exit(1);
    }
    printf("server: Server address is %d\n",ntohl(addr.sin_addr));
    printf("server: listening on port %d\n", ntohs(addr.sin_port));

    // 4. Accept
    int addrlen = sizeof(addr);
    printf("ECHOSERVER: Waiting on accept()...\n");
    connfd = accept(sockfd, &addr, &addrlen);
    printf("ECHOSERVER: accept() returned: %d\n", connfd);
    
    if (connfd < 0) {
        printf("server: accept failed\n");
        close(sockfd);
        exit(1);
    }
    printf("server: accepted connection from IP %x port %d\n",
       addr.sin_addr, ntohs(addr.sin_port));

    // 5. Read from client
    char buf[512];
    int n = read(connfd, buf, sizeof(buf) - 1);
    if (n < 0) {
        printf("server: read error\n");
    } else {
        buf[n] = 0;
        printf("server: received: %s\n", buf);
    }

    // 6. Send reply
    char *reply = "Hello from server";
    write(connfd, reply, strlen(reply));

    // 7. Close connection
    close(connfd);
    close(sockfd);

    printf("server: connection closed\n");
    exit(0);
}
