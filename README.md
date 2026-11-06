# mmWave Radar TLV Data Parser & UDP Pipeline

> 세종대학교 전자정보통신공학과 캡스톤디자인 경진대회 **대상(Grand Prize) 수상작** 
> **저시야 환경(연기/안개)에서의 소방관 안전 보조를 위한 실시간 레이더 객체 탐지 시스템**

---

# 프로젝트 개요 (Overview)
재난 현장과 같은 저시야 환경에서는 시각 정보가 제한되어 소방관의 상황 인지가 지연되고 구조 리스크가 증가합니다[cite: 3, 16]. 본 프로젝트는 **mmWave 레이더(IWR6843AOP)**와 **임베디드 보드(Zybo Z7-20)**를 활용해 PC 없이 단독으로 동작하며, 레이더의 실시간 검출 결과를 파싱·처리하여 오디오(Sonification)로 즉각 피드백해주는 휴대용 시스템입니다

본 저장소에는 센서 제어부터 고속 시리얼 통신, 바이너리 패킷 파싱, 그리고 실시간 네트워크 전송에 이르는 핵심 데이터 파이프라인 코드가 단계별로 기록되어 있습니다.

---

#  담당 역할 (My Role)
* **하드웨어 제어 및 고속 시리얼 통신:** 듀얼 COM 포트를 통한 CFG(115200 bps) 설정 주입 및 DATA(921600 bps) 스트림 수신
* **로우레벨 바이너리 프로토콜 파싱:** 8바이트 Magic Word 동기화, 44바이트 패킷 헤더 구조체 매핑, Type-Length-Value (TLV) 포맷 디코딩 구현[cite: 17]
* **통신 안정성 및 실시간 파이프라인 구축:** 패킷 오염(Desync) 방어 로직 구현 및 파싱된 3D 포인트 클라우드(`Float` 좌표, Doppler)의 실시간 UDP 소켓 브로드캐스팅 연동

---

# 개발 진화 과정 (Iterative Development History)
본 프로젝트는 안정적인 실시간 데이터 처리를 위해 다음과 같은 4단계의 점진적 고도화 과정을 거쳐 개발되었습니다.

1. **Phase 1 (Mock Data Unit Test):** 가상 버퍼 및 1바이트 구조체 패킹(`#pragma pack(1)`)을 통한 메모리 정렬 및 TLV 기본 파싱 로직 단위 검증
2. **Phase 2 (Win32 UART & Ring Buffer):** Win32 API를 활용한 실제 시리얼 포트 연동 및 슬라이딩 윈도우 기반 스트림 동기화 구현
3. **Phase 3 (Desync Defense & Exception Handling):** 패킷 길이 오염으로 인한 무한 대기 버그를 막기 위한 `MAX_PACKET_LENGTH` 방어 로직 및 예외 처리 강화
4. **Phase 4 (Hardware Spec & UDP Pipeline Integration):** 실제 TI IWR6843AOP 레이더 스펙(44B 헤더, Float 좌표, 32바이트 패딩 스킵) 적용 및 실시간 UDP 소켓 전송 파이프라인 완성

---

## 시스템 아키텍처 (System Flow)

```text
[ IWR6843AOP Radar ] --(CFG / DATA UART)--> [ Parser & Pipeline ] --(UDP)--> [ 3D Visualization & Audio Sonification ]
