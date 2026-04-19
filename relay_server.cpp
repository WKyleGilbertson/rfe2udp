#include <iostream>
#include <vector>
#include <mutex>
#include <thread>
#include <set>
#include <stdint.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef int socklen_t; // Windows uses int for length
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
typedef int SOCKET;
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#endif

// A simple registration packet structure
struct Registration
{
  uint32_t magic; // Should be 0x4A4F494E ('JOIN')
};

struct ClientAddr
{
  sockaddr_in addr;
  bool operator<(const ClientAddr &other) const
  {
    if (addr.sin_addr.s_addr != other.addr.sin_addr.s_addr)
      return addr.sin_addr.s_addr < other.addr.sin_addr.s_addr;
    return addr.sin_port < other.addr.sin_port;
  }
};

class UDPRelay
{
  uint16_t listen_port = 12345;
  std::set<ClientAddr> clients;
  std::mutex client_mtx;
  SOCKET sock;

public:
  void run()
  {
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET)
      return;

    sockaddr_in servaddr{};
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(listen_port);

    if (bind(sock, (struct sockaddr *)&servaddr, sizeof(servaddr)) == SOCKET_ERROR)
    {
      std::cerr << "Bind failed!" << std::endl;
      return;
    }

    // Buffer for max UDP packet
    uint8_t buffer;
    std::cout << "[*] Relay active on port " << listen_port << std::endl;
    std::cout << "[*] Clients must send 0x4A4F494E to register." << std::endl;

    while (true)
    {
      sockaddr_in src_addr;
      socklen_t addr_len = sizeof(src_addr);
      int len = recvfrom(sock, (char *)buffer, sizeof(buffer), 0, (struct sockaddr *)&src_addr, &addr_len);

      if (len <= 0)
        continue;

      // Logic: Is this a registration or a data packet?
      // Data packets from your caster are 1023 + 12 = 1035 bytes.
      if (len == sizeof(Registration))
      {
        Registration *reg = (Registration *)buffer;
        if (reg->magic == 0x4A4F494E)
        {
          std::lock_guard<std::mutex> lock(client_mtx);
          ClientAddr c = {src_addr};
          if (clients.find(c) == clients.end())
          {
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &src_addr.sin_addr, ip, INET_ADDRSTRLEN);
            std::cout << "[+] New Client: " << ip << ":" << ntohs(src_addr.sin_port) << std::endl;
            clients.insert(c);
          }
          continue;
        }
      }

      // Otherwise, it's RF data - Fan out to all registered clients
      {
        std::lock_guard<std::mutex> lock(client_mtx);
        if (clients.empty())
          continue;

        for (auto it = clients.begin(); it != clients.end();)
        {
          int sent = sendto(sock, (const char *)buffer, len, 0, (struct sockaddr *)&it->addr, sizeof(it->addr));
          if (sent == SOCKET_ERROR)
          {
            // Optional: remove stale clients
            std::cout << "[-] Removing stale client." << std::endl;
            it = clients.erase(it);
          }
          else
          {
            ++it;
          }
        }
      }
    }
  }
};

int main()
{
  UDPRelay relay;
  relay.run();
  return 0;
}