#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// 구조체 바이트 정렬을 1바이트로 강제 설정 (이전 문제 해결)
#pragma pack(1) 

// 1. TI mmWave 패킷 헤더 구조
typedef struct {
    uint32_t magicWord;   // 4 bytes
    uint32_t version;     // 4 bytes
    uint32_t totalLength; // 4 bytes
    uint32_t frameNumber; // 4 bytes
    uint32_t platform;    // 4 bytes
    uint32_t timeCpuCycles; // 4 bytes
    uint16_t numDetectedObj; // 2 bytes
    uint16_t numTLV;       // 2 bytes
    uint32_t subFrameNum;  // 4 bytes
    // 총 40 bytes
} MmwDemo_Header;

// 2. Type 1: Point Cloud의 개별 점(Point) 데이터 구조 (8 bytes)
typedef struct {
    int16_t x;          // X 좌표 (2 bytes)
    int16_t y;          // Y 좌표 (2 bytes)
    int16_t z;          // Z 좌표 (2 bytes)
    int16_t velocity;   // 도플러 속도 (2 bytes)
} PointCloud_t;

// 3. TLV 블록 헤더 (8 bytes)
typedef struct {
    uint32_t type;     // 4 bytes
    uint32_t length;   // 4 bytes
} TLV_Header;

// 구조체 정렬 설정을 기본값으로 되돌림 (선택 사항이지만 안전함)
#pragma pack() 


// 4. 엔디안 변환 함수: Big-Endian -> Little-Endian (또는 그 반대)
// 현재 시스템(PC)과 데이터 송신측(FPGA/레이더)의 바이트 순서가 다를 때 사용됩니다.
uint32_t swap_endian32(uint32_t val) {
    return (val << 24) | ((val << 8) & 0x00FF0000) |
        ((val >> 8) & 0x0000FF00) | (val >> 24);
}
uint16_t swap_endian16(uint16_t val) {
    return (val << 8) | (val >> 8);
}


// --- 가상 데이터 정의 ---
// 현재 코드는 Little Endian으로 데이터가 배열되어 있습니다 (0xEF, 0xBE, 0xAD, 0xDE)
// 만약 레이더가 Big Endian으로 보낸다면, 이 데이터 배열을 Big Endian 순서로 바꿔야 합니다.
// 하지만 일단 지금은 파싱 로직만 수정하고, 이 데이터 배열은 그대로 두겠습니다.
uint8_t rx_buffer[100] = {
    // ... (이전과 동일한 가상 데이터) ...
    // ----------------------------------------------------------------------
    // 1. MmwDemo_Header (40 Bytes) - 예시 MagicWord: 0xDEADBEEF
    // ----------------------------------------------------------------------
    0xEF, 0xBE, 0xAD, 0xDE, // Magic Word (0xDEADBEEF, Little Endian)
    0x00, 0x00, 0x00, 0x00, // Version (임의)
    0x58, 0x00, 0x00, 0x00, // Total Length (88 bytes, Little Endian)
    0x00, 0x00, 0x00, 0x00, // Frame Number
    0x00, 0x00, 0x00, 0x00, // Platform
    0x00, 0x00, 0x00, 0x00, // Time CPU Cycles
    0x00, 0x00, 0x00, 0x00, // 4 bytes padding (version ~ timeCpuCycles)
    0x01, 0x00,             // numDetectedObj = 1 (1 point)
    0x02, 0x00,             // numTLV = 2 (Type 1, Type 7)
    0x00, 0x00, 0x00, 0x00, // subFrameNum (4 bytes)
    // ----------------------------------------------------------------------
    // 2. TLV 1: Point Cloud (Type 1, Length 8 Bytes)
    // ----------------------------------------------------------------------
    0x01, 0x00, 0x00, 0x00, // TLV Type = 1 (Little Endian)
    0x08, 0x00, 0x00, 0x00, // TLV Length = 8 (1 Point * 8 bytes/Point)
    // V: PointCloud_t (1st Point - 8 Bytes)
    0x32, 0x00,             // X = 50 (Little Endian)
    0x00, 0x01,             // Y = 256 (Little Endian)
    0x0A, 0x00,             // Z = 10 (Little Endian)
    0xF6, 0xFF,             // Velocity = -10 (Little Endian)
    // ----------------------------------------------------------------------
    // 3. TLV 2: SNR Info (Type 7, Length 8 Bytes)
    // ----------------------------------------------------------------------
    0x07, 0x00, 0x00, 0x00, // TLV Type = 7
    0x08, 0x00, 0x00, 0x00, // TLV Length = 8 (1 SNR value * 8 bytes/value)
    // V: SNR Data (8 Bytes)
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00
};

int main() {
    printf("--- TLV Parsing Test Start ---\n");

    uint8_t* rx_buffer_ptr = rx_buffer;
    size_t buffer_length = sizeof(rx_buffer);

    // 1. 헤더 파싱 및 검증
    MmwDemo_Header* header = (MmwDemo_Header*)rx_buffer_ptr;

    // 🚨 Magic Word는 고정 값이므로, 변환된 값으로 비교해야 합니다.
    // 레이더가 리틀 엔디안으로 보내는 것으로 가정했으므로, 여기서는 변환이 필요 없지만,
    // 만약 레이더가 빅 엔디안을 쓴다면 header->magicWord != swap_endian32(0xDEADBEEF) 로 비교해야 합니다.
    // 일단 현재 가상 데이터는 리틀 엔디안이므로, 변환 없이 비교합니다.
    if (header->magicWord != 0xDEADBEEF) {
        printf("Error: Invalid Magic Word (Expected 0xDEADBEEF, Got 0x%X)\n", header->magicWord);
        return 1;
    }

    // 🚨 헤더 필드 변환 적용!
    uint32_t totalLength = header->totalLength;
    uint16_t numTLV = header->numTLV;
    // 만약 빅 엔디안이라면:
    // uint32_t totalLength = swap_endian32(header->totalLength);
    // uint16_t numTLV = swap_endian16(header->numTLV);

    printf("Header OK. Total Length: %u bytes, Num TLV: %u\n", totalLength, numTLV);

    // 2. TLV 목록 시작 위치로 포인터 이동
    rx_buffer_ptr += sizeof(MmwDemo_Header);

    // 3. TLV 목록 순회
    // 순회 횟수를 변환된 numTLV로 제한합니다.
    for (int i = 0; i < numTLV; i++) {
        TLV_Header* tlv_h = (TLV_Header*)rx_buffer_ptr;

        // 🚨 TLV 헤더 필드 변환 적용!
        uint32_t type = tlv_h->type;
        uint32_t length = tlv_h->length;
        // 만약 빅 엔디안이라면:
        // uint32_t type = swap_endian32(tlv_h->type);
        // uint32_t length = swap_endian32(tlv_h->length);

        printf("\n[TLV #%d] Type: %u, Length: %u bytes\n", i + 1, type, length);

        // 데이터(Value) 영역의 시작 위치로 포인터 이동
        uint8_t* tlv_value_ptr = rx_buffer_ptr + sizeof(TLV_Header);

        if (type == 1) { // Type 1: Point Cloud
            size_t num_points = length / sizeof(PointCloud_t);
            PointCloud_t* points_array = (PointCloud_t*)tlv_value_ptr;

            for (size_t j = 0; j < num_points; j++) {
                // 🚨 PointCloud 필드 변환 적용! (X, Y, Z, V 모두 2바이트)
                int16_t x = points_array[j].x;
                int16_t y = points_array[j].y;
                int16_t z = points_array[j].z;
                int16_t velocity = points_array[j].velocity;
                // 만약 빅 엔디안이라면:
                // int16_t x = (int16_t)swap_endian16(points_array[j].x);
                // int16_t y = (int16_t)swap_endian16(points_array[j].y);
                // int16_t z = (int16_t)swap_endian16(points_array[j].z);
                // int16_t velocity = (int16_t)swap_endian16(points_array[j].velocity);

                printf("  -> Point %zu: X=%d, Y=%d, Z=%d, V=%d\n", j,
                    x, y, z, velocity);
            }
        }
        else if (type == 7) { // Type 7: SNR Info
            printf("  -> SNR Info received (Actual parsing required based on SDK doc)\n");
        }

        // 다음 TLV 헤더 위치로 포인터 이동
        rx_buffer_ptr += sizeof(TLV_Header) + length;
    }

    printf("\n--- TLV Parsing Test Complete ---\n");
    return 0;
}