// IperfNode Example - EnigmaNG
// Mesh node (ESP32) that performs an iperf2-compatible TCP or UDP throughput
// test against a remote server to measure end-to-end bandwidth through the mesh.
//
// ─── Server setup (run on a PC / Linux box reachable from the mesh) ──────────
//   TCP mode:  iperf -s -i 1              # iperf2 TCP, port 5001
//   UDP mode:  iperf -s -u -i 1           # iperf2 UDP, port 5001
//
// ─── Select mode ─────────────────────────────────────────────────────────────
//   Set USE_UDP = false  →  TCP upload test
//   Set USE_UDP = true   →  UDP upload test at UDP_TARGET_KBPS
//
// ─── Protocol note ───────────────────────────────────────────────────────────
//   ESP-IDF ships its own iperf component but it is not available under the
//   Arduino framework without a CMake/IDF build.  This sketch reimplements the
//   iperf2 TCP and UDP *client* sides using raw lwIP sockets (Arduino-ESP32
//   >= 2.x / IDF >= 4.4) so no extra library is needed.
//
//   iperf3 was not chosen because it uses a JSON control channel + a separate
//   data channel, making client-side reimplementation significantly harder.
//
// Requires a running EnigmaNG gateway with uplink and NAT enabled.

#include <Arduino.h>
#include <MeshNetwork.h>
#include <lwip/sockets.h>
#include <lwip/inet.h>
#include <fcntl.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

MeshNetwork mesh;

// ─── CONFIGURATION ──────────────────────────────────────────────
// Pre-Shared Key (must be the same on all nodes in the network)
const char* PSK = "MySecretMeshKey123";

// Channel (must match the gateway)
const uint8_t CHANNEL = 6;

// iperf2 server IP (reachable from the mesh — LAN host or gateway IP)
const char* IPERF_SERVER_IP = "192.168.5.219";

// iperf2 port (default 5001 for both TCP and UDP)
const uint16_t IPERF_PORT = 5001;

// Duration of each throughput test (seconds)
const uint32_t TEST_DURATION_SEC = 10;

// How often to repeat the test (ms); 0 = run once and stop
const uint32_t REPEAT_INTERVAL_MS = 30000;

// ── Mode selection ───────────────────────────────────────────────
// false = TCP upload test
// true  = UDP upload test (requires iperf -s -u on the server)
const bool USE_UDP = true;

// ── TCP parameters ───────────────────────────────────────────────
// Send buffer = MSS = MTU(216) - IP(20) - TCP(20) = 176 bytes.
// One send() maps to one TCP segment that fits in one ESP-NOW frame.
const size_t TCP_SEND_BUF = 176;

// Receive window sized to the bandwidth-delay product:
//   BDP ≈ 250 kbps × 27 ms RTT ≈ 844 B  →  8 × MSS = 1408 B.
const int TCP_RECV_WINDOW = 8 * 176;  // 1408 bytes

// ── UDP parameters ───────────────────────────────────────────────
// Datagram payload = MTU(216) - IP(20) - UDP(8) = 188 bytes.
// Includes the 12-byte iperf2 UDP header, so net user data = 176 B.
const size_t UDP_DATAGRAM_SIZE = 188;

// Target send rate in kbit/s.  Keep below the actual link capacity
// (~250 kbps) to avoid flooding the ESP-NOW queue.
const uint32_t UDP_TARGET_KBPS = 200;
// ────────────────────────────────────────────────────────────────

// iperf2 TCP client handshake header (big-endian / network byte order).
// Reference: iperf2 source, include/Settings.hpp + lib/Client.cpp
#pragma pack(push, 1)
struct IperfClientHdr {
    int32_t flags;       // HEADER_VERSION1 = 0x80000000
    int32_t numThreads;  // parallel streams (always 1 here)
    int32_t mPort;       // server port (echoed back)
    int32_t bufferlen;   // 0 = server uses its own default
    int32_t mWinBand;    // TCP window / UDP bandwidth; 0 = OS default
    int32_t mAmount;     // >0: bytes to send; <0: –(duration × 100)
                         // e.g. 10 s → mAmount = –(10 × 100) = –1000
};

// iperf2 UDP datagram header — prepended to every UDP payload.
// Reference: iperf2 source, include/payloads.h (struct UDP_datagram)
struct IperfUdpHdr {
    int32_t  id;      // sequence number; negative on the last datagram
    uint32_t tv_sec;  // send timestamp — seconds
    uint32_t tv_usec; // send timestamp — microseconds
};
#pragma pack(pop)

// Re-used send buffer sized to the larger of the two payload sizes.
static_assert(UDP_DATAGRAM_SIZE >= TCP_SEND_BUF, "buffer size mismatch");
static uint8_t sTxBuf[UDP_DATAGRAM_SIZE];

// ─── Shared result printer ───────────────────────────────────────

static void printResult(uint64_t totalBytes, uint32_t elapsedMs, const char* proto) {
    const float secs = (float)elapsedMs / 1000.0f;
    const float mbps = (secs > 0.0f) ? ((float)totalBytes * 8.0f / 1e6f / secs) : 0.0f;
    Serial.printf("[iperf] ── %s Result ──────────────────────────\n", proto);
    Serial.printf("[iperf]   Transferred : %.2f MB\n",    (float)totalBytes / 1e6f);
    Serial.printf("[iperf]   Duration    : %.1f s\n",     secs);
    Serial.printf("[iperf]   Throughput  : %.3f Mbit/s\n", mbps);
    Serial.printf("[iperf] ─────────────────────────────────────────\n");
}

// ─── iperf2 TCP client ────────────────────────────────────────────

// Non-blocking TCP upload.  The socket is O_NONBLOCK from creation so that
// connect() and send() never suspend the FreeRTOS task indefinitely — a
// blocking lwIP socket in a low-priority task is never woken up when the
// tcpip task is busy, causing the hang seen on the 2nd run.
static float runIperfTcpTest() {
    struct sockaddr_in serverAddr = {};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port   = htons(IPERF_PORT);
    if (inet_pton(AF_INET, IPERF_SERVER_IP, &serverAddr.sin_addr) != 1) {
        Serial.printf("[iperf] Invalid server address: %s\n", IPERF_SERVER_IP);
        return -1.0f;
    }

    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) {
        Serial.printf("[iperf/tcp] socket() failed: errno=%d\n", errno);
        return -2.0f;
    }

    // Advertise the BDP-sized receive window in the SYN.
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &TCP_RECV_WINDOW, sizeof(TCP_RECV_WINDOW));
    fcntl(sock, F_SETFL, O_NONBLOCK);

    // ── Non-blocking connect ──────────────────────────────────────
    Serial.printf("[iperf/tcp] Connecting to %s:%u …\n", IPERF_SERVER_IP, IPERF_PORT);
    int rc = connect(sock, (struct sockaddr*)&serverAddr, sizeof(serverAddr));
    if (rc != 0 && errno != EINPROGRESS) {
        Serial.printf("[iperf/tcp] connect() failed: errno=%d\n", errno);
        close(sock);
        return -3.0f;
    }
    {
        fd_set wfds, efds;
        FD_ZERO(&wfds); FD_SET(sock, &wfds);
        FD_ZERO(&efds); FD_SET(sock, &efds);
        struct timeval tvConn = { 10, 0 };
        if (select(sock + 1, nullptr, &wfds, &efds, &tvConn) <= 0) {
            Serial.println("[iperf/tcp] connect() timed out");
            close(sock); return -3.0f;
        }
        int sockerr = 0; socklen_t optlen = sizeof(sockerr);
        getsockopt(sock, SOL_SOCKET, SO_ERROR, &sockerr, &optlen);
        if (sockerr != 0) {
            Serial.printf("[iperf/tcp] connect() SO_ERROR=%d\n", sockerr);
            close(sock); return -3.0f;
        }
    }
    Serial.println("[iperf/tcp] Connected — sending client header");

    // ── iperf2 handshake header ───────────────────────────────────
    IperfClientHdr hdr = {};
    hdr.flags      = htonl(0x80000000);
    hdr.numThreads = htonl(1);
    hdr.mPort      = htonl(IPERF_PORT);
    hdr.bufferlen  = htonl(0);
    hdr.mWinBand   = htonl(0);
    hdr.mAmount    = htonl(-(int32_t)(TEST_DURATION_SEC * 100));
    {
        const uint8_t* p = (const uint8_t*)&hdr;
        size_t rem = sizeof(hdr);
        const uint32_t hdrDl = millis() + 5000;
        while (rem > 0 && millis() < hdrDl) {
            fd_set wfds; FD_ZERO(&wfds); FD_SET(sock, &wfds);
            struct timeval tv = { 0, 10000 };
            if (select(sock + 1, nullptr, &wfds, nullptr, &tv) > 0) {
                int n = send(sock, p, rem, MSG_DONTWAIT);
                if (n > 0) { p += n; rem -= (size_t)n; }
                else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                    Serial.printf("[iperf/tcp] header send error: errno=%d\n", errno);
                    close(sock); return -4.0f;
                }
            }
            vTaskDelay(1);
        }
        if (rem > 0) {
            Serial.println("[iperf/tcp] header send timeout");
            close(sock); return -4.0f;
        }
    }

    // ── Data phase ───────────────────────────────────────────────
    for (size_t i = 0; i < TCP_SEND_BUF; i++) sTxBuf[i] = (uint8_t)('0' + (i % 10));

    Serial.printf("[iperf/tcp] Sending for %u s …\n", TEST_DURATION_SEC);
    Serial.printf("[iperf/tcp] %4s  %10s  %10s\n", "Sec", "Bytes", "Kbit/s");

    const uint32_t t0       = millis();
    const uint32_t deadline = t0 + TEST_DURATION_SEC * 1000UL;
    uint64_t totalSent = 0, intervalSent = 0;
    uint32_t nextReportMs = t0 + 1000UL, intervalNum = 0;

    while (millis() < deadline) {
        fd_set wfds; FD_ZERO(&wfds); FD_SET(sock, &wfds);
        struct timeval tv = { 0, 10000 };
        int ready = select(sock + 1, nullptr, &wfds, nullptr, &tv);
        if (ready < 0) { Serial.printf("[iperf/tcp] select() errno=%d\n", errno); break; }
        if (ready > 0 && FD_ISSET(sock, &wfds)) {
            int n = send(sock, sTxBuf, TCP_SEND_BUF, MSG_DONTWAIT);
            if (n > 0)                                           { totalSent += n; intervalSent += n; }
            else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                Serial.printf("[iperf/tcp] send() errno=%d\n", errno); break;
            }
        }
        uint32_t now = millis();
        if (now >= nextReportMs) {
            Serial.printf("[iperf/tcp] %4u  %10llu  %10.1f\n",
                          ++intervalNum, intervalSent, (float)intervalSent * 8.0f / 1000.0f);
            intervalSent = 0; nextReportMs += 1000UL;
        }
        vTaskDelay(1);
    }
    const uint32_t elapsed = millis() - t0;
    close(sock);
    printResult(totalSent, elapsed, "TCP");
    return (elapsed > 0) ? ((float)totalSent * 8.0f / 1e6f / ((float)elapsed / 1000.0f)) : 0.0f;
}

// ─── iperf2 UDP client ────────────────────────────────────────────

// Sends iperf2-compatible UDP datagrams to IPERF_SERVER_IP:IPERF_PORT at
// UDP_TARGET_KBPS for TEST_DURATION_SEC seconds, then sends a final datagram
// with a negative sequence number to tell the server the stream has ended.
// Server command: iperf -s -u -i 1
static float runIperfUdpTest() {
    struct sockaddr_in serverAddr = {};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port   = htons(IPERF_PORT);
    if (inet_pton(AF_INET, IPERF_SERVER_IP, &serverAddr.sin_addr) != 1) {
        Serial.printf("[iperf/udp] Invalid server address: %s\n", IPERF_SERVER_IP);
        return -1.0f;
    }

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        Serial.printf("[iperf/udp] socket() failed: errno=%d\n", errno);
        return -2.0f;
    }
    fcntl(sock, F_SETFL, O_NONBLOCK);

    // Inter-packet gap in microseconds to hit UDP_TARGET_KBPS.
    //   gap_us = (datagram_bytes × 8 × 1 000 000) / (target_kbps × 1000)
    const uint32_t gapUs = (uint32_t)((uint64_t)UDP_DATAGRAM_SIZE * 8UL * 1000UL
                                       / UDP_TARGET_KBPS);

    Serial.printf("[iperf/udp] Target %u kbps  →  %u us/datagram (%u B)\n",
                  UDP_TARGET_KBPS, gapUs, (unsigned)UDP_DATAGRAM_SIZE);
    Serial.printf("[iperf/udp] Sending to %s:%u for %u s …\n",
                  IPERF_SERVER_IP, IPERF_PORT, TEST_DURATION_SEC);
    Serial.printf("[iperf/udp] %4s  %10s  %10s  %8s\n",
                  "Sec", "Datagrams", "Bytes", "Kbit/s");

    // Pre-fill payload bytes after the header with printable ASCII.
    for (size_t i = sizeof(IperfUdpHdr); i < UDP_DATAGRAM_SIZE; i++) {
        sTxBuf[i] = (uint8_t)('0' + (i % 10));
    }

    const uint32_t t0       = millis();
    const uint32_t deadline = t0 + TEST_DURATION_SEC * 1000UL;
    uint64_t totalSent = 0, intervalSent = 0;
    uint32_t totalPkts = 0, intervalPkts = 0;
    uint32_t nextReportMs = t0 + 1000UL, intervalNum = 0;
    int32_t  seqNum = 0;
    uint32_t nextSendUs = (uint32_t)(esp_timer_get_time()); // microsecond clock

    while (millis() < deadline) {
        uint32_t nowUs = (uint32_t)(esp_timer_get_time());

        if ((int32_t)(nowUs - nextSendUs) >= 0) {
            // Fill iperf2 UDP header at the start of the buffer.
            IperfUdpHdr* udpHdr = (IperfUdpHdr*)sTxBuf;
            udpHdr->id      = htonl(seqNum);
            udpHdr->tv_sec  = htonl((uint32_t)(millis() / 1000));
            udpHdr->tv_usec = htonl((uint32_t)((millis() % 1000) * 1000));

            int n = sendto(sock, sTxBuf, UDP_DATAGRAM_SIZE, MSG_DONTWAIT,
                           (struct sockaddr*)&serverAddr, sizeof(serverAddr));
            if (n > 0) {
                seqNum++;
                totalSent    += (uint64_t)n;
                intervalSent += (uint64_t)n;
                totalPkts++;
                intervalPkts++;
                nextSendUs += gapUs;
            } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                Serial.printf("[iperf/udp] sendto() errno=%d\n", errno);
                break;
            }
        } else {
            vTaskDelay(1); // yield while waiting for next send slot
        }

        uint32_t now = millis();
        if (now >= nextReportMs) {
            Serial.printf("[iperf/udp] %4u  %10u  %10llu  %8.1f\n",
                          ++intervalNum, intervalPkts, intervalSent,
                          (float)intervalSent * 8.0f / 1000.0f);
            intervalSent = 0; intervalPkts = 0;
            nextReportMs += 1000UL;
        }
    }

    // ── End-of-stream: send the last datagram with a negative id ──
    // iperf2 server waits for this to print its report.
    for (int i = 0; i < 3; i++) {  // send 3 times in case of loss
        IperfUdpHdr* udpHdr = (IperfUdpHdr*)sTxBuf;
        udpHdr->id      = htonl(-seqNum);  // negative flags end of stream
        udpHdr->tv_sec  = htonl((uint32_t)(millis() / 1000));
        udpHdr->tv_usec = htonl((uint32_t)((millis() % 1000) * 1000));
        sendto(sock, sTxBuf, UDP_DATAGRAM_SIZE, 0,
               (struct sockaddr*)&serverAddr, sizeof(serverAddr));
        vTaskDelay(100);
    }

    const uint32_t elapsed = millis() - t0;
    close(sock);

    Serial.printf("[iperf/udp] Total datagrams sent: %u\n", totalPkts);
    printResult(totalSent, elapsed, "UDP");
    return (elapsed > 0) ? ((float)totalSent * 8.0f / 1e6f / ((float)elapsed / 1000.0f)) : 0.0f;
}

// ─── Dispatcher ───────────────────────────────────────────────────

static float runIperfTest() {
    return USE_UDP ? runIperfUdpTest() : runIperfTcpTest();
}

// ─── FreeRTOS task wrapper ────────────────────────────────────────

static TaskHandle_t sIperfTask = nullptr;

// The task runs one iperf test and then deletes itself.
// Stack of 6 KB: sTxBuf is file-scope, but the task needs space for the
// socket structs, local variables and the Serial printf formatting chain.
static void iperfTaskFn(void* /*arg*/) {
    runIperfTest();
    sIperfTask = nullptr;
    vTaskDelete(nullptr);
}

// Spawn the task if not already running.
static void startIperfTask() {
    if (sIperfTask != nullptr) {
        Serial.println("[iperf] Test already in progress — skipping");
        return;
    }
    xTaskCreate(iperfTaskFn,
                "iperf_client",
                6144,          // stack in bytes
                nullptr,
                1,             // priority (below WiFi/lwIP at 23, above idle at 0)
                &sIperfTask);
}

// ─── Arduino entry points ─────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n[IperfNode] Starting …");
    Serial.printf("[IperfNode] Mode    : %s\n", USE_UDP ? "UDP" : "TCP");
    Serial.printf("[IperfNode] Server  : %s:%u\n", IPERF_SERVER_IP, IPERF_PORT);
    Serial.printf("[IperfNode] Duration: %u s   Repeat: %u ms\n",
                  TEST_DURATION_SEC, REPEAT_INTERVAL_MS);
    if (USE_UDP) {
        Serial.printf("[IperfNode] Target  : %u kbps  (%u B/datagram)\n",
                      UDP_TARGET_KBPS, (unsigned)UDP_DATAGRAM_SIZE);
    }

    mesh.setChannel(CHANNEL);
    if (!mesh.begin(PSK, MESH_NODE)) {
        Serial.println("[IperfNode] mesh.begin() failed — halting");
        while (true) delay(1000);
    }

    Serial.println("[IperfNode] Mesh started, waiting for mesh IP …");
}

static uint32_t sLastTestMs = 0;
static bool     sFirstRun   = true;

void loop() {
    mesh.loop();

    if (!mesh.isConnected()) return;

    const uint32_t now = millis();

    const bool timeToRun = sFirstRun ||
                           (REPEAT_INTERVAL_MS > 0 &&
                            (now - sLastTestMs) >= REPEAT_INTERVAL_MS);

    if (!timeToRun) return;

    sFirstRun   = false;
    sLastTestMs = now;

    Serial.printf("\n[IperfNode] Local IP : %s\n", mesh.getLocalIP().toString().c_str());
    Serial.printf("[IperfNode] Gateway  : %s\n",   mesh.getGatewayIP().toString().c_str());
    Serial.printf("[IperfNode] RSSI GW  : %d dBm\n", mesh.getRssiFromGateway());

    startIperfTask();
}
