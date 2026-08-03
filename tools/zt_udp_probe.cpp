/*
 * UDP path probe for distinguishing network/overlay loss from SRT or codec
 * behaviour. This intentionally has no UltraGrid dependencies.
 */

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using clock_type = std::chrono::steady_clock;

namespace {

constexpr uint32_t MAGIC = 0x5a545550; // "ZTUP"
constexpr uint16_t VERSION = 1;
constexpr uint16_t FLAG_FIN = 1;
constexpr int DEFAULT_BUFFER = 16 * 1024 * 1024;

#pragma pack(push, 1)
struct wire_header {
        uint32_t magic;
        uint16_t version;
        uint16_t flags;
        uint64_t stream_id;
        uint64_t sequence;
        uint64_t send_time_ns;
        uint32_t frame;
        uint16_t packet_in_frame;
        uint16_t packets_in_frame;
};
#pragma pack(pop)

static_assert(sizeof(wire_header) == 40, "wire header layout changed");

std::atomic<bool> stop_requested{false};

uint64_t host_to_be64(uint64_t value)
{
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        return __builtin_bswap64(value);
#else
        return value;
#endif
}

uint64_t be64_to_host(uint64_t value)
{
        return host_to_be64(value);
}

uint64_t now_ns()
{
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
                       clock_type::now().time_since_epoch())
                .count();
}

void signal_handler(int)
{
        stop_requested = true;
}

[[noreturn]] void fail(const std::string &message)
{
        throw std::runtime_error(message);
}

uint64_t parse_scaled(const std::string &text)
{
        if (text.empty()) {
                fail("empty numeric value");
        }
        char *end = nullptr;
        errno = 0;
        double value = std::strtod(text.c_str(), &end);
        if (errno != 0 || end == text.c_str() || value < 0) {
                fail("invalid numeric value: " + text);
        }
        double scale = 1.0;
        if (*end != '\0') {
                if (end[1] != '\0') {
                        fail("invalid suffix in: " + text);
                }
                switch (*end) {
                case 'k':
                case 'K': scale = 1000.0; break;
                case 'm':
                case 'M': scale = 1000000.0; break;
                case 'g':
                case 'G': scale = 1000000000.0; break;
                default: fail("invalid suffix in: " + text);
                }
        }
        double result = value * scale;
        if (result > static_cast<double>(std::numeric_limits<uint64_t>::max())) {
                fail("numeric value is too large: " + text);
        }
        return static_cast<uint64_t>(result);
}

double parse_double(const std::string &text)
{
        char *end = nullptr;
        errno = 0;
        double value = std::strtod(text.c_str(), &end);
        if (errno != 0 || end == text.c_str() || *end != '\0' || value < 0) {
                fail("invalid numeric value: " + text);
        }
        return value;
}

struct endpoint {
        std::string host;
        std::string port;
};

endpoint split_endpoint(const std::string &value, bool allow_empty_host)
{
        endpoint result;
        if (!value.empty() && value.front() == '[') {
                auto close = value.find(']');
                if (close == std::string::npos || close + 1 >= value.size() ||
                    value[close + 1] != ':') {
                        fail("expected [address]:port: " + value);
                }
                result.host = value.substr(1, close - 1);
                result.port = value.substr(close + 2);
        } else {
                auto colon = value.rfind(':');
                if (colon == std::string::npos) {
                        fail("expected address:port: " + value);
                }
                result.host = value.substr(0, colon);
                result.port = value.substr(colon + 1);
        }
        if ((!allow_empty_host && result.host.empty()) || result.port.empty()) {
                fail("invalid endpoint: " + value);
        }
        return result;
}

struct resolved_address {
        sockaddr_storage storage{};
        socklen_t length = 0;
        int family = AF_UNSPEC;
};

resolved_address resolve(const endpoint &ep, bool passive)
{
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_protocol = IPPROTO_UDP;
        hints.ai_flags = passive ? AI_PASSIVE : 0;
        addrinfo *addresses = nullptr;
        const char *host = ep.host.empty() ? nullptr : ep.host.c_str();
        int rc = getaddrinfo(host, ep.port.c_str(), &hints, &addresses);
        if (rc != 0) {
                fail("cannot resolve " + ep.host + ":" + ep.port + ": " +
                     gai_strerror(rc));
        }
        resolved_address result;
        if (addresses == nullptr ||
            addresses->ai_addrlen > sizeof(result.storage)) {
                freeaddrinfo(addresses);
                fail("no usable address");
        }
        std::memcpy(&result.storage, addresses->ai_addr,
                    addresses->ai_addrlen);
        result.length = addresses->ai_addrlen;
        result.family = addresses->ai_family;
        freeaddrinfo(addresses);
        return result;
}

int make_socket(int family, int requested_buffer, bool receive)
{
        int fd = socket(family, SOCK_DGRAM, IPPROTO_UDP);
        if (fd < 0) {
                fail("socket: " + std::string(std::strerror(errno)));
        }
        int optname = receive ? SO_RCVBUF : SO_SNDBUF;
        if (setsockopt(fd, SOL_SOCKET, optname, &requested_buffer,
                       sizeof(requested_buffer)) != 0) {
                std::fprintf(stderr, "warning: cannot set socket buffer: %s\n",
                             std::strerror(errno));
        }
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
                close(fd);
                fail("cannot make socket nonblocking");
        }
        return fd;
}

int actual_buffer(int fd, int optname)
{
        int value = 0;
        socklen_t length = sizeof(value);
        if (getsockopt(fd, SOL_SOCKET, optname, &value, &length) != 0) {
                return -1;
        }
        return value;
}

void sleep_until(clock_type::time_point deadline)
{
        while (!stop_requested) {
                auto now = clock_type::now();
                if (now >= deadline) {
                        return;
                }
                auto remaining = deadline - now;
                if (remaining > std::chrono::microseconds(200)) {
                        std::this_thread::sleep_for(
                                remaining - std::chrono::microseconds(100));
                } else {
                        std::this_thread::yield();
                }
        }
}

void usage(FILE *out)
{
        std::fprintf(out,
                "Usage:\n"
                "  zt_udp_probe recv --bind ADDRESS:PORT [options]\n"
                "  zt_udp_probe send --target ADDRESS:PORT [options]\n\n"
                "Common options:\n"
                "  --duration SEC       Test/receive duration (default: 30/0=unlimited)\n"
                "  --buffer BYTES       Requested socket buffer (default: 16M)\n"
                "  --report SEC         Report interval (default: 1)\n\n"
                "Sender options:\n"
                "  --bind ADDRESS:PORT   Optional source address\n"
                "  --bitrate BPS        Payload bitrate (default: 60M)\n"
                "  --packet-size BYTES  UDP payload including probe header (default: 1304)\n"
                "  --pattern PATTERN    paced or frame (default: frame)\n"
                "  --fps FPS            Frame rate for frame pattern (default: 24)\n"
                "  --burst-ms MS        Spread each frame burst over this time (default: 8)\n");
}

std::string require_value(int &index, int argc, char **argv)
{
        if (++index >= argc) {
                fail(std::string("missing value after ") + argv[index - 1]);
        }
        return argv[index];
}

struct sender_options {
        std::string target;
        std::string bind_address;
        uint64_t bitrate = 60000000;
        size_t packet_size = 1304;
        double duration = 30.0;
        double report = 1.0;
        double fps = 24.0;
        double burst_ms = 8.0;
        int buffer = DEFAULT_BUFFER;
        bool frame_pattern = true;
};

struct receiver_options {
        std::string bind_address;
        double duration = 0.0;
        double report = 1.0;
        int buffer = DEFAULT_BUFFER;
};

void set_header(wire_header &header, uint64_t stream_id, uint64_t sequence,
                uint16_t flags, uint32_t frame, uint16_t packet_in_frame,
                uint16_t packets_in_frame)
{
        header.magic = htonl(MAGIC);
        header.version = htons(VERSION);
        header.flags = htons(flags);
        header.stream_id = host_to_be64(stream_id);
        header.sequence = host_to_be64(sequence);
        header.send_time_ns = host_to_be64(now_ns());
        header.frame = htonl(frame);
        header.packet_in_frame = htons(packet_in_frame);
        header.packets_in_frame = htons(packets_in_frame);
}

int run_sender(const sender_options &options)
{
        if (options.packet_size < sizeof(wire_header) ||
            options.packet_size > 65507) {
                fail("packet size must be between 40 and 65507");
        }
        if (options.bitrate == 0 || options.fps == 0 ||
            options.report == 0) {
                fail("bitrate, fps, and report interval must be nonzero");
        }

        auto destination = resolve(split_endpoint(options.target, false), false);
        int fd = make_socket(destination.family, options.buffer, false);
        if (!options.bind_address.empty()) {
                auto local =
                        resolve(split_endpoint(options.bind_address, true), true);
                if (local.family != destination.family ||
                    bind(fd, reinterpret_cast<sockaddr *>(&local.storage),
                         local.length) != 0) {
                        close(fd);
                        fail("bind failed: " + std::string(std::strerror(errno)));
                }
        }
        if (connect(fd, reinterpret_cast<sockaddr *>(&destination.storage),
                    destination.length) != 0) {
                close(fd);
                fail("connect failed: " + std::string(std::strerror(errno)));
        }

        std::vector<unsigned char> packet(options.packet_size, 0xa5);
        auto *header = reinterpret_cast<wire_header *>(packet.data());
        std::random_device random;
        uint64_t stream_id =
                (static_cast<uint64_t>(random()) << 32U) ^ random() ^ now_ns();
        uint64_t sequence = 0;
        uint64_t sent = 0;
        uint64_t bytes = 0;
        uint64_t eagain = 0;
        uint64_t enobufs = 0;
        uint64_t errors = 0;
        uint64_t interval_sent = 0;
        uint64_t interval_bytes = 0;
        uint64_t frame = 0;
        const double packet_bits = options.packet_size * 8.0;
        const double packets_per_second = options.bitrate / packet_bits;
        const uint64_t packets_per_frame = std::max<uint64_t>(
                1, std::llround(packets_per_second / options.fps));
        if (packets_per_frame > std::numeric_limits<uint16_t>::max()) {
                close(fd);
                fail("too many packets per frame");
        }

        auto start = clock_type::now();
        auto deadline = start + std::chrono::duration_cast<clock_type::duration>(
                                        std::chrono::duration<double>(
                                                options.duration));
        auto next_report =
                start + std::chrono::duration_cast<clock_type::duration>(
                                std::chrono::duration<double>(options.report));

        std::printf("send target=%s pattern=%s bitrate=%.3f Mb/s "
                    "packet=%zu pps=%.1f sndbuf=%d stream=%016llx\n",
                    options.target.c_str(),
                    options.frame_pattern ? "frame" : "paced",
                    options.bitrate / 1000000.0, options.packet_size,
                    packets_per_second, actual_buffer(fd, SO_SNDBUF),
                    static_cast<unsigned long long>(stream_id));

        auto send_one = [&](uint32_t frame_number, uint16_t in_frame,
                            uint16_t frame_packets, uint16_t flags) {
                set_header(*header, stream_id, sequence, flags, frame_number,
                           in_frame, frame_packets);
                ssize_t rc = send(fd, packet.data(), packet.size(), 0);
                if (rc == static_cast<ssize_t>(packet.size())) {
                        ++sent;
                        ++interval_sent;
                        bytes += rc;
                        interval_bytes += rc;
                        ++sequence;
                        return true;
                }
                if (rc < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                        ++eagain;
                } else if (rc < 0 && errno == ENOBUFS) {
                        ++enobufs;
                } else {
                        ++errors;
                }
                ++sequence; // A failed send becomes an explicit sequence gap.
                return false;
        };

        while (!stop_requested && clock_type::now() < deadline) {
                if (options.frame_pattern) {
                        auto frame_start =
                                start +
                                std::chrono::duration_cast<clock_type::duration>(
                                        std::chrono::duration<double>(
                                                frame / options.fps));
                        sleep_until(frame_start);
                        double spread_seconds = options.burst_ms / 1000.0;
                        for (uint64_t i = 0;
                             i < packets_per_frame && !stop_requested; ++i) {
                                if (spread_seconds > 0) {
                                        auto packet_deadline =
                                                frame_start +
                                                std::chrono::duration_cast<
                                                        clock_type::duration>(
                                                        std::chrono::duration<
                                                                double>(
                                                                spread_seconds *
                                                                i /
                                                                packets_per_frame));
                                        sleep_until(packet_deadline);
                                }
                                send_one(static_cast<uint32_t>(frame),
                                         static_cast<uint16_t>(i),
                                         static_cast<uint16_t>(
                                                 packets_per_frame),
                                         0);
                        }
                        ++frame;
                } else {
                        auto packet_deadline =
                                start +
                                std::chrono::duration_cast<clock_type::duration>(
                                        std::chrono::duration<double>(
                                                sequence /
                                                packets_per_second));
                        sleep_until(packet_deadline);
                        send_one(0, 0, 0, 0);
                }

                auto now = clock_type::now();
                if (now >= next_report) {
                        double elapsed =
                                std::chrono::duration<double>(
                                        now - (next_report -
                                               std::chrono::duration_cast<
                                                       clock_type::duration>(
                                                       std::chrono::duration<
                                                               double>(
                                                               options.report))))
                                        .count();
                        std::printf("tx %.3f Mb/s %.0f pps total=%llu "
                                    "eagain=%llu enobufs=%llu errors=%llu\n",
                                    interval_bytes * 8.0 / elapsed / 1000000.0,
                                    interval_sent / elapsed,
                                    static_cast<unsigned long long>(sent),
                                    static_cast<unsigned long long>(eagain),
                                    static_cast<unsigned long long>(enobufs),
                                    static_cast<unsigned long long>(errors));
                        interval_sent = interval_bytes = 0;
                        next_report = now +
                                      std::chrono::duration_cast<
                                              clock_type::duration>(
                                              std::chrono::duration<double>(
                                                      options.report));
                }
        }
        set_header(*header, stream_id, sequence, FLAG_FIN,
                   static_cast<uint32_t>(frame), 0, 0);
        for (int i = 0; i < 3; ++i) {
                send(fd, packet.data(), packet.size(), 0);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        double elapsed =
                std::chrono::duration<double>(clock_type::now() - start).count();
        std::printf("FINAL tx=%.3f Mb/s packets=%llu attempted=%llu "
                    "eagain=%llu enobufs=%llu errors=%llu elapsed=%.3f s\n",
                    bytes * 8.0 / elapsed / 1000000.0,
                    static_cast<unsigned long long>(sent),
                    static_cast<unsigned long long>(sequence),
                    static_cast<unsigned long long>(eagain),
                    static_cast<unsigned long long>(enobufs),
                    static_cast<unsigned long long>(errors), elapsed);
        close(fd);
        return errors == 0 && eagain == 0 && enobufs == 0 ? 0 : 2;
}

struct receive_stats {
        uint64_t stream_id = 0;
        uint64_t received = 0;
        uint64_t bytes = 0;
        uint64_t duplicates = 0;
        uint64_t reordered = 0;
        uint64_t invalid = 0;
        uint64_t highest = 0;
        uint64_t first = 0;
        bool have_sequence = false;
        uint64_t kernel_drops = 0;
        uint64_t last_arrival_ns = 0;
        uint64_t max_gap_ns = 0;
        uint64_t current_ms = 0;
        uint64_t packets_current_ms = 0;
        uint64_t max_packets_ms = 0;
        std::vector<uint64_t> seen;

        bool mark_seen(uint64_t sequence)
        {
                size_t word = sequence / 64;
                if (word >= seen.size()) {
                        seen.resize(word + 1, 0);
                }
                uint64_t bit = uint64_t{1} << (sequence % 64);
                bool duplicate = (seen[word] & bit) != 0;
                seen[word] |= bit;
                return duplicate;
        }

        uint64_t loss() const
        {
                if (!have_sequence) {
                        return 0;
                }
                uint64_t expected = highest - first + 1;
                return expected > received ? expected - received : 0;
        }
};

int run_receiver(const receiver_options &options)
{
        auto local = resolve(split_endpoint(options.bind_address, true), true);
        int fd = make_socket(local.family, options.buffer, true);
        int one = 1;
#ifdef SO_RXQ_OVFL
        if (setsockopt(fd, SOL_SOCKET, SO_RXQ_OVFL, &one, sizeof(one)) != 0) {
                std::fprintf(stderr,
                             "warning: SO_RXQ_OVFL is unavailable: %s\n",
                             std::strerror(errno));
        }
#endif
        if (bind(fd, reinterpret_cast<sockaddr *>(&local.storage),
                 local.length) != 0) {
                close(fd);
                fail("bind failed: " + std::string(std::strerror(errno)));
        }

        std::vector<unsigned char> packet(65536);
        receive_stats stats;
        uint64_t interval_received = 0;
        uint64_t interval_bytes = 0;
        auto process_start = clock_type::now();
        auto stream_start = clock_type::time_point{};
        auto last_packet = process_start;
        auto report_start = process_start;
        auto next_report =
                clock_type::time_point::max();
        auto deadline = options.duration > 0
                              ? process_start +
                                        std::chrono::duration_cast<
                                                clock_type::duration>(
                                                std::chrono::duration<double>(
                                                        options.duration))
                              : clock_type::time_point::max();
        bool final_seen = false;
        auto finish_deadline = clock_type::time_point::max();

        std::printf("recv bind=%s rcvbuf=%d\n", options.bind_address.c_str(),
                    actual_buffer(fd, SO_RCVBUF));

        while (!stop_requested && clock_type::now() < deadline &&
               clock_type::now() < finish_deadline) {
                pollfd descriptor{fd, POLLIN, 0};
                int rc = poll(&descriptor, 1, 100);
                if (rc < 0 && errno != EINTR) {
                        fail("poll failed: " + std::string(std::strerror(errno)));
                }
                if (rc > 0 && (descriptor.revents & POLLIN)) {
                        while (true) {
                                iovec iov{packet.data(), packet.size()};
                                unsigned char control[CMSG_SPACE(sizeof(uint32_t))]{};
                                msghdr message{};
                                message.msg_iov = &iov;
                                message.msg_iovlen = 1;
                                message.msg_control = control;
                                message.msg_controllen = sizeof(control);
                                ssize_t length = recvmsg(fd, &message, 0);
                                if (length < 0 &&
                                    (errno == EAGAIN ||
                                     errno == EWOULDBLOCK)) {
                                        break;
                                }
                                if (length < 0) {
                                        fail("recvmsg failed: " +
                                             std::string(std::strerror(errno)));
                                }
                                uint64_t arrival = now_ns();
                                last_packet = clock_type::now();
                                if (static_cast<size_t>(length) <
                                    sizeof(wire_header)) {
                                        ++stats.invalid;
                                        continue;
                                }
                                wire_header header{};
                                std::memcpy(&header, packet.data(),
                                            sizeof(header));
                                if (ntohl(header.magic) != MAGIC ||
                                    ntohs(header.version) != VERSION) {
                                        ++stats.invalid;
                                        continue;
                                }
                                uint64_t stream_id =
                                        be64_to_host(header.stream_id);
                                if (stats.stream_id == 0) {
                                        stats.stream_id = stream_id;
                                        stream_start = clock_type::now();
                                        report_start = stream_start;
                                        next_report =
                                                stream_start +
                                                std::chrono::duration_cast<
                                                        clock_type::duration>(
                                                        std::chrono::duration<
                                                                double>(
                                                                options.report));
                                        std::printf("stream=%016llx\n",
                                                    static_cast<unsigned long long>(
                                                            stream_id));
                                } else if (stream_id != stats.stream_id) {
                                        ++stats.invalid;
                                        continue;
                                }
                                if (ntohs(header.flags) & FLAG_FIN) {
                                        uint64_t final_sequence =
                                                be64_to_host(header.sequence);
                                        if (final_sequence > 0) {
                                                stats.highest = std::max(
                                                        stats.highest,
                                                        final_sequence - 1);
                                                stats.have_sequence = true;
                                        }
                                        if (!final_seen) {
                                                final_seen = true;
                                                finish_deadline =
                                                        clock_type::now() +
                                                        std::chrono::milliseconds(
                                                                250);
                                        }
                                        continue;
                                }
                                uint64_t sequence =
                                        be64_to_host(header.sequence);
                                if (!stats.have_sequence) {
                                        // Probe streams always start at zero, so
                                        // loss before the first received packet
                                        // remains visible.
                                        stats.first = 0;
                                        stats.highest = sequence;
                                        stats.have_sequence = true;
                                } else {
                                        if (sequence < stats.highest) {
                                                ++stats.reordered;
                                        }
                                        stats.highest =
                                                std::max(stats.highest, sequence);
                                }
                                if (stats.mark_seen(sequence)) {
                                        ++stats.duplicates;
                                        continue;
                                }
                                ++stats.received;
                                ++interval_received;
                                stats.bytes += length;
                                interval_bytes += length;

                                if (stats.last_arrival_ns != 0) {
                                        stats.max_gap_ns = std::max(
                                                stats.max_gap_ns,
                                                arrival -
                                                        stats.last_arrival_ns);
                                }
                                stats.last_arrival_ns = arrival;
                                uint64_t millisecond = arrival / 1000000;
                                if (millisecond != stats.current_ms) {
                                        stats.max_packets_ms = std::max(
                                                stats.max_packets_ms,
                                                stats.packets_current_ms);
                                        stats.current_ms = millisecond;
                                        stats.packets_current_ms = 1;
                                } else {
                                        ++stats.packets_current_ms;
                                }

#ifdef SO_RXQ_OVFL
                                for (cmsghdr *cmsg = CMSG_FIRSTHDR(&message);
                                     cmsg != nullptr;
                                     cmsg = CMSG_NXTHDR(&message, cmsg)) {
                                        if (cmsg->cmsg_level == SOL_SOCKET &&
                                            cmsg->cmsg_type == SO_RXQ_OVFL) {
                                                uint32_t drops = 0;
                                                std::memcpy(&drops,
                                                            CMSG_DATA(cmsg),
                                                            sizeof(drops));
                                                stats.kernel_drops = drops;
                                        }
                                }
#endif
                        }
                }

                auto now = clock_type::now();
                if (stats.stream_id != 0 && now >= next_report) {
                        double elapsed =
                                std::chrono::duration<double>(now - report_start)
                                        .count();
                        std::printf("rx %.3f Mb/s %.0f pps total=%llu "
                                    "loss=%llu reorder=%llu dup=%llu "
                                    "socket_drops=%llu max_gap=%.3f ms "
                                    "max_pkts/ms=%llu\n",
                                    interval_bytes * 8.0 / elapsed / 1000000.0,
                                    interval_received / elapsed,
                                    static_cast<unsigned long long>(
                                            stats.received),
                                    static_cast<unsigned long long>(stats.loss()),
                                    static_cast<unsigned long long>(
                                            stats.reordered),
                                    static_cast<unsigned long long>(
                                            stats.duplicates),
                                    static_cast<unsigned long long>(
                                            stats.kernel_drops),
                                    stats.max_gap_ns / 1000000.0,
                                    static_cast<unsigned long long>(
                                            stats.max_packets_ms));
                        interval_received = interval_bytes = 0;
                        report_start = now;
                        next_report = now +
                                      std::chrono::duration_cast<
                                              clock_type::duration>(
                                              std::chrono::duration<double>(
                                                      options.report));
                }
                if (stats.have_sequence &&
                    clock_type::now() - last_packet > std::chrono::seconds(5)) {
                        std::fprintf(stderr,
                                     "no packets for 5 seconds; waiting\n");
                        last_packet = clock_type::now();
                }
        }
        stats.max_packets_ms =
                std::max(stats.max_packets_ms, stats.packets_current_ms);
        auto measurement_start =
                stream_start == clock_type::time_point{} ? process_start
                                                         : stream_start;
        double elapsed = std::chrono::duration<double>(
                                 clock_type::now() - measurement_start)
                                 .count();
        std::printf("FINAL rx=%.3f Mb/s packets=%llu loss=%llu reorder=%llu "
                    "duplicates=%llu invalid=%llu socket_drops=%llu "
                    "max_gap=%.3f ms max_pkts/ms=%llu elapsed=%.3f s\n",
                    stats.bytes * 8.0 / elapsed / 1000000.0,
                    static_cast<unsigned long long>(stats.received),
                    static_cast<unsigned long long>(stats.loss()),
                    static_cast<unsigned long long>(stats.reordered),
                    static_cast<unsigned long long>(stats.duplicates),
                    static_cast<unsigned long long>(stats.invalid),
                    static_cast<unsigned long long>(stats.kernel_drops),
                    stats.max_gap_ns / 1000000.0,
                    static_cast<unsigned long long>(stats.max_packets_ms),
                    elapsed);
        close(fd);
        return stats.loss() == 0 && stats.kernel_drops == 0 ? 0 : 2;
}

} // namespace

int main(int argc, char **argv)
{
        try {
                if (argc < 2) {
                        usage(stderr);
                        return 1;
                }
                std::signal(SIGINT, signal_handler);
                std::signal(SIGTERM, signal_handler);
                std::string mode = argv[1];
                if (mode == "send") {
                        sender_options options;
                        for (int i = 2; i < argc; ++i) {
                                std::string arg = argv[i];
                                if (arg == "--target") {
                                        options.target =
                                                require_value(i, argc, argv);
                                } else if (arg == "--bind") {
                                        options.bind_address =
                                                require_value(i, argc, argv);
                                } else if (arg == "--bitrate") {
                                        options.bitrate = parse_scaled(
                                                require_value(i, argc, argv));
                                } else if (arg == "--packet-size") {
                                        options.packet_size = parse_scaled(
                                                require_value(i, argc, argv));
                                } else if (arg == "--duration") {
                                        options.duration = parse_double(
                                                require_value(i, argc, argv));
                                } else if (arg == "--report") {
                                        options.report = parse_double(
                                                require_value(i, argc, argv));
                                } else if (arg == "--fps") {
                                        options.fps = parse_double(
                                                require_value(i, argc, argv));
                                } else if (arg == "--burst-ms") {
                                        options.burst_ms = parse_double(
                                                require_value(i, argc, argv));
                                } else if (arg == "--buffer") {
                                        options.buffer = static_cast<int>(
                                                parse_scaled(require_value(
                                                        i, argc, argv)));
                                } else if (arg == "--pattern") {
                                        std::string value =
                                                require_value(i, argc, argv);
                                        if (value == "frame") {
                                                options.frame_pattern = true;
                                        } else if (value == "paced") {
                                                options.frame_pattern = false;
                                        } else {
                                                fail("pattern must be frame or "
                                                     "paced");
                                        }
                                } else if (arg == "--help") {
                                        usage(stdout);
                                        return 0;
                                } else {
                                        fail("unknown sender option: " + arg);
                                }
                        }
                        if (options.target.empty()) {
                                fail("--target is required");
                        }
                        return run_sender(options);
                }
                if (mode == "recv") {
                        receiver_options options;
                        for (int i = 2; i < argc; ++i) {
                                std::string arg = argv[i];
                                if (arg == "--bind") {
                                        options.bind_address =
                                                require_value(i, argc, argv);
                                } else if (arg == "--duration") {
                                        options.duration = parse_double(
                                                require_value(i, argc, argv));
                                } else if (arg == "--report") {
                                        options.report = parse_double(
                                                require_value(i, argc, argv));
                                } else if (arg == "--buffer") {
                                        options.buffer = static_cast<int>(
                                                parse_scaled(require_value(
                                                        i, argc, argv)));
                                } else if (arg == "--help") {
                                        usage(stdout);
                                        return 0;
                                } else {
                                        fail("unknown receiver option: " + arg);
                                }
                        }
                        if (options.bind_address.empty()) {
                                fail("--bind is required");
                        }
                        return run_receiver(options);
                }
                if (mode == "--help" || mode == "help") {
                        usage(stdout);
                        return 0;
                }
                fail("mode must be send or recv");
        } catch (const std::exception &error) {
                std::fprintf(stderr, "error: %s\n", error.what());
                return 1;
        }
}
