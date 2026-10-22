#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <charconv>
#include <cstring>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <variant>


class Socket {
public:
    explicit Socket(int fd) : fd_(fd) {}
    ~Socket() { if (fd_ >= 0) ::close(fd_); }
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    int  get()   const { return fd_; }
    bool valid() const { return fd_ >= 0; }
    bool close_checked() {
        if (fd_ < 0) return true;
        int r = ::close(fd_); fd_ = -1; return r == 0;
    }
private:
    int fd_;
};

struct BadArg { std::string msg; };
struct Config { int domain, type; std::string ip; uint16_t port; bool do_bind; };

static std::optional<int> parse_domain(std::string_view s) {
    if (s == "AF_INET")  return AF_INET;
    if (s == "AF_INET6") return AF_INET6;
    if (s == "AF_UNIX")  return AF_UNIX;
    return std::nullopt;
}
static std::optional<int> parse_type(std::string_view s) {
    if (s == "SOCK_STREAM") return SOCK_STREAM;
    if (s == "SOCK_DGRAM")  return SOCK_DGRAM;
    return std::nullopt;
}
static std::optional<uint16_t> parse_port(std::string_view s) {
    unsigned long v = 0;
    auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc{} || p != s.data() + s.size() || v > 65535) return std::nullopt;
    return static_cast<uint16_t>(v);
}
static std::variant<Config, BadArg> parse_args(int argc, char* argv[]) {
    if (argc < 5 || argc > 6)
        return BadArg{"Использование: " + std::string(argv[0]) +
                      " <domain> <type> <ip> <port> [--bind]"};
    auto d = parse_domain(argv[1]);
    if (!d) return BadArg{"Неверный домен"};
    auto t = parse_type(argv[2]);
    if (!t) return BadArg{"Неверный тип сокета"};
    auto p = parse_port(argv[4]);
    if (!p) return BadArg{"Порт вне 0..65535"};
    bool b = false;
    if (argc == 6) {
        if (std::string_view(argv[5]) != "--bind") return BadArg{"Ожидается --bind"};
        b = true;
    }
    return Config{*d, *t, argv[3], *p, b};
}


static void print_ip_port(const char* tag, const sockaddr_in& a) {
    char buf[INET_ADDRSTRLEN] = {};
    if (!::inet_ntop(AF_INET, &a.sin_addr, buf, sizeof(buf))) { std::perror("inet_ntop"); return; }
    std::cout << tag << ": " << buf << ':' << ::ntohs(a.sin_port) << '\n';
}

int main(int argc, char* argv[]) {
    auto parsed = parse_args(argc, argv);
    if (std::holds_alternative<BadArg>(parsed)) {
        std::cerr << std::get<BadArg>(parsed).msg << '\n';
        return EXIT_FAILURE;
    }
    Config cfg = std::get<Config>(parsed);
    if (cfg.domain != AF_INET) { std::cerr << "Только AF_INET\n"; return EXIT_FAILURE; }

    Socket sock(::socket(cfg.domain, cfg.type, 0));
    if (!sock.valid()) { std::perror("socket"); return EXIT_FAILURE; }
    std::cout << "FD = " << sock.get() << '\n';

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = ::htons(cfg.port);
    int rc = ::inet_pton(AF_INET, cfg.ip.c_str(), &addr.sin_addr);
    if (rc == 0) { std::cerr << "Неверный IP: " << cfg.ip << '\n'; return EXIT_FAILURE; }
    if (rc < 0)  { std::perror("inet_pton"); return EXIT_FAILURE; }

    print_ip_port("Адрес", addr);       

    if (cfg.do_bind) {
        if (::bind(sock.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            if (errno == EADDRINUSE)      std::cerr << "Порт занят (EADDRINUSE)\n";
            else if (errno == EACCES)     std::cerr << "Нет прав (EACCES)\n";
            else if (errno == EADDRNOTAVAIL) std::cerr << "Адрес не принадлежит машине\n";
            else std::perror("bind");
            return EXIT_FAILURE;
        }
        std::cout << "bind OK\n";
        sockaddr_in act{}; socklen_t len = sizeof(act);
        if (::getsockname(sock.get(), reinterpret_cast<sockaddr*>(&act), &len) == 0)
            print_ip_port("Фактический", act);
    }

    if (!sock.close_checked()) { std::perror("close"); return EXIT_FAILURE; }
    std::cout << "Закрыт\n";
    return EXIT_SUCCESS;
}