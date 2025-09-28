#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>

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
  //inet_pton(AF_INET, "10.0.2.15", &server.sin_addr);  // Because we're using port forwarding

  char ipstr[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &server.sin_addr, ipstr, sizeof(ipstr));
  printf("DEBUG: Attempting to connect to server at %s:%d\n", ipstr, ntohs(server.sin_port));

  if (connect(sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
    perror("connect failed");
    return 1;
  }

  // After connect, print the address and port actually connected to
  struct sockaddr_in actual_addr;
  socklen_t actual_len = sizeof(actual_addr);
  if (getpeername(sock, (struct sockaddr *)&actual_addr, &actual_len) == 0) {
    char actual_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &actual_addr.sin_addr, actual_ip, sizeof(actual_ip));
    printf("DEBUG: Connected to server at %s:%d\n", actual_ip, ntohs(actual_addr.sin_port));
  } else {
    printf("DEBUG: Connected to server (could not resolve address)\n");
  }

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