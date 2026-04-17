// Collector.c Upstream FT2232H (D2XX) -> UDP
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

#define CHUNK_SIZE 1023
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
    FT_HANDLE ftHandle;
    FT_STATUS ftStatus;
    SOCKET sock;
    struct sockaddr_in servaddr;
    
    // Initializing with {0} is critical to avoid the Error 4 / C4700 warning
    unsigned char buffer[CHUNK_SIZE] = {0}; 
    unsigned char send_buf[CHUNK_SIZE + 12] = {0};
    DWORD bytesRead = 0;

    uint64_t total_bytes_in_second = 0;
    PacketHeader hdr;
    memset(&hdr, 0, sizeof(PacketHeader));

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return 1;

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(DEST_PORT);
    inet_pton(AF_INET, DEST_IP, &servaddr.sin_addr);

    // EXACT OPEN LOGIC
    ftStatus = FT_OpenEx("USB<->GPS A", FT_OPEN_BY_DESCRIPTION, &ftHandle);
    if (ftStatus != FT_OK) {
        printf("Open failed\n");
        return 1;
    }

    FT_SetLatencyTimer(ftHandle, 2);
    FT_SetUSBParameters(ftHandle, 65536, 65536);
    
    hdr.unix_time = (uint32_t)time(NULL);

    printf("Streaming (Initialized Buffers) -> UDP %s:%d\n", DEST_IP, DEST_PORT);

    while (1) {
        // Use CHUNK_SIZE (1024) exactly as in your working version
        ftStatus = FT_Read(ftHandle, buffer, CHUNK_SIZE+2, &bytesRead);
        
        if (ftStatus == FT_OK && bytesRead == 1025) {
            if (total_bytes_in_second >= 8184000) {
                hdr.unix_time = (uint32_t)time(NULL);
                total_bytes_in_second = 0;
            }

            hdr.sample_tick = (uint32_t)total_bytes_in_second;

            // Copy header then payload
            memcpy(send_buf, &hdr, 12);
            memcpy(send_buf + 12, buffer, CHUNK_SIZE);

            sendto(sock, (const char*)send_buf, 1035, 0,
                   (const struct sockaddr *)&servaddr, sizeof(servaddr));

            total_bytes_in_second += 1023;
            hdr.seq_num++;

        } else if (ftStatus != FT_OK) {
            fprintf(stderr, "FTDI Read Error: %d\n", (int)ftStatus);
            break;
        }
    }

    FT_Close(ftHandle);
    closesocket(sock);
    WSACleanup();
    return 0;
}