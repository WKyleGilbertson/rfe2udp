#include <iostream>
#include <vector>
#include <mutex>
#include <set>
#include <stdint.h>
#include <cstring>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef int socklen_t;
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    typedef int SOCKET;
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
#endif

const uint32_t JOIN_MAGIC = 0x4A4F494E; // 'JOIN'
const int MAX_UDP_SIZE = 2048;

struct ClientAddr {
    sockaddr_in addr;
    bool operator<(const ClientAddr& other) const {
        if (addr.sin_addr.s_addr != other.addr.sin_addr.s_addr)
            return addr.sin_addr.s_addr < other.addr.sin_addr.s_addr;
        return addr.sin_port < other.addr.sin_port;
    }
};

class UDPRelay {
    uint16_t listen_port = 12345;
    std::set<ClientAddr> clients;
    std::mutex client_mtx;
    SOCKET sock;

public:
    void run() {
#ifdef _WIN32
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            std::cerr << "[!] Winsock init failed." << std::endl;
            return;
        }
#endif
        sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock == INVALID_SOCKET) return;

        // Set a small timeout for recvfrom so the loop remains responsive
        int timeout = 1000;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

        sockaddr_in servaddr{};
        servaddr.sin_family = AF_INET;
        servaddr.sin_addr.s_addr = INADDR_ANY;
        servaddr.sin_port = htons(listen_port);

        if (bind(sock, (struct sockaddr*)&servaddr, sizeof(servaddr)) == SOCKET_ERROR) {
#ifdef _WIN32
            std::cerr << "[!] Bind failed. Error: " << WSAGetLastError() << std::endl;
            std::cerr << "[!] Check if another relay or glimpse.py is already using port " << listen_port << std::endl;
#endif
            return;
        }

        uint8_t buffer[MAX_UDP_SIZE];
        uint32_t rx_count = 0;
        
        std::cout << "[*] Relay active on port " << listen_port << std::endl;
        std::cout << "[*] Waiting for Caster data and Client JOIN (0x4A4F494E)..." << std::endl;

        while (true) {
            sockaddr_in src_addr;
            socklen_t addr_len = sizeof(src_addr);
            int len = recvfrom(sock, (char*)buffer, MAX_UDP_SIZE, 0, (struct sockaddr*)&src_addr, &addr_len);

            if (len <= 0) continue;

            // --- 1. HEARTBEAT & CASTER TRACKING ---
            // We assume packets > 100 bytes are RF data from the caster
            if (len > 100) {
                if (++rx_count % 500 == 0) {
                    printf("\r[*] Traffic: %u packets received from Caster...", rx_count);
                    fflush(stdout);
                }
            }

            // --- 2. REGISTRATION LOGIC ---
            if (len >= 4) {
                uint32_t magic;
                memcpy(&magic, buffer, 4);
                
                if (magic == JOIN_MAGIC) {
                    std::lock_guard<std::mutex> lock(client_mtx);
                    ClientAddr c = {src_addr};
                    
                    char ip[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &src_addr.sin_addr, ip, INET_ADDRSTRLEN);
                    uint16_t port = ntohs(src_addr.sin_port);

                    if (clients.find(c) == clients.end()) {
                        std::cout << "\n[+] REGISTERED: " << ip << ":" << port << std::endl;
                        clients.insert(c);
                    } else {
                        // Re-join from existing client (don't spam, but good for debug)
                        // std::cout << "[.] Keep-alive from: " << port << std::endl;
                    }
                    continue; 
                }
            }

            // --- 3. FAN-OUT LOGIC ---
            {
                std::lock_guard<std::mutex> lock(client_mtx);
                if (clients.empty()) continue;

                for (auto it = clients.begin(); it != clients.end();) {
                    int sent = sendto(sock, (const char*)buffer, len, 0, (struct sockaddr*)&it->addr, sizeof(it->addr));
                    if (sent == SOCKET_ERROR) {
                        char ip[INET_ADDRSTRLEN];
                        inet_ntop(AF_INET, &it->addr.sin_addr, ip, INET_ADDRSTRLEN);
                        std::cout << "\n[-] REMOVING STALE CLIENT: " << ip << ":" << ntohs(it->addr.sin_port) << std::endl;
                        it = clients.erase(it);
                    } else {
                        ++it;
                    }
                }
            }
        }
    }

    ~UDPRelay() {
#ifdef _WIN32
        closesocket(sock);
        WSACleanup();
#else
        close(sock);
#endif
    }
};

int main() {
    UDPRelay relay;
    relay.run();
    return 0;
}