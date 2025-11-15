#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <linux/input.h>
#include <getopt.h>
#include <pthread.h>

#include "window_monitor.h"

void print_usage(const char *program_name) {
  printf("Usage: %s [OPTIONS]\n", program_name);
  printf("Options:\n");
  printf("  -d, --device <path>     HID device path (default: /dev/input/event15)\n");
  printf("  -p, --port <port>       UDP port (default: 5555)\n");
  printf("  -i, --ip <address>      Pi Zero IP address (default: 192.168.1.102)\n");
  printf("  -w, --window <name>     Target window name (optional)\n");
  printf("  -s, --scale <float>     Sensitivity scale (default: 1.0)\n");
  printf("  -h, --help              Show this help message\n");
}

static inline int clamp_int(int v, int lo, int hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

int main(int argc, char *argv[]) {

  char *device = "/dev/input/event15";
  int UDP_PORT = 5555;
  char *PI_ZERO_IP = "192.168.1.102";
  char *target_window = NULL;
  double scale = 1.0;

  static struct option long_options[] = {
    {"device", required_argument, 0, 'd'},
    {"port",   required_argument, 0, 'p'},
    {"ip",     required_argument, 0, 'i'},
    {"window", required_argument, 0, 'w'},
    {"scale",  required_argument, 0, 's'},
    {"help",   no_argument,       0, 'h'},
    {0, 0, 0, 0}
  };

  int opt;
  int option_index = 0;

  while ((opt = getopt_long(argc, argv, "d:p:i:w:s:h", long_options, &option_index)) != -1) {
    switch (opt) {
      case 'd':
        device = optarg;
        break;
      case 'p':
        UDP_PORT = atoi(optarg);
        break;
      case 'i':
        PI_ZERO_IP = optarg;
        break;
      case 'w':
        target_window = optarg;
        break;
      case 's':
        scale = atof(optarg);
        if (scale <= 0.01) scale = 0.01;
        if (scale > 32.0) scale = 32.0;
        break;
      case 'h':
        print_usage(argv[0]);
        return 0;
      default:
        print_usage(argv[0]);
        return 1;
    }
  }

  struct sockaddr_in pi_addr;
  struct input_event ev;
  unsigned char buttons = 0;
  int accum_dx = 0;
  int accum_dy = 0;
  char report[3] = {0, 0, 0};

  // Open HID device (mouse)
  int mouse_fd = open(device, O_RDONLY);
  if (mouse_fd < 0) {
    perror("Failed to get mouse");
    return 1;
  }
  printf("Opened mouse: %d\n", mouse_fd);

  // Create UDP socket
  int sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock_fd < 0) {
    perror("Failed to get socket");
    close(mouse_fd);
    return 1;
  }
  printf("Opened socket: %d\n", sock_fd);

  // Setup Pi Zero address
  memset(&pi_addr, 0, sizeof(pi_addr));
  pi_addr.sin_family = AF_INET;
  pi_addr.sin_port = htons(UDP_PORT);
  if (inet_pton(AF_INET, PI_ZERO_IP, &pi_addr.sin_addr) <= 0) {
    perror("Invalid IP address");
    close(sock_fd);
    close(mouse_fd);
    return 1;
  }

  printf("Forwarding to %s:%d\n", PI_ZERO_IP, UDP_PORT);

  // Start window monitoring thread
  if (target_window) {
    if (start_window_monitor(target_window) != 0) {
      close(sock_fd);
      close(mouse_fd);
      return 1;
    }
  }

  while (read(mouse_fd, &ev, sizeof(ev)) == sizeof(ev)) {
    if (target_window && !should_forward_events()) {
      continue;
    }

    if (ev.type == EV_KEY) {
      if (ev.code == BTN_LEFT) {
        buttons = ev.value ? (buttons | 1) : (buttons & ~1);
      } else if (ev.code == BTN_RIGHT) {
        buttons = ev.value ? (buttons | 2) : (buttons & ~2);
      } else if (ev.code == BTN_MIDDLE) {
        buttons = ev.value ? (buttons | 4) : (buttons & ~4);
      } else if (ev.code == BTN_SIDE || ev.code == BTN_BACK) {
        // Map side/back to HID Button 4 (bit 3)
        buttons = ev.value ? (buttons | 8) : (buttons & ~8);
      } else if (ev.code == BTN_EXTRA || ev.code == BTN_FORWARD) {
        // Map extra/forward to HID Button 5 (bit 4)
        buttons = ev.value ? (buttons | 16) : (buttons & ~16);
      }
      // Send immediate button state change with zero movement for click responsiveness
      report[0] = (char)buttons;
      report[1] = 0;
      report[2] = 0;
      sendto(sock_fd, report, 3, 0, (struct sockaddr*)&pi_addr, sizeof(pi_addr));
    }
    else if (ev.type == EV_REL) {
      if (ev.code == REL_X) {
        accum_dx += ev.value;
      } else if (ev.code == REL_Y) {
        accum_dy += ev.value;
      }
    }
    else if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
      // Scale, round and chunk accumulated movement; send combined X+Y per report
      double vx = accum_dx * scale;
      double vy = accum_dy * scale;
      int total_x = (int)(vx >= 0 ? vx + 0.5 : vx - 0.5);
      int total_y = (int)(vy >= 0 ? vy + 0.5 : vy - 0.5);
      while (total_x != 0 || total_y != 0) {
        int chunk_x = clamp_int(total_x, -127, 127);
        int chunk_y = clamp_int(total_y, -127, 127);
        report[0] = (char)buttons;
        report[1] = (char)chunk_x;
        report[2] = (char)chunk_y;
        sendto(sock_fd, report, 3, 0, (struct sockaddr*)&pi_addr, sizeof(pi_addr));
        total_x -= chunk_x;
        total_y -= chunk_y;
      }
      accum_dx = 0;
      accum_dy = 0;
    }
  }

  return 0;
}