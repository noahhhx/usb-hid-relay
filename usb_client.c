#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define UDP_PORT 5555
#define HID_DEVICE "/dev/hidg0"

int main() {
  int sock_fd, hid_fd;
  struct sockaddr_in server_addr, client_addr;
  socklen_t addr_len = sizeof(client_addr);
  char report[3];

  // Open HID device
  // TODO - uncomment
  // hid_fd = open(HID_DEVICE, O_WRONLY | O_NONBLOCK);
  // if (hid_fd < 0) {
  //   perror("Failed to open HID device");
  //   return 1;
  // }
  // printf("Opened HID device: %s\n", HID_DEVICE);

  // Create UDP socket
  sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock_fd < 0) {
    perror("Failed to create socket");
    close(hid_fd);
    return 1;
  }

  // Bind socket
  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(UDP_PORT);

  if (bind(sock_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
    perror("Failed to bind socket");
    close(sock_fd);
    close(hid_fd);
    return 1;
  }

  printf("Listening on UDP port %d...\n", UDP_PORT);

  // Main loop
  while (1) {
    ssize_t n = recvfrom(sock_fd, report, 3, 0,
                        (struct sockaddr*)&client_addr, &addr_len);

    if (n == 3) {
      printf("Received from UDP");
      // Write to HID device immediately
      // TODO - uncomment
      // if (write(hid_fd, report, 3) != 3) {
      //   perror("Failed to write to HID");
      // }
    }
  }

  close(sock_fd);
  close(hid_fd);
  return 0;
}