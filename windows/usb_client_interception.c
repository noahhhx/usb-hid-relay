#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "interception.h"

#pragma comment(lib, "ws2_32.lib")

#define UDP_PORT 5555

int is_league_active() {
    static DWORD last_check = 0;
    static int cached_result = 0;
    DWORD now = GetTickCount();

    // Check every 500ms to avoid calling Windows API too frequently
    if (now - last_check < 500 && last_check != 0) {
        return cached_result;
    }
    last_check = now;

    HWND hwnd = GetForegroundWindow();
    if (hwnd == NULL) {
        cached_result = 0;
        return 0;
    }

    char title[256];
    // GetWindowTextA is the ANSI version, compatible with char buffers
    if (GetWindowTextA(hwnd, title, sizeof(title)) > 0) {
        // Check if title contains "League of Legends"
        if (strstr(title, "League of Legends") != NULL) {
            cached_result = 1;
            return 1;
        }
    }

    cached_result = 0;
    return 0;
}

int main() {
    WSADATA wsaData;
    SOCKET sock_fd;
    struct sockaddr_in server_addr, client_addr;
    int addr_len = sizeof(client_addr);
    char recv_buf[4];
    char report[4]; 

    WSAStartup(MAKEWORD(2, 2), &wsaData);
    sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(UDP_PORT);
    
    if (bind(sock_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        printf("Bind failed. Error: %d\n", WSAGetLastError());
        return 1;
    }
    printf("Listening on UDP %d via Interception...\n", UDP_PORT);

    InterceptionContext context = interception_create_context();

    InterceptionMouseStroke mstroke;
    memset(&mstroke, 0, sizeof(mstroke));

    int last_buttons = 0; // Track previous state

    // 3. Loop
    while (1) {
        int n = recvfrom(sock_fd, recv_buf, sizeof(recv_buf), 0, (struct sockaddr*)&client_addr, &addr_len);

        if (!is_league_active()) {
            continue;
        }

        // Uncomment for debug
        // printf("Received %d bytes: Buttons=%02x, X=%d, Y=%d\n",
        //        n,
        //        recv_buf[0],
        //        (signed char) recv_buf[1],
        //        (signed char) recv_buf[2]);

        if (n > 0) {
            int dx = 0, dy = 0, buttons = 0;

            if (n == 3) {
                buttons = recv_buf[0];
                dx = (signed char)recv_buf[1];
                dy = (signed char)recv_buf[2];
            } else if (n == 4) {
                buttons = recv_buf[0];
                dx = (signed char)recv_buf[2];
                dy = (signed char)recv_buf[3];
            } else {
                continue;
            }

            // Convert to Interception Flags
            unsigned short state = 0;

            // Left Button
            int left_curr = buttons & 1;
            int left_last = last_buttons & 1;
            if (left_curr && !left_last)       state |= INTERCEPTION_MOUSE_LEFT_BUTTON_DOWN;
            else if (!left_curr && left_last)  state |= INTERCEPTION_MOUSE_LEFT_BUTTON_UP;

            // Right Button
            int right_curr = buttons & 2;
            int right_last = last_buttons & 2;
            if (right_curr && !right_last)      state |= INTERCEPTION_MOUSE_RIGHT_BUTTON_DOWN;
            else if (!right_curr && right_last) state |= INTERCEPTION_MOUSE_RIGHT_BUTTON_UP;

            // Update history
            last_buttons = buttons;

            // Prepare Stroke
            mstroke.flags = INTERCEPTION_MOUSE_MOVE_RELATIVE;
            mstroke.x = dx;
            mstroke.y = dy;
            mstroke.state = state;
            mstroke.rolling = 0;
            mstroke.information = 0;

            // Inject!
            // Uncomment for debug
            //printf("Injecting: state=%04x, dx=%d, dy=%d\n", state, dx, dy);

            // Send to all possible mouse devices because lazy
            for (int i = 0; i < INTERCEPTION_MAX_MOUSE; ++i) {
                interception_send(context, INTERCEPTION_MOUSE(i), (InterceptionStroke *)&mstroke, 1);
            }
        }
    }

    interception_destroy_context(context);
    closesocket(sock_fd);
    WSACleanup();
    return 0;
}