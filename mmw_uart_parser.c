// mmw_uart_win.c  (Windows / Win32 API)
// - Opens two COM ports: CFG(115200) and DATA(921600)
// - Sends cfg file line-by-line (CRLF) to CFG
// - Reads DATA, syncs on Magic Word, parses FrameHeader/TLV
// - For TLV type==1 (Detected Points), prints and UDP-sends "F<frame>,x,y,z,v\n"
//
// Usage:
//   mmw_uart_win.exe <CFG_COM> <DATA_COM> <cfg_file> [udp_ip] [udp_port]
//   ex) mmw_uart_win.exe COM23 COM24 "C:\\...\\xwr68xx_AOP_profile.cfg"
//   ex) mmw_uart_win.exe COM23 COM24 "C:\\...\\xwr68xx_AOP_profile.cfg" 127.0.0.1 9002
//
// NOTE: This expects the 44B OOB header (8B magic + 9*4B fields).

#define _CRT_SECURE_NO_WARNINGS
#define _WIN32_WINNT 0x0600

#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")

static const uint8_t MAGIC8[8] = {0x02,0x01,0x04,0x03,0x06,0x05,0x08,0x07}; // LE bytes of 0102 0304 0506 0708

#pragma pack(push,1)
typedef struct {
    // 44B header: 8B magic + nine 4B fields
    uint8_t  magic[8];
    uint32_t version;
    uint32_t totalPacketLen;
    uint32_t platform;
    uint32_t frameNumber;
    uint32_t timeCpuCycles;
    uint32_t numDetectedObj;
    uint32_t numTLVs;
    uint32_t subFrameNumber;
} FrameHeader44;

typedef struct {
    uint32_t type;
    uint32_t length; // payload length only
} TLVHeader;

typedef struct {
    float x, y, z;
    float doppler;
} DetectedPoint;
#pragma pack(pop)

// ------------------ Serial helpers ------------------
static HANDLE open_com(const char* name, DWORD baud)
{
    char path[64];
    // Win32: need "\\.\COMx" for COM>9
    snprintf(path, sizeof(path), "\\\\.\\%s", name);
    HANDLE h = CreateFileA(path, GENERIC_READ|GENERIC_WRITE, 0, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (h==INVALID_HANDLE_VALUE) return h;

    DCB dcb = {0};
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(h, &dcb)) { CloseHandle(h); return INVALID_HANDLE_VALUE; }
    dcb.BaudRate = baud;
    dcb.ByteSize = 8;
    dcb.Parity   = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fOutxCtsFlow = FALSE; dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl  = DTR_CONTROL_DISABLE;
    dcb.fRtsControl  = RTS_CONTROL_DISABLE;
    dcb.fOutX = dcb.fInX = FALSE;
    if (!SetCommState(h, &dcb)) { CloseHandle(h); return INVALID_HANDLE_VALUE; }

    COMMTIMEOUTS to = {0};
    to.ReadIntervalTimeout         = 5;
    to.ReadTotalTimeoutConstant    = 5;
    to.ReadTotalTimeoutMultiplier  = 0;
    to.WriteTotalTimeoutConstant   = 100;
    to.WriteTotalTimeoutMultiplier = 0;
    SetCommTimeouts(h, &to);

    SetupComm(h, 1<<20, 1<<20);
    PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);
    return h;
}

static bool write_line_crlf(HANDLE h, const char* line)
{
    DWORD wr=0;
    size_t n = strlen(line);
    // trim trailing \r?\n
    while (n && (line[n-1]=='\n' || line[n-1]=='\r')) n--;

    if (n) {
        if (!WriteFile(h, line, (DWORD)n, &wr, NULL)) return false;
        if (wr != n) return false;
    }
    const char crlf[2] = {'\r','\n'};
    if (!WriteFile(h, crlf, 2, &wr, NULL)) return false;
    return wr==2;
}

static bool send_cfg_file(HANDLE h_cfg, const char* cfg_path)
{
    FILE* fp = fopen(cfg_path, "rb");
    if (!fp) { fprintf(stderr, "cfg open fail: %s\n", cfg_path); return false; }
    char buf[2048];
    while (fgets(buf, sizeof(buf), fp)) {
        // skip obvious comments if present
        const char* p = buf;
        while (*p==' '||*p=='\t') p++;
        if (*p=='%' || *p=='#' || (*p=='/' && *(p+1)=='/')) { continue; }

        if (!write_line_crlf(h_cfg, buf)) { fclose(fp); return false; }
        // small delay; device may need a breath per line
        Sleep(5);
    }
    fclose(fp);
    return true;
}

static bool read_exact(HANDLE h, void* out, DWORD n)
{
    uint8_t* p = (uint8_t*)out;
    DWORD got=0, rd=0;
    while (got < n) {
        if (!ReadFile(h, p+got, n-got, &rd, NULL)) return false;
        if (rd==0) Sleep(1);
        got += rd;
    }
    return true;
}

static bool find_magic(HANDLE h)
{
    uint8_t w[8]={0};
    DWORD rd=0;
    size_t filled=0;
    for (;;) {
        if (!ReadFile(h, w+filled, 1, &rd, NULL)) return false;
        if (rd==0) continue;
        filled++;
        if (filled>=8) {
            if (memcmp(w, MAGIC8, 8)==0) return true;
            memmove(w, w+1, 7); // shift
            filled=7;
        }
    }
    // unreachable
}

// ------------------ main ------------------
int main(int argc, char** argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: %s <CFG_COM> <DATA_COM> <cfg_file> [udp_ip] [udp_port]\n", argv[0]);
        fprintf(stderr, " ex) %s COM23 COM24 xwr68xx_AOP_profile.cfg\n", argv[0]);
        fprintf(stderr, " ex) %s COM23 COM24 xwr68xx_AOP_profile.cfg 127.0.0.1 9002\n", argv[0]);
        return 1;
    }
    const char* cfg_com  = argv[1];
    const char* data_com = argv[2];
    const char* cfg_file = argv[3];

    // UDP defaults
    const char* udp_ip   = (argc >= 5) ? argv[4] : "127.0.0.1";
    int         udp_port = (argc >= 6) ? atoi(argv[5]) : 9001;

    // Open serials
    HANDLE h_cfg  = open_com(cfg_com, 115200);
    if (h_cfg==INVALID_HANDLE_VALUE) {
        DWORD e = GetLastError();
        fprintf(stderr, "open %s fail (err=%lu)\n", cfg_com, (unsigned long)e);
        return 2;
    }
    HANDLE h_data = open_com(data_com, 921600);
    if (h_data==INVALID_HANDLE_VALUE) {
        DWORD e = GetLastError();
        fprintf(stderr, "open %s fail (err=%lu)\n", data_com, (unsigned long)e);
        CloseHandle(h_cfg);
        return 3;
    }

    // Init UDP
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        CloseHandle(h_cfg); CloseHandle(h_data);
        return 4;
    }
    SOCKET usock = socket(AF_INET, SOCK_DGRAM, 0);
    if (usock == INVALID_SOCKET) {
        fprintf(stderr, "socket() failed\n");
        CloseHandle(h_cfg); CloseHandle(h_data);
        WSACleanup();
        return 5;
    }
    struct sockaddr_in dst; memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port   = htons((u_short)udp_port);
    unsigned long ia = inet_addr(udp_ip);  // IPv4 only
    if (ia == INADDR_NONE) {
        fprintf(stderr, "invalid IPv4: %s\n", udp_ip);
        closesocket(usock); CloseHandle(h_cfg); CloseHandle(h_data); WSACleanup();
        return 6;
    }
dst.sin_addr.s_addr = ia;


    printf("[CFG] sending cfg: %s\n", cfg_file);
    if (!send_cfg_file(h_cfg, cfg_file)) {
        fprintf(stderr, "cfg send failed\n");
        closesocket(usock); WSACleanup();
        CloseHandle(h_cfg); CloseHandle(h_data);
        return 7;
    }
    printf("[CFG] done. listening DATA…  (UDP -> %s:%d)\n", udp_ip, udp_port);

    // Main loop
    for (;;) {
        // 1) magic sync
        if (!find_magic(h_data)) { fprintf(stderr, "sync fail\n"); break; }

        // 2) read rest of header (we already consumed magic)
        FrameHeader44 fh;
        memcpy(fh.magic, MAGIC8, 8);
        if (!read_exact(h_data, ((uint8_t*)&fh)+8, sizeof(fh)-8)) break;

        // 3) TLVs
        uint32_t bytes_left = 0;
        if (fh.totalPacketLen < sizeof(fh)) {
            // corrupted packet; resync
            continue;
        }
        bytes_left = fh.totalPacketLen - sizeof(fh);

        for (uint32_t i=0; i<fh.numTLVs && bytes_left >= sizeof(TLVHeader); i++) {
            TLVHeader th;
            if (!read_exact(h_data, &th, sizeof(th))) goto CLEANUP;
            if (bytes_left < sizeof(th)) goto CLEANUP;
            bytes_left -= sizeof(th);

            if (th.type == 1) {
                uint32_t n = th.length / sizeof(DetectedPoint);
                for (uint32_t k=0; k<n; k++) {
                    DetectedPoint p;
                    if (!read_exact(h_data, &p, sizeof(p))) goto CLEANUP;
                    printf("F%u N%u: x=%.3f y=%.3f z=%.3f v=%.3f\n",
                           fh.frameNumber, k, p.x, p.y, p.z, p.doppler);

                    // ---- UDP send line ----
                    char line[256];
                    int len = snprintf(line, sizeof(line),
                        "F%u,%.3f,%.3f,%.3f,%.3f\n",
                        fh.frameNumber, p.x, p.y, p.z, p.doppler);
                    if (len > 0) {
                        sendto(usock, line, len, 0,
                               (struct sockaddr*)&dst, sizeof(dst));
                    }
                }
                if (bytes_left < th.length) goto CLEANUP;
                bytes_left -= th.length;
            } else {
                // skip other TLVs
                const uint32_t CHUNK = 1024;
                uint8_t dump[CHUNK];
                uint32_t left = th.length;
                while (left) {
                    uint32_t c = left > CHUNK ? CHUNK : left;
                    if (!read_exact(h_data, dump, c)) goto CLEANUP;
                    left -= c;
                }
                if (bytes_left < th.length) goto CLEANUP;
                bytes_left -= th.length;
            }
        }

        // 4) if extra padding remains (32B align etc.), skip it
        if (bytes_left) {
            uint8_t pad[64];
            while (bytes_left) {
                uint32_t c = bytes_left > sizeof(pad) ? sizeof(pad) : bytes_left;
                if (!read_exact(h_data, pad, c)) goto CLEANUP;
                bytes_left -= c;
            }
        }
    }

CLEANUP:
    closesocket(usock);
    WSACleanup();
    CloseHandle(h_cfg);
    CloseHandle(h_data);
    return 0;
}
