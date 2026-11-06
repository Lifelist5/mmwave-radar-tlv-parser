#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h> // Windows API for Serial Communication

// --- 1. 설정 및 상수 ---
#define MAGIC_WORD 0xDEADBEEF 
#define HEADER_SIZE 40      
#define MAX_BUFFER_SIZE 4096 
#define MAX_PACKET_LENGTH 2048 // 🚨🚨🚨 TLV 패킷의 최대 길이 (4096을 넘지 않는 상식적인 값으로 설정)

// ----------------------------------------------------------------------
// 2. 구조체 정의 (바이트 정렬 필수)
// ----------------------------------------------------------------------
#pragma pack(1)

typedef struct {
    uint32_t magicWord;
    uint32_t version;
    uint32_t totalLength;
    uint32_t frameNumber;
    uint32_t platform;
    uint32_t timeCpuCycles;
    uint16_t numDetectedObj;
    uint16_t numTLV;
    uint32_t subFrameNum;
} MmwDemo_Header;

typedef struct {
    int16_t x; int16_t y; int16_t z; int16_t velocity;
} PointCloud_t;

// Type 7: SNR/Noise 정보 (가정: 4 bytes)
typedef struct {
    int16_t snr;
    int16_t noise;
} SNRInfo_t;

typedef struct {
    uint32_t type;
    uint32_t length;
} TLV_Header;

#pragma pack() 
// ----------------------------------------------------------------------

// 3. TLV 파싱 함수 (로직은 이전과 동일)
void parse_tlv_data(const uint8_t* packet_buffer, uint32_t total_length) {
    // Type 1과 Type 7 파싱 로직 그대로 사용

    const uint8_t* rx_buffer_ptr = packet_buffer + sizeof(MmwDemo_Header);
    const MmwDemo_Header* header = (const MmwDemo_Header*)packet_buffer;

    uint16_t numTLV = header->numTLV;

    printf("\n--- TLV Frame Start --- (Total Length: %u, Num TLV: %u)\n", total_length, numTLV);

    for (int i = 0; i < numTLV; i++) {
        const TLV_Header* tlv_h = (const TLV_Header*)rx_buffer_ptr;

        uint32_t type = tlv_h->type;
        uint32_t length = tlv_h->length;

        printf("[TLV #%d] Type: %u, Length: %u bytes\n", i + 1, type, length);

        const uint8_t* tlv_value_ptr = rx_buffer_ptr + sizeof(TLV_Header);

        if (type == 1) {
            size_t num_points = length / sizeof(PointCloud_t);
            const PointCloud_t* points_array = (const PointCloud_t*)tlv_value_ptr;

            for (size_t j = 0; j < (num_points > 5 ? 5 : num_points); j++) {
                printf("  -> Point %zu: X=%d, Y=%d, Z=%d, V=%d\n", j, points_array[j].x, points_array[j].y, points_array[j].z, points_array[j].velocity);
            }
            if (num_points > 5) printf("  ...and %zu more points (showing only 5)\n", num_points - 5);
        }
        else if (type == 7) {
            size_t num_snr_items = length / sizeof(SNRInfo_t);
            const SNRInfo_t* snr_array = (const SNRInfo_t*)tlv_value_ptr;

            printf("  -> SNR Info received (Items: %zu)\n", num_snr_items);

            for (size_t j = 0; j < (num_snr_items > 5 ? 5 : num_snr_items); j++) {
                printf("    SNR Item %zu: SNR=%d, Noise=%d\n", j, snr_array[j].snr, snr_array[j].noise);
            }
            if (num_snr_items > 5) printf("    ...and %zu more SNR items (showing only 5)\n", num_snr_items - 5);
        }

        rx_buffer_ptr += sizeof(TLV_Header) + length;

        if (rx_buffer_ptr > packet_buffer + total_length) {
            printf("Error: TLV structure exceeded total length.\n");
            break;
        }
    }
    printf("--- TLV Frame End ---\n");
}


// 4. UART 설정 및 수신 로직 (Windows API)
HANDLE open_and_configure_serial_port(const char* port_name) {
    //  open_and_configure_serial_port 함수 내용 그대로 사용
    HANDLE hSerial = CreateFileA(port_name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hSerial == INVALID_HANDLE_VALUE) { printf("Error opening serial port %s\n", port_name); return INVALID_HANDLE_VALUE; }

    DCB dcbSerialParams = { 0 }; dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
    if (!GetCommState(hSerial, &dcbSerialParams)) { CloseHandle(hSerial); return INVALID_HANDLE_VALUE; }

    dcbSerialParams.BaudRate = 115200; dcbSerialParams.ByteSize = 8;
    dcbSerialParams.StopBits = ONESTOPBIT; dcbSerialParams.Parity = NOPARITY;
    if (!SetCommState(hSerial, &dcbSerialParams)) { CloseHandle(hSerial); return INVALID_HANDLE_VALUE; }

    COMMTIMEOUTS timeouts = { 0 };
    timeouts.ReadIntervalTimeout = 50; timeouts.ReadTotalTimeoutConstant = 50; timeouts.ReadTotalTimeoutMultiplier = 10;
    timeouts.WriteTotalTimeoutConstant = 50; timeouts.WriteTotalTimeoutMultiplier = 10;
    SetCommTimeouts(hSerial, &timeouts);

    return hSerial;
}

uint8_t rx_buffer[MAX_BUFFER_SIZE];
size_t buffer_index = 0;

int main() {
    const char* port_name = "\\\\.\\COM3"; // ❗ 사용할 COM 포트 번호로 변경
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

            uint32_t current_magic = *(uint32_t*)(rx_buffer + search_offset);

            if (current_magic == MAGIC_WORD) {

                MmwDemo_Header* header = (MmwDemo_Header*)(rx_buffer + search_offset);
                uint32_t total_length = header->totalLength;

                // 🚨🚨🚨 3. Total Length 안전성 검증 (강화된 로직) 🚨🚨🚨
                if (total_length > MAX_PACKET_LENGTH || total_length < HEADER_SIZE) {
                    printf("Desync: Invalid Total Length (%u). Skipping header.\n", total_length);
                    search_offset += 4; // Magic Word 건너뛰고 다음 4바이트부터 다시 검색
                    continue;
                }

                // 4. 패킷 완성 여부 확인
                if (buffer_index >= search_offset + total_length) {

                    parse_tlv_data(rx_buffer + search_offset, total_length);

                    // 파싱 완료된 패킷을 버퍼에서 제거
                    size_t packet_size = search_offset + total_length;
                    size_t remaining_data = buffer_index - packet_size;
                    memmove(rx_buffer, rx_buffer + packet_size, remaining_data);
                    buffer_index = remaining_data;
                    search_offset = 0;
                }
                else {
                    // 패킷이 덜 도착함.
                    break;
                }
            }
            else {
                // Magic Word가 아님. 1바이트씩 이동하며 검색
                search_offset++;

                // 오염된 데이터 정리
                if (search_offset > buffer_index - 4) {
                    size_t remaining_data = buffer_index - search_offset;
                    memmove(rx_buffer, rx_buffer + search_offset, remaining_data);
                    buffer_index = remaining_data;
                    search_offset = 0;
                }
            }
        }

        // ❗❗❗ [FPGA 포팅 시 수정 필요] Sleep 함수를 타겟 시스템의 지연 함수로 교체필요. 
        Sleep(10);
    }

    CloseHandle(hSerial);
    return 0;
}