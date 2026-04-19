#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <cstring>
#include <chrono>
#include <stdint.h>

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

const uint32_t JOIN_MAGIC = 0x4A4F494E;
const int RING_BUFFER_SIZE = 8184000;
const int UDP_BUF_MAX = 2048;

class ElasticReceiver {
private:
    SOCKET sock;
    std::atomic<bool> running{true};
    std::vector<uint8_t> ring_buffer;
    std::atomic<size_t> write_idx{0};
    size_t read_idx = 0;
    std::mutex mtx;
    std::condition_variable cv;

    void ingest_thread() {
        uint8_t packet[UDP_BUF_MAX];
        sockaddr_in sender_addr;
        int addr_len = sizeof(sender_addr);

        while (running) {
            // Use recvfrom for connectionless UDP
            int rx = recvfrom(sock, (char*)packet, UDP_BUF_MAX, 0, (struct sockaddr*)&sender_addr, &addr_len);
            
            if (rx == SOCKET_ERROR) {
#ifdef _WIN32
                int err = WSAGetLastError();
                if (err == WSAETIMEDOUT) continue; 
                if (running) std::cerr << "[!] Winsock Error: " << err << std::endl;
#endif
                continue;
            }

            if (rx <= 12) continue; // Header is 12 bytes

            uint8_t* payload = packet + 12;
            int payload_len = rx - 12;

            for (int i = 0; i < payload_len; ++i) {
                ring_buffer[write_idx] = payload[i];
                write_idx = (write_idx + 1) % RING_BUFFER_SIZE;
            }
            cv.notify_one();
        }
    }

public:
    ElasticReceiver() : ring_buffer(RING_BUFFER_SIZE), sock(INVALID_SOCKET) {}
bool connect_to_relay(const char* ip, int port) {
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) return false;

    // --- NEW: BIND TO ANY PORT ---
    // This tells the OS to assign a local port immediately 
    // and keep it open for incoming traffic.
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = INADDR_ANY;
    local.sin_port = 0; // Let OS pick, but bind it now
    if (bind(sock, (struct sockaddr*)&local, sizeof(local)) == SOCKET_ERROR) {
        return false;
    }

    int timeout = 500;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(port);
    inet_pton(AF_INET, ip, &dest.sin_addr);

    // --- RE-SEND JOIN A FEW TIMES ---
    // UDP is "fire and forget". Sometimes the first one is dropped 
    // by the OS while initializing the stack.
    std::cout << "[*] Sending JOIN to " << ip << ":" << port << "..." << std::endl;
    for(int i=0; i<3; i++) {
        sendto(sock, (const char*)&JOIN_MAGIC, sizeof(JOIN_MAGIC), 0, (struct sockaddr*)&dest, sizeof(dest));
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::thread t(&ElasticReceiver::ingest_thread, this);
    t.detach();
    return true;
}

    bool get_samples(uint8_t* out_ptr, size_t count) {
        std::unique_lock<std::mutex> lock(mtx);
        
        bool success = cv.wait_for(lock, std::chrono::seconds(2), [&] {
            size_t w = write_idx.load();
            size_t available = (w >= read_idx) ? (w - read_idx) : (RING_BUFFER_SIZE - read_idx + w);
            return available >= count || !running;
        });

        if (!success) {
            size_t w = write_idx.load();
            size_t avail = (w >= read_idx) ? (w - read_idx) : (RING_BUFFER_SIZE - read_idx + w);
            std::cerr << "\n[!] Timeout. Buffer: " << avail << "/" << count << std::endl;
            return false;
        }

        for (size_t i = 0; i < count; ++i) {
            out_ptr[i] = ring_buffer[read_idx];
            read_idx = (read_idx + 1) % RING_BUFFER_SIZE;
        }
        return true;
    }

    ~ElasticReceiver() {
        running = false;
        if (sock != INVALID_SOCKET) {
#ifdef _WIN32
            closesocket(sock);
            WSACleanup();
#else
            close(sock);
#endif
        }
    }
};

int main() {
    ElasticReceiver rx;
    if (!rx.connect_to_relay("127.0.0.1", 12345)) {
        std::cerr << "Failed to connect." << std::endl;
        return 1;
    }

    std::cout << "[*] Staging active. Listening for 20ms blocks..." << std::endl;

    while (true) {
        const size_t integration_samples = 8184 * 20; 
        std::vector<uint8_t> block(integration_samples);

        if (rx.get_samples(block.data(), integration_samples)) {
            // Success - keep the output on one line to avoid spamming
            printf("\r[LIVE] Received 20ms Block | Sample: %02X", (unsigned int)block.data());
            fflush(stdout);
        }
    }
    return 0;
}