#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

int main() {
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  printf("DEBUG: Socket created with fd: %d\n", sock);
  if (sock < 0) {
    perror("socket");
    return 1;
  }

  struct sockaddr_in server;
  server.sin_family = AF_INET;
  server.sin_port = htons(5000); // Should match lwIP echo server
  inet_pton(AF_INET, "127.0.0.1", &server.sin_addr);  // Because we're using port forwarding
  printf("DEBUG: Server address set to\n");
  if (connect(sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
    perror("connect failed");
    return 1;
  }

  printf("DEBUG: Connected to server\n");
  char *msg = "Hello from host!\n";
  send(sock, msg, strlen(msg), 0);
  printf("DEBUG: Sent message to server\n");
  char buffer[1024] = {0};
    printf("DEBUG: Waiting for response from server\n");
  int len = recv(sock, buffer, sizeof(buffer) - 1, 0);
    printf("DEBUG: Received %d bytes from server\n", len);
  if (len > 0) {
    buffer[len] = 0; // null-terminate
    printf("Received from server: %s\n", buffer);
  } else {
    printf("No data received from server.\n");
  }

  close(sock);
  return 0;
}