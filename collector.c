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
#define FIFO_SIZE 131072
#define DEST_IP "127.0.0.1"
#define DEST_PORT 12345

// Use global memory to avoid stack issues
unsigned char fifo[FIFO_SIZE];
uint32_t head = 0;
uint32_t tail = 0;
uint32_t count = 0;

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
    uint64_t total_bytes_streamed = 0, total_bytes_all_time = 0;
    uint32_t absolute_tick = 0, bytes_since_last_print = 0;
    time_t start_time = time(NULL);
    double elapsed_time = 0.0, mb_total = 0.0;
    PacketHeader hdr = {0};

    WSAStartup(MAKEWORD(2, 2), &wsaData);
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(DEST_PORT);
    inet_pton(AF_INET, DEST_IP, &servaddr.sin_addr);

    if (FT_OpenEx("USB<->GPS A", FT_OPEN_BY_DESCRIPTION, &ftH) != FT_OK)
        return 1;
    FT_SetTimeouts(ftH, 100, 100);
    FT_SetLatencyTimer(ftH, 2);
    FT_SetUSBParameters(ftH, 65536, 65536);
    FT_Purge(ftH, FT_PURGE_RX | FT_PURGE_TX);

    hdr.unix_time = (uint32_t)time(NULL);
    printf("Locked 1023-byte Stream (1/8 ms alignment)\n");

    while (1)
    {
        FT_GetQueueStatus(ftH, &bytesQueued);

        if (bytesQueued >= CHUNK_SIZE)
        {
            if (FT_Read(ftH, (LPVOID)hw_buf, CHUNK_SIZE, &bytesRead) == FT_OK)
            {
                // Bulk Push to FIFO
                for (DWORD i = 0; i < bytesRead; i++)
                {
                    fifo[head] = hw_buf[i];
                    head = (head + 1) % FIFO_SIZE;
                    if (count < FIFO_SIZE)
                        count++;
                    else
                        tail = (tail + 1) % FIFO_SIZE; // Overwrite protection
                }

                // Dequeue 1023-byte packets
                while (count >= PAYLOAD_SIZE)
                {
                    // hdr.sample_tick = (uint32_t)total_bytes_streamed;
                    hdr.sample_tick = absolute_tick;
                    memcpy((void *)udp_buf, (void *)&hdr, sizeof(PacketHeader));
                    absolute_tick += PAYLOAD_SIZE;
                    bytes_since_last_print += PAYLOAD_SIZE;

                    if (bytes_since_last_print >= 8184000)
                    {
                        bytes_since_last_print = 0;
                        hdr.unix_time = (uint32_t)time(NULL);
                        total_bytes_all_time += total_bytes_streamed;
                        total_bytes_streamed = 0;
                        time_t now = time(NULL);
                        elapsed_time = difftime(now, start_time);
                        mb_total = total_bytes_all_time / (1024.0 * 1024.0);
                        printf("\r[RUNNING] Uptime: %02d:%02d:%02d | Sent: %.2f MB | Tick: %u    ",
                               (int)(elapsed_time / 3600), ((int)elapsed_time % 3600) / 60,
                               (int)elapsed_time % 60, mb_total, hdr.sample_tick);
                        fflush(stdout);
                    }

                    // Bulk Pull from FIFO (manual copy to handle wrap)
                    for (int i = 0; i < PAYLOAD_SIZE; i++)
                    {
                        udp_buf[12 + i] = fifo[tail];
                        tail = (tail + 1) % FIFO_SIZE;
                        count--;
                    }

                    sendto(sock, (const char *)udp_buf, PAYLOAD_SIZE + 12, 0,
                           (const struct sockaddr *)&servaddr, sizeof(servaddr));

                    total_bytes_streamed += PAYLOAD_SIZE;
                    hdr.seq_num++;
                }
            }
        }
    }
    return 0;
}