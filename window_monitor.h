#ifndef WINDOW_MONITOR_H
#define WINDOW_MONITOR_H

#include <pthread.h>

// Initialize and start the window monitoring thread
int start_window_monitor(const char *target_window);

// Check if events should be forwarded based on window state
int should_forward_events(void);

// Stop the window monitor (optional, for cleanup)
void stop_window_monitor(void);

#endif