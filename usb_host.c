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

static volatile int window_is_active = 1;
static pthread_mutex_t window_mutex = PTHREAD_MUTEX_INITIALIZER;

void* window_monitor_thread(void *arg) {
  char *target_window = arg;
  char line[256];

  while (1) {
    int found = 0;

    // Run hyprctl to get windows
    FILE *fp = popen("hyprctl activewindow", "r");
    if (fp != NULL) {
      // Parse output and look for window class or title
      while (fgets(line, sizeof(line), fp) != NULL) {
        if (strstr(line, "class:") != NULL && strstr(line, target_window) != NULL) {
          found = 1;
          break;
        }
        if (strstr(line, "title:") != NULL && strstr(line, target_window) != NULL) {
          found = 1;
          break;
        }
      }
      pclose(fp);
    }

    // Update global flag with mutex protection
    pthread_mutex_lock(&window_mutex);
    int previous_state = window_is_active;
    window_is_active = found;
    pthread_mutex_unlock(&window_mutex);

    // TODO - Can remove logging
    if (previous_state != found) {
      if (found) {
        printf("[Window Monitor] Target window '%s' is now ACTIVE\n", target_window);
      } else {
        printf("[Window Monitor] Target window '%s' is now INACTIVE\n", target_window);
      }
    }

    // Check every 100 ms
    usleep(100000);
  }
}

// Function to check if we should forward events
int should_forward_events() {
  pthread_mutex_lock(&window_mutex);
  int result = window_is_active;
  pthread_mutex_unlock(&window_mutex);
  return result;
}

void print_usage(const char *program_name) {
  printf("Usage: %s [OPTIONS]\n", program_name);
  printf("Options:\n");
  printf("  -d, --device <path>     HID device path (default: /dev/input/event16)\n");
  printf("  -p, --port <port>       UDP port (default: 5555)\n");
  printf("  -i, --ip <address>      Pi Zero IP address (default: 192.168.1.100)\n");
  printf("  -w, --window <name>     Target window name (optional)\n");
  printf("  -h, --help              Show this help message\n");
}

int main(int argc, char *argv[]) {

  char *device = "/dev/input/event15";
  int UDP_PORT = 5555;
  char *PI_ZERO_IP = "192.168.1.100";
  char *target_window = NULL;

  static struct option long_options[] = {
    {"device", required_argument, 0, 'd'},
    {"port",   required_argument, 0, 'p'},
    {"ip",     required_argument, 0, 'i'},
    {"window", required_argument, 0, 'w'},
    {"help",   no_argument,       0, 'h'},
    {0, 0, 0, 0}
  };

  int opt;
  int option_index = 0;

  while ((opt = getopt_long(argc, argv, "d:p:i:w:h", long_options, &option_index)) != -1) {
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
  pthread_t monitor_thread;
  if (target_window) {
    printf("Starting window monitor for: %s\n", target_window);
    if (pthread_create(&monitor_thread, NULL, window_monitor_thread, target_window) != 0) {
      perror("Failed to create monitor thread");
      close(sock_fd);
      close(mouse_fd);
      return 1;
    }
    usleep(150000);
  }

  while (read(mouse_fd, &ev, sizeof(ev)) == sizeof(ev)) {
    if (target_window && !should_forward_events()) {
      continue;
    }

    if (ev.type == EV_KEY) {
      if (ev.code == BTN_LEFT) {
        report[0] = ev.value ? (report[0] | 1) : (report[0] & ~1);
      } else if (ev.code == BTN_RIGHT) {
        report[0] = ev.value ? (report[0] | 2) : (report[0] & ~2);
      } else if (ev.code == BTN_MIDDLE) {
        report[0] = ev.value ? (report[0] | 4) : (report[0] & ~4);
      }

      sendto(sock_fd, report, 3, 0,
             (struct sockaddr*)&pi_addr, sizeof(pi_addr));
    }
    else if (ev.type == EV_REL) {
      if (ev.code == REL_X) {
        report[1] = (ev.value > 127) ? 127 :
                   (ev.value < -127) ? -127 : ev.value;
        report[2] = 0;
      } else if (ev.code == REL_Y) {
        report[1] = 0;
        report[2] = (ev.value > 127) ? 127 :
                   (ev.value < -127) ? -127 : ev.value;
      }

      sendto(sock_fd, report, 3, 0,
             (struct sockaddr*)&pi_addr, sizeof(pi_addr));

      report[1] = 0;
      report[2] = 0;
    }
  }

  return 0;
}