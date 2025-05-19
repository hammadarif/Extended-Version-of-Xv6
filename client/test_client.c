// test_client.c (on host)
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

int main() {
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in server;
  server.sin_family = AF_INET;
  server.sin_port = htons(5000);
  inet_pton(AF_INET, "10.0.2.15", &server.sin_addr); // IP of guest
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