// Collector.c Upstream FT2232H (D2XX) -> UDP
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <stdio.h>
#include <stdlib.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include "inc/ftd2xx.h"
// Link with libraries
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "lib/FTD2XX.lib")

#define CHUNK_SIZE 1024
#define DEST_IP "127.0.0.1"
#define DEST_PORT 12345

int main() {
    WSADATA wsaData;
    FT_HANDLE ftHandle;
    FT_STATUS ftStatus;
    SOCKET sock;
    struct sockaddr_in servaddr;
    unsigned char buffer[CHUNK_SIZE];
    DWORD bytesRead;
    // 1. Initialize Winsock
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        printf("Winsock init failed.\n");
        return 1;
    }
    // 2. Setup UDP Socket
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        printf("Socket creation failed: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(DEST_PORT);
    inet_pton(AF_INET, DEST_IP, &servaddr.sin_addr);
    // 3. Setup FTDI (D2XX Style)
    // Open by index 0 (the first FTDI device found)
    ftStatus = FT_OpenEx("USB<->GPS A", FT_OPEN_BY_DESCRIPTION, &ftHandle);
    if (ftStatus != FT_OK) {
        fprintf(stderr, "Can't open FTDI device. Status: %d\n", (int)ftStatus);
        closesocket(sock);
        WSACleanup();
        return 1;
    }
    // Set Latency Timer to 2ms
    FT_SetLatencyTimer(ftHandle, 2);
    // Set USB parameters for higher throughput
    FT_SetUSBParameters(ftHandle, 65536, 65536);
    printf("Streaming FT2232H (D2XX) -> UDP %s:%d\n", DEST_IP, DEST_PORT);
    // 4. Acquisition Loop
    while (1) {
        // FT_Read is synchronous by default
        ftStatus = FT_Read(ftHandle, buffer, CHUNK_SIZE, &bytesRead);
        if (ftStatus == FT_OK && bytesRead > 0) {
            sendto(sock, (const char*)buffer, bytesRead, 0,
                   (const struct sockaddr *)&servaddr, sizeof(servaddr));
        } else if (ftStatus != FT_OK) {
            fprintf(stderr, "FTDI Read Error: %d\n", (int)ftStatus);
            break;
        }
    }
    // 5. Cleanup
    FT_Close(ftHandle);
    closesocket(sock);
    WSACleanup();
    return 0;
}