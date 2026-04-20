#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <time.h>
#include "inc/ftd2xx.h"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "lib/FTD2XX.lib")

#define CHUNK_SIZE 1024
#define PAYLOAD_SIZE 1023
#define FIFO_SIZE 32768
#define DEST_IP "127.0.0.1"
#define DEST_PORT 12345

unsigned char fifo[FIFO_SIZE];
uint32_t head = 0;
uint32_t tail = 0;
uint32_t count = 0;
uint32_t max_count = 0; // High Water Mark tracker

unsigned char hw_buf[CHUNK_SIZE];
unsigned char udp_buf[PAYLOAD_SIZE + 12];

#pragma pack(push, 1)
typedef struct
{
    uint32_t unix_time;
    uint32_t sample_tick;
    uint32_t seq_num;
} PacketHeader;
#pragma pack(pop)

int main()
{
    WSADATA wsaData;
    FT_HANDLE ftH;
    SOCKET sock;
    struct sockaddr_in servaddr;
    DWORD bytesRead = 0, bytesQueued = 0;
    uint64_t total_bytes_all_time = 0;
    uint32_t absolute_tick = 0;
    uint32_t bytes_since_last_print = 0;
    time_t start_time = time(NULL);
    PacketHeader hdr = {0};

    WSAStartup(MAKEWORD(2, 2), &wsaData);
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    if (getaddrinfo("P2P.local", "12345", &hints, &res) != 0)
    {
        printf("Failed to resolve P2P.local - falling back to 127.0.0.1\n");
        servaddr.sin_addr.s_addr = inet_addr("127.0.0.1");
    }
    else
    {
        memcpy(&servaddr, res->ai_addr, res->ai_addrlen);
        freeaddrinfo(res);
    }

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(DEST_PORT);
    inet_pton(AF_INET, DEST_IP, &servaddr.sin_addr);

    if (FT_OpenEx("USB<->GPS A", FT_OPEN_BY_DESCRIPTION, &ftH) != FT_OK)
    {
        printf("Error: Could not open FTDI device.\n");
        return 1;
    }

    FT_SetTimeouts(ftH, 100, 100);
    FT_SetLatencyTimer(ftH, 2);
    FT_SetUSBParameters(ftH, 65536, 65536);
    FT_Purge(ftH, FT_PURGE_RX | FT_PURGE_TX);

    hdr.unix_time = (uint32_t)time(NULL);
    printf("Collector Started: Streaming 1023-byte aligned packets.\n");

    while (1)
    {
        FT_GetQueueStatus(ftH, &bytesQueued);

        if (bytesQueued >= CHUNK_SIZE)
        {
            if (FT_Read(ftH, (LPVOID)hw_buf, CHUNK_SIZE, &bytesRead) == FT_OK)
            {
                // Optimized FIFO Ingest
                for (DWORD i = 0; i < bytesRead; i++)
                {
                    fifo[head] = hw_buf[i];
                    head = (head + 1) % FIFO_SIZE;
                    if (count < FIFO_SIZE)
                        count++;
                    else
                        tail = (tail + 1) % FIFO_SIZE;
                }

                // Update High Water Mark
                if (count > max_count)
                    max_count = count;

                // Dequeue packets
                while (count >= PAYLOAD_SIZE)
                {
                    hdr.sample_tick = absolute_tick;
                    memcpy(udp_buf, &hdr, 12);

                    // Pull Payload from FIFO
                    for (int i = 0; i < PAYLOAD_SIZE; i++)
                    {
                        udp_buf[12 + i] = fifo[tail];
                        tail = (tail + 1) % FIFO_SIZE;
                        count--;
                    }

                    sendto(sock, (const char *)udp_buf, PAYLOAD_SIZE + 12, 0,
                           (const struct sockaddr *)&servaddr, sizeof(servaddr));

                    absolute_tick += PAYLOAD_SIZE;
                    bytes_since_last_print += PAYLOAD_SIZE;
                    total_bytes_all_time += PAYLOAD_SIZE;
                    hdr.seq_num++;

                    // Second boundary check (8,184,000 bytes)
                    if (bytes_since_last_print >= 8184000)
                    {
                        time_t now = time(NULL);
                        double elapsed = difftime(now, start_time);

                        printf("\r[UP:%02d:%02d:%02d] Sent:%6.1f MB | FIFO Max:%6u | Tick:%u    ",
                               (int)(elapsed / 3600), ((int)elapsed % 3600) / 60, (int)elapsed % 60,
                               (double)total_bytes_all_time / 1048576.0, max_count, absolute_tick);

                        fflush(stdout);

                        // Reset for next second
                        absolute_tick = 0;
                        bytes_since_last_print = 0;
                        max_count = 0; // Reset monitor for new second
                        hdr.unix_time = (uint32_t)now;
                    }
                }
            }
        }
    }
    return 0;
}