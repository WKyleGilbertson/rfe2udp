#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <time.h>
#include "inc/ftd2xx.h"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "lib/FTD2XX.lib")

#define CHUNK_SIZE 1024  // Standard USB packet multiple
#define DEST_IP "127.0.0.1"
#define DEST_PORT 12345

#pragma pack(push, 1)
typedef struct {
    uint32_t unix_time;   
    uint32_t sample_tick; 
    uint32_t seq_num;     
} PacketHeader;
#pragma pack(pop)

int main() {
    WSADATA wsaData;
    FT_HANDLE ftH;
    SOCKET sock;
    struct sockaddr_in servaddr;
    
    unsigned char buffer[CHUNK_SIZE] = {0}; 
    unsigned char send_buf[CHUNK_SIZE + 12] = {0};
    DWORD bytesRead = 0, bytesQueued = 0;

    uint64_t total_bytes_in_second = 0;
    PacketHeader hdr = {0};

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return 1;
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(DEST_PORT);
    inet_pton(AF_INET, DEST_IP, &servaddr.sin_addr);

    if (FT_OpenEx("USB<->GPS A", FT_OPEN_BY_DESCRIPTION, &ftH) != FT_OK) {
        printf("Open failed\n");
        return 1;
    }

    // Initialize exactly as a raw pass-through
    FT_SetTimeouts(ftH, 100, 100);
    FT_SetLatencyTimer(ftH, 2);
    FT_SetUSBParameters(ftH, 65536, 65536); 
    FT_Purge(ftH, FT_PURGE_RX | FT_PURGE_TX);
    
    hdr.unix_time = (uint32_t)time(NULL);
    printf("Raw Stream Active (1024 bytes) -> UDP %s:%d\n", DEST_IP, DEST_PORT);

    while (1) {
        // Check if at least one chunk is ready
        FT_GetQueueStatus(ftH, &bytesQueued);

        if (bytesQueued >= CHUNK_SIZE) {
            // Read exactly 1024 bytes
            if (FT_Read(ftH, buffer, CHUNK_SIZE, &bytesRead) == FT_OK && bytesRead == CHUNK_SIZE) {
                
                // One-second reset for sample_tick
                if (total_bytes_in_second >= 8184000) {
                    hdr.unix_time = (uint32_t)time(NULL);
                    total_bytes_in_second = 0;
                    printf("."); // Heartbeat
                    fflush(stdout);
                }

                hdr.sample_tick = (uint32_t)total_bytes_in_second;

                // Copy header + the 1024 raw bytes (No skipping!)
                memcpy(send_buf, &hdr, 12);
                memcpy(send_buf + 12, buffer, CHUNK_SIZE);

                sendto(sock, (const char*)send_buf, CHUNK_SIZE + 12, 0,
                       (const struct sockaddr *)&servaddr, sizeof(servaddr));

                total_bytes_in_second += CHUNK_SIZE;
                hdr.seq_num++;
            }
        }
    }
    return 0;
}