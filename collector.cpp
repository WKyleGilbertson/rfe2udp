#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <time.h>
#include "inc/ftd2xx.h"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "lib/FTD2XX.lib")

#define CHUNK_SIZE 1024
#define PAYLOAD_SIZE 1023
#define FIFO_SIZE 65536 
#define DEST_PORT "12345"

#pragma pack(push, 1)
typedef struct {
    uint8_t pkt_type; // 0x01 for Data, 0x02 for Command, etc.
    uint32_t unix_time;
    uint32_t sample_tick;
    uint32_t seq_num;
    char dev_tag; 
} RFE_Header_t;
#pragma pack(pop)

struct InternalState {
    unsigned char fifo[FIFO_SIZE];
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    unsigned char hw_tmp[CHUNK_SIZE];
    unsigned char udp_out[PAYLOAD_SIZE + sizeof(RFE_Header_t)];
    char ftdi_sn;
    RFE_Header_t hdr;
};

static struct InternalState S;

int main(int argc, char** argv) {
    WSADATA wsaData;
    FT_HANDLE ftH;
    SOCKET sock;
    struct sockaddr_in servaddr;
    S.hdr.pkt_type = 0x01; // Data packet
    
    memset(&S, 0, sizeof(struct InternalState));
    const char* target_host = (argc > 1) ? *(argv + 1) : "P2P.local";

    WSAStartup(MAKEWORD(2, 2), &wsaData);
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    if (FT_OpenEx((void*)"USB<->GPS A", FT_OPEN_BY_DESCRIPTION, &ftH) != FT_OK) {
        printf("[!] Error: Could not open FTDI device.\n");
        return 1;
    }

    // We use C-style casts to treat the memory addresses as raw byte arrays
    // This bypasses the compiler's confused type-checking of the struct members
    if (FT_GetDeviceInfo(ftH, NULL, NULL, (char*)(&S.ftdi_sn), NULL, NULL) == FT_OK) {
        int slen = 0;
        char* pSN = (char*)(&S.ftdi_sn);
        while(pSN[slen] != '\0' && slen < 31) slen++;
        
        printf("[*] FTDI Serial: %s\n", pSN);
        
        if (slen >= 4) {
            // Manual pointer arithmetic to avoid [] subscript operator conflicts
            *(char*)((char*)&S.hdr.dev_tag + 0) = *(char*)(pSN + slen - 4);
            *(char*)((char*)&S.hdr.dev_tag + 1) = *(char*)(pSN + slen - 3);
            *(char*)((char*)&S.hdr.dev_tag + 2) = *(char*)(pSN + slen - 2);
            *(char*)((char*)&S.hdr.dev_tag + 3) = *(char*)(pSN + slen - 1);
        } else {
            memcpy(&S.hdr.dev_tag, "UKWN", 4);
        }
    }

    FT_SetTimeouts(ftH, 100, 100);
    FT_SetLatencyTimer(ftH, 2);
    FT_SetUSBParameters(ftH, 65536, 65536);
    FT_Purge(ftH, FT_PURGE_RX | FT_PURGE_TX);

    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    if (getaddrinfo(target_host, DEST_PORT, &hints, &res) != 0) {
        memset(&servaddr, 0, sizeof(servaddr));
        servaddr.sin_family = AF_INET;
        servaddr.sin_port = htons(12345);
        inet_pton(AF_INET, "127.0.0.1", &servaddr.sin_addr);
    } else {
        memcpy(&servaddr, res->ai_addr, res->ai_addrlen);
        freeaddrinfo(res);
    }

    S.hdr.unix_time = (uint32_t)time(NULL);
    uint32_t absolute_tick = 0, bytes_since_print = 0;
    uint64_t total_bytes = 0;
    time_t start_t = time(NULL);

    printf("[*] Streaming to %s | Tag: %.4s\n", target_host, (char*)&S.hdr.dev_tag);

    while (1) {
        DWORD bQ = 0, bR = 0;
        FT_GetQueueStatus(ftH, &bQ);

        if (bQ >= CHUNK_SIZE) {
            if (FT_Read(ftH, &S.hw_tmp, CHUNK_SIZE, &bR) == FT_OK) {
                for (DWORD i = 0; i < bR; i++) {
                    S.fifo[S.head] = S.hw_tmp[i];
                    S.head = (S.head + 1) % FIFO_SIZE;
                    if (S.count < FIFO_SIZE) S.count++;
                    else S.tail = (S.tail + 1) % FIFO_SIZE;
                }

                while (S.count >= PAYLOAD_SIZE) {
                    S.hdr.sample_tick = absolute_tick;
                    memcpy(&S.udp_out, &S.hdr, sizeof(RFE_Header_t));

                    for (int i = 0; i < PAYLOAD_SIZE; i++) {
                        S.udp_out[sizeof(RFE_Header_t) + i] = S.fifo[S.tail];
                        S.tail = (S.tail + 1) % FIFO_SIZE;
                        S.count--;
                    }

                    sendto(sock, (const char*)&S.udp_out, PAYLOAD_SIZE + (int)sizeof(RFE_Header_t), 0, (struct sockaddr*)&servaddr, sizeof(servaddr));

                    absolute_tick += PAYLOAD_SIZE;
                    bytes_since_print += PAYLOAD_SIZE;
                    total_bytes += PAYLOAD_SIZE;
                    S.hdr.seq_num++;

                    if (bytes_since_print >= 8184000) {
                        time_t now = time(NULL);
                        double elapsed = difftime(now, start_t);
                        printf("\r[UP:%02d:%02d:%02d] Sent:%6.1fMB | ID: %.4s", 
                            (int)elapsed/3600, ((int)elapsed%3600)/60, (int)elapsed%60, 
                            (double)total_bytes/1048576.0, (char*)&S.hdr.dev_tag);
                        fflush(stdout);
                        bytes_since_print = 0;
                        S.hdr.unix_time = (uint32_t)now;
                    }
                }
            }
        }
    }
    return 0;
}