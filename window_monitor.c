#include "window_monitor.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static volatile int window_is_active = 1;
static pthread_mutex_t window_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t monitor_thread;

static void* window_monitor_thread(void *arg) {
  char *target_window = (char *)arg;
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
  return NULL;
}

int start_window_monitor(const char *target_window) {
  printf("Starting window monitor for: %s\n", target_window);
  if (pthread_create(&monitor_thread, NULL, window_monitor_thread, (void *)target_window) != 0) {
    perror("Failed to create monitor thread");
    return -1;
  }
  usleep(150000);
  return 0;
}

int should_forward_events(void) {
  pthread_mutex_lock(&window_mutex);
  int result = window_is_active;
  pthread_mutex_unlock(&window_mutex);
  return result;
}

void stop_window_monitor(void) {
  pthread_cancel(monitor_thread);
  pthread_join(monitor_thread, NULL);
}