# mmWave Radar TLV Parser & UDP Pipeline

세종대학교 캡스톤디자인 경진대회 **대상(Grand Prize) 수상작**
저시야 환경(연기/안개) 소방관 안전 보조를 위한 **실시간 mmWave 레이더 객체 탐지 시스템**

---

## 프로젝트 소개
* 개발 배경: 재난 현장 같은 저시야 환경에서 카메라/열화상 한계를 극복하고, 소방관의 시선 분산을 최소화하기 위해 레이더 포인트 클라우드를 오디오(Sonification)로 즉각 변환하는 시스템
* 핵심 장비: TI IWR6843AOP 레이더 센서 + Zybo Z7-20 (Zynq-7020) 임베디드 보드

---

## 담당 역할 (My Role)
1. 듀얼 시리얼 통신: CFG(115200 bps) 설정 주입 및 DATA(921600 bps) 스트림 수신
2. 바이너리 프로토콜 파싱: 8바이트 Magic Word 동기화, 44바이트 헤더 및 TLV(Type-Length-Value) 디코딩 구현
3. 네트워크 파이프라인: 패킷 오염(Desync) 방어 로직 적용 및 파싱된 3D 포인트 클라우드 좌표(Float, Doppler)의 실시간 UDP 브로드캐스팅 구현

---

## 개발 진화 과정 (Iterative History)
* Phase 1 (Mock Data): 가상 버퍼 및 1바이트 구조체 패킹(#pragma pack(1))으로 파서 로직 단위 검증
* Phase 2 (UART & Buffer): Win32 API 기반 시리얼 포트 연동 및 슬라이딩 윈도우 버퍼링 구현
* Phase 3 (Exception Handling): 패킷 길이 오염(Desync) 방어 로직(MAX_PACKET_LENGTH) 추가로 안정성 확보
* Phase 4 (Hardware Integration): 실제 레이더 스펙 적용 및 UDP 소켓 통신 최종 통합

---

## 실행 방법 (Usage)
mmw_uart_win.exe <CFG_COM> <DATA_COM> <cfg_file> [udp_ip] [udp_port]
예시: mmw_uart_win.exe COM23 COM24 xwr68xx_AOP_profile.cfg 127.0.0.1 9001
