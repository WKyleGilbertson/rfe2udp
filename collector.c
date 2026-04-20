#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <time.h>
#include "inc/ftd2xx.h"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "lib/FTD2XX.lib")

#define CHUNK_SIZE 1024
#define PAYLOAD_SIZE 1023
#define FIFO_SIZE 32768  // Reduced based on 2046 high-water mark
#define DEFAULT_TARGET "P2P.local"
#define DEFAULT_PORT "12345"

unsigned char fifo[FIFO_SIZE];
uint32_t head = 0, tail = 0, count = 0;
uint32_t max_count = 0, dropped_pkts = 0;
uint32_t last_seq = 0;
bool first_pkt = true;

unsigned char hw_buf[CHUNK_SIZE];
unsigned char udp_buf[PAYLOAD_SIZE + 12];

#pragma pack(push, 1)
typedef struct {
    uint32_t unix_time;
    uint32_t sample_tick;
    uint32_t seq_num;
} PacketHeader;
#pragma pack(pop)

int main(int argc, char* argv[]) {
    WSADATA wsaData;
    FT_HANDLE ftH;
    SOCKET sock;
    struct sockaddr_in servaddr;
    const char* target_host = (argc > 1) ? argv : DEFAULT_TARGET;
    const char* target_port = (argc > 2) ? argv : DEFAULT_PORT;

    WSAStartup(MAKEWORD(2, 2), &wsaData);
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    // Resolve target (mDNS or IP) once at startup
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    printf("[*] Resolving %s:%s...\n", target_host, target_port);
    if (getaddrinfo(target_host, target_port, &hints, &res) != 0) {
        printf("[!] Failed to resolve. Falling back to 127.0.0.1:12345\n");
        memset(&servaddr, 0, sizeof(servaddr));
        servaddr.sin_family = AF_INET;
        servaddr.sin_port = htons(12345);
        inet_pton(AF_INET, "127.0.0.1", &servaddr.sin_addr);
    } else {
        memcpy(&servaddr, res->ai_addr, res->ai_addrlen);
        freeaddrinfo(res);
    }

    if (FT_OpenEx("USB<->GPS A", FT_OPEN_BY_DESCRIPTION, &ftH) != FT_OK) {
        printf("[!] Error: Could not open FTDI device.\n");
        return 1;
    }

    FT_SetTimeouts(ftH, 100, 100);
    FT_SetLatencyTimer(ftH, 2);
    FT_SetUSBParameters(ftH, 65536, 65536);
    FT_Purge(ftH, FT_PURGE_RX | FT_PURGE_TX);

    PacketHeader hdr = {0};
    hdr.unix_time = (uint32_t)time(NULL);
    uint32_t absolute_tick = 0, bytes_since_print = 0;
    uint64_t total_mb = 0;
    time_t start_t = time(NULL);

    printf("[*] Casting to %s. FIFO Monitor Active.\n", target_host);

    while (1) {
        DWORD bytesQueued = 0, bytesRead = 0;
        FT_GetQueueStatus(ftH, &bytesQueued);

        if (bytesQueued >= CHUNK_SIZE) {
            if (FT_Read(ftH, hw_buf, CHUNK_SIZE, &bytesRead) == FT_OK) {
                // Ingest to FIFO
                for (DWORD i = 0; i < bytesRead; i++) {
                    fifo[head] = hw_buf[i];
                    head = (head + 1) % FIFO_SIZE;
                    if (count < FIFO_SIZE) count++;
                    else tail = (tail + 1) % FIFO_SIZE;
                }
                if (count > max_count) max_count = count;

                // Dequeue
                while (count >= PAYLOAD_SIZE) {
                    hdr.sample_tick = absolute_tick;
                    memcpy(udp_buf, &hdr, 12);

                    for (int i = 0; i < PAYLOAD_SIZE; i++) {
                        udp_buf[12 + i] = fifo[tail];
                        tail = (tail + 1) % FIFO_SIZE;
                        count--;
                    }

                    sendto(sock, (const char*)udp_buf, PAYLOAD_SIZE + 12, 0, (struct sockaddr*)&servaddr, sizeof(servaddr));

                    absolute_tick += PAYLOAD_SIZE;
                    bytes_since_print += PAYLOAD_SIZE;
                    total_mb += PAYLOAD_SIZE;
                    hdr.seq_num++;

                    if (bytes_since_print >= 8184000) {
                        time_t now = time(NULL);
                        double elapsed = difftime(now, start_t);
                        printf("\r[UP:%02d:%02d:%02d] Sent:%6.1fMB | FIFO Max:%5u | Drops: %u", 
                            (int)elapsed/3600, ((int)elapsed%3600)/60, (int)elapsed%60, 
                            (double)total_mb/1048576.0, max_count, dropped_pkts);
                        fflush(stdout);

                        absolute_tick = 0;
                        bytes_since_print = 0;
                        max_count = 0;
                        hdr.unix_time = (uint32_t)now;
                    }
                }
            }
        }
    }
    return 0;
}