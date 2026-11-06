#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h> // Windows API for Serial Communication

// --- 1. 설정 및 상수 ---
#define MAGIC_WORD 0xDEADBEEF // TI mmWave 레이더 패킷 시작 매직 워드
#define HEADER_SIZE 40        // MmwDemo_Header의 크기 (TI SDK v3.x 기준 40 bytes)
#define MAX_BUFFER_SIZE 4096  // UART 수신 버퍼 최대 크기

// ----------------------------------------------------------------------
// 2. 구조체 정의 (바이트 정렬 필수)
// ----------------------------------------------------------------------
#pragma pack(1)

// MmwDemo_Header (40 bytes)
typedef struct {
    uint32_t magicWord;
    uint32_t version;
    uint32_t totalLength;   // 전체 패킷 길이
    uint32_t frameNumber;
    uint32_t platform;
    uint32_t timeCpuCycles;
    uint16_t numDetectedObj; // 검출된 객체 수
    uint16_t numTLV;         // 뒤따르는 TLV 블록 수
    uint32_t subFrameNum;
} MmwDemo_Header;

// Type 1: Point Cloud의 개별 점(Point) 데이터 구조 (8 bytes)
typedef struct {
    int16_t x;
    int16_t y;
    int16_t z;
    int16_t velocity;
} PointCloud_t;

// Type 7: SNR/Noise 정보의 개별 항목 (Point당 4 bytes 가정)
typedef struct {
    int16_t snr;    // SNR 값
    int16_t noise;  // Noise 값
} SNRInfo_t;

// TLV 블록 헤더 (8 bytes)
typedef struct {
    uint32_t type;
    uint32_t length;
} TLV_Header;

#pragma pack() 
// ----------------------------------------------------------------------


// ----------------------------------------------------------------------
// 3. 엔디안 변환 함수 (현재 Little Endian 가정으로 주석 처리됨)
// ----------------------------------------------------------------------
/*
uint32_t swap_endian32(uint32_t val) {
    return (val << 24) | ((val << 8) & 0x00FF0000) |
           ((val >> 8) & 0x0000FF00) | (val >> 24);
}

uint16_t swap_endian16(uint16_t val) {
    return (val << 8) | (val >> 8);
}
*/


// ----------------------------------------------------------------------
// 4. TLV 파싱 함수 (Type 1과 Type 7 처리 포함)
// ----------------------------------------------------------------------
void parse_tlv_data(const uint8_t* packet_buffer, uint32_t total_length) {

    const uint8_t* rx_buffer_ptr = packet_buffer + sizeof(MmwDemo_Header);
    const MmwDemo_Header* header = (const MmwDemo_Header*)packet_buffer;

    uint16_t numTLV = header->numTLV;

    // Total Length는 파싱 로직에서 이미 검증되었으므로 그대로 사용합니다.
    printf("\n--- TLV Frame Start --- (Total Length: %u, Num TLV: %u)\n", total_length, numTLV);

    for (int i = 0; i < numTLV; i++) {
        const TLV_Header* tlv_h = (const TLV_Header*)rx_buffer_ptr;

        // 현재 Little Endian 가정이므로 엔디안 변환 함수(swap_endian)를 사용하지 않습니다.
        uint32_t type = tlv_h->type;
        uint32_t length = tlv_h->length;

        printf("[TLV #%d] Type: %u, Length: %u bytes\n", i + 1, type, length);

        const uint8_t* tlv_value_ptr = rx_buffer_ptr + sizeof(TLV_Header);

        if (type == 1) { // Type 1: Point Cloud (X, Y, Z, V)
            size_t num_points = length / sizeof(PointCloud_t);
            const PointCloud_t* points_array = (const PointCloud_t*)tlv_value_ptr;

            for (size_t j = 0; j < (num_points > 5 ? 5 : num_points); j++) {
                printf("  -> Point %zu: X=%d, Y=%d, Z=%d, V=%d\n", j,
                    points_array[j].x, points_array[j].y, points_array[j].z, points_array[j].velocity);
            }
            if (num_points > 5) printf("  ...and %zu more points (showing only 5)\n", num_points - 5);
        }
        else if (type == 7) { // Type 7: SNR Info (SNR, Noise)
            size_t num_snr_items = length / sizeof(SNRInfo_t);
            const SNRInfo_t* snr_array = (const SNRInfo_t*)tlv_value_ptr;

            printf("  -> SNR Info received (Items: %zu)\n", num_snr_items);

            for (size_t j = 0; j < (num_snr_items > 5 ? 5 : num_snr_items); j++) {
                printf("    SNR Item %zu: SNR=%d, Noise=%d\n", j,
                    snr_array[j].snr, snr_array[j].noise);
            }
            if (num_snr_items > 5) printf("    ...and %zu more SNR items (showing only 5)\n", num_snr_items - 5);
        }

        // 다음 TLV 블록으로 포인터 이동
        rx_buffer_ptr += sizeof(TLV_Header) + length;

        if (rx_buffer_ptr > packet_buffer + total_length) {
            printf("Error: TLV structure exceeded total length.\n");
            break;
        }
    }
    printf("--- TLV Frame End ---\n");
}


// ----------------------------------------------------------------------
// 5. UART 설정 및 수신 로직 (Windows API)
// ----------------------------------------------------------------------
HANDLE open_and_configure_serial_port(const char* port_name) {
    // Port Opening (BaudRate: 115200)
    HANDLE hSerial = CreateFileA(
        port_name,
        GENERIC_READ | GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL
    );
    if (hSerial == INVALID_HANDLE_VALUE) { printf("Error opening serial port %s\n", port_name); return INVALID_HANDLE_VALUE; }

    DCB dcbSerialParams = { 0 };
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
    if (!GetCommState(hSerial, &dcbSerialParams)) { CloseHandle(hSerial); return INVALID_HANDLE_VALUE; }

    dcbSerialParams.BaudRate = 115200; // 확정된 Baud Rate
    dcbSerialParams.ByteSize = 8;
    dcbSerialParams.StopBits = ONESTOPBIT;
    dcbSerialParams.Parity = NOPARITY;
    if (!SetCommState(hSerial, &dcbSerialParams)) { CloseHandle(hSerial); return INVALID_HANDLE_VALUE; }

    COMMTIMEOUTS timeouts = { 0 };
    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutConstant = 50;
    timeouts.ReadTotalTimeoutMultiplier = 10;
    SetCommTimeouts(hSerial, &timeouts);

    return hSerial;
}

uint8_t rx_buffer[MAX_BUFFER_SIZE];
size_t buffer_index = 0;

int main() {
    // ❗❗❗ 사용할 실제 COM 포트 번호로 변경하세요. (예: COM3, COM10 등)
    const char* port_name = "\\\\.\\COM3";

    HANDLE hSerial = open_and_configure_serial_port(port_name);

    if (hSerial == INVALID_HANDLE_VALUE) {
        printf("Failed to initialize serial communication.\n");
        return 1;
    }
    printf("Serial port %s opened successfully at 115200 bps. Waiting for data...\n", port_name);

    DWORD bytesRead;

    while (1) {
        // 1. UART로부터 새로운 데이터 읽기
        ReadFile(hSerial, rx_buffer + buffer_index, MAX_BUFFER_SIZE - buffer_index, &bytesRead, NULL);
        if (bytesRead > 0) {
            buffer_index += bytesRead;
        }

        // 2. 버퍼에서 Magic Word를 찾아 프레임 동기화 시도
        size_t search_offset = 0;

        while (buffer_index >= HEADER_SIZE) {

            // Magic Word 검색 (Little Endian 가정)
            uint32_t current_magic = *(uint32_t*)(rx_buffer + search_offset);

            if (current_magic == MAGIC_WORD) {
                // Magic Word 발견!
                MmwDemo_Header* header = (MmwDemo_Header*)(rx_buffer + search_offset);

                // Total Length 읽기 (Little Endian 가정)
                uint32_t total_length = header->totalLength;

                // 3. 패킷 완성 여부 확인
                if (buffer_index >= search_offset + total_length) {
                    // **패킷이 완전히 도착함!**

                    // TLV 파싱 함수 호출
                    parse_tlv_data(rx_buffer + search_offset, total_length);

                    // 파싱 완료된 데이터(패킷)를 버퍼에서 제거하고 나머지 데이터 앞으로 이동
                    size_t packet_size = search_offset + total_length;
                    size_t remaining_data = buffer_index - packet_size;
                    memmove(rx_buffer, rx_buffer + packet_size, remaining_data);
                    buffer_index = remaining_data;
                    search_offset = 0; // 버퍼 시작부터 다시 동기화 시도
                }
                else {
                    // 패킷이 아직 덜 도착함. 다음 ReadFile을 기다립니다.
                    break;
                }
            }
            else {
                // Magic Word가 아님. 1바이트씩 이동하여 다음 Magic Word를 검색
                search_offset++;

                // 버퍼에 데이터가 남아있지 않으면 종료
                if (search_offset > buffer_index - 4) {
                    size_t remaining_data = buffer_index - search_offset;
                    memmove(rx_buffer, rx_buffer + search_offset, remaining_data);
                    buffer_index = remaining_data;
                    search_offset = 0;
                }
            }
        }

        Sleep(10); // CPU 과부하 방지를 위해 10ms 대기
    }

    CloseHandle(hSerial);
    return 0;
}