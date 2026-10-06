/*
 * main_node_window_can_v5_4.c — 스마트 창문 CAN 노드 (STM32G474RET + DRV8313 + AS5047P + INA240)
 *   [v5.4] POS_FROM_STEPS = 1 : 창문 위치를 자석 엔코더가 아니라 "모터에 보낸 스텝 수" 로 계산
 *     - 자석이 작거나 중심이 어긋나서 엔코더 각도가 한 바퀴를 다 못 따라가고 좁은 범위(약 20도)에서
 *       왔다 갔다만 하는 보드용. (증상: '여는 중' 인데 개도율이 0% <-> 2% 반복)
 *     - 스텝모터는 보낸 만큼 돌기 때문에, 켠 자리(닫힘) = 0% 로 두고 스텝 수로 0~100% 계산
 *     - 전류 끼임 감지 / 이동 시간 제한 / 0도 맞추기(SW 짧게, SW 2초) 는 그대로 동작
 *     - 이 모드에서는 엔코더 걸림 감지, 엔코더 보정(SW 5초), 플래시 0도 저장은 쓰지 않음 (각도는 표시용으로만 읽음)
 *     - 주의: 전원을 켤 때 창문이 닫혀 있어야 함 (켠 자리 = 0%). 끼임으로 스텝이 밀리면 SW 짧게 눌러 0% 로 돌린 뒤
 *             창문을 손으로 닫고 SW 2초로 0도를 다시 잡으세요
 *     - 자석이 잘 되는 보드는 POS_FROM_STEPS 0 으로 두면 v5.3 과 똑같이 엔코더로 동작
 *   [v5.3] 작은(약한) 자석용 — v5.2 에 더해
 *     - 각도를 1ms 마다 5번 읽어 중간값 (v5.2 는 3번)
 *     - 엔코더 보정(SW 5초) 결과를 플래시에 저장 -> 전원을 꺼도 유지, 켤 때 자동 적용
 *       작은 자석/중심이 살짝 어긋난 자석에서 생기는 '돌아가는 각도에 따라 커졌다 작아지는 오차'를 없애 줌
 *       ★ 이 보드에서는 창문을 닫아 둔 상태에서 SW 를 5초 눌러 보정을 한 번 해 주세요 (모터가 한 바퀴 열었다 돌아옴)
 *     - 이미 보정된 상태에서 다시 보정해도 위치가 틀어지지 않게 맞춤
 *     - 걸림 감지를 조금 더 느슨하게 (각도가 덜 매끄럽게 나와도 오판하지 않게)
 *   v5.1 그대로 + [v5.2] 자석(AS5047P) 인식을 너그럽게
 *     - 1ms 마다 각도를 3번 연속 읽어서 중간값 사용 -> 자석이 약하거나 멀어서 값이 튀어도 안정
 *     - 1ms 사이 말도 안 되게 크게 튄 값(ENC_JUMP_MAX 초과)은 버림
 *     - 칩 진단(DIAAGC)은 표시용으로만 읽음: 자석 약함(MAGL)/AGC 최대여도 각도가 안정적이면 '사용 가능'
 *       (v5 는 진단 응답이 0.5초 안 오면 고장 1 로 정지했음 -> 이제 정지 안 함)
 *     - 각도 읽기가 잠깐 실패해도 고장으로 멈추지 않음. ENC_LOST_MS 넘게 아예 못 읽으면
 *       이동만 멈추고(고장코드 1 표시, 빨간 LED 빠르게 깜빡) 다시 읽히면 자동으로 이어서 이동
 *     - 정지 유지 중 다시 움직이기 시작하는 기준을 넓힘(HOLD_RESTART) -> 노이즈로 앞뒤 떨지 않게
 *     - 걸림 감지를 목표 근처 저속(4rpm~)에서도 감시 + 이동 시간 제한 -> 각도가 멈춰도 끝없이 돌지 않음
 *     - SPI 5MHz -> 2.5MHz (선이 길어도 덜 깨짐)
 *   v5 기능(이동/끼임/보정/0도 저장/게이트웨이/공기질/구역 부저) 그대로
 *
 *   ★ 보드마다 아래 설정만 바꿔서 올리세요 ★
 *     NODE_ID          1 = 창문1(0x101/0x111), 2 = 창문2(0x102/0x112)
 *     FAN_ON_THIS_NODE 환풍기(C1815 -> 팬)가 PB4 에 연결된 노드만 1
 *     CAN_UP_PORT      마스터 쪽으로 연결된 포트 (1 = CAN1 PA11/PA12, 2 = CAN2 PB5/PB6)
 *     CAN_GATEWAY      1 이면 반대쪽 포트를 열어 양쪽 버스 메시지를 서로 중계
 *
 *     배선 (마스터 -CAN1- 노드1 -CAN2- 노드2 [-CAN1- 패널]) 기준:
 *       노드1 : NODE_ID 1, CAN_UP_PORT 1, CAN_GATEWAY 1
 *       노드2 : NODE_ID 2, CAN_UP_PORT 2, CAN_GATEWAY 0   (노드2 의 빈 CAN1 포트에 패널을 꽂았으면 1)
 *
 * ---------------- 동작 ----------------
 *   [전원 ON] 저장된 0도가 있으면 가장 가까운 0도로 맞춰 정렬(최대 반 바퀴), 없으면 켠 자리 = 0
 *   [CAN 0x100 수신] [1]창문 목표% -> 0 / 50 / 75 / 100 % 네 단계로 맞춰서 이동
 *                    [2]환풍기 PWM% -> PB4 (FAN_ON_THIS_NODE=1 일 때)
 *   [이동] 멀면 빠르게, 가까워질수록 천천히. 도착하면 약한 유지 전류로 정지
 *   [끼임] INA240 전류가 평소보다 크게 오르거나, 엔코더가 안 움직이면
 *          -> 즉시 정지 -> 반대 방향으로 살짝 되돌아감 -> 정지 유지 (고장코드 5 / 2)
 *   [SW 짧게]      정지 중이면 0도(= 창문 0%)로 복귀.  끼임 중이면 끼임 해제,  고장 중이면 고장 해제
 *   [SW 2~5초 후 뗌] 지금 자리를 0도로 저장 (플래시, 전원 꺼도 유지)
 *                   누르고 2초가 지나면 초록+주황 LED 가 같이 켜짐 -> 이때 떼면 저장, 저장되면 LED 3번 깜빡
 *   [SW 5초 이상]   엔코더 보정 (정지 중일 때만)
 *   [마스터 명령] 목표가 "바뀔 때만" 따라감 -> SW 로 0도 복귀한 상태는 마스터가 목표를 바꿀 때까지 유지
 *   0x111/0x112 의 각도는 저장된 0도 기준 (여는 방향으로 증가)
 *
 * ---------------- CAN (500kbps) ----------------
 *   수신 0x100 : [0]등급 [1]창문목표% [2]환풍기% [3]seq [4]플래그
 *                [4] bit1 = 구역별 목표 있음 -> [5]창문1 목표% [6]창문2 목표% [7]구역 등급 묶음
 *   수신 0x120 (마스터 환경) : [2..3]온도x10 [4..5]습도x10 [6] bit0 DHT22 정상 -> ENS160 보정
 *   송신 0x100+NODE_ID (100ms) :
 *        [0]상태 [1]현재% [2]목표% [3]고장코드 [4]부하율% [5..6]전류mA [7]seq
 *   송신 0x110+NODE_ID (100ms, 50ms 엇갈림) :
 *        [0]AGC [1]플래그 [2..3]절대각(0~16383) [4..5]전압mV [6]DRV온도 [7]PWR온도 (int8 도, 0x80 = 없음)
 *        플래그 bit0 엔코더 응답(각도가 읽힘), bit1 자석 사용 가능(각도 읽히고 안정), bit2 MAGL(약함), bit3 MAGH(셈),
 *               bit4 COF, bit5 보정 완료, bit6 각도 불안정(정지 중인데 각도가 튐)
 *   송신 0x120+NODE_ID (500ms) : 구역 공기질
 *        [0..1]CO ppm [2..3]TVOC ppb [4..5]eCO2 ppm (모두 큰 바이트 먼저) [6]AQI 1~5 (0 = 없음)
 *        [7]플래그 bit0 ENS160 응답, bit1-2 ENS160 유효도(0 정상 1 예열 2 첫 가동 3 무효), bit3 CO 예열 끝, bit4 CO 센서 정상
 *   송신 0x201 (환풍기 노드만) : [0]현재PWM% [1]목표PWM% [7]seq
 *   고장코드 : 0 없음, 1 엔코더 안 읽힘(다시 읽히면 자동 해제), 2 걸림/이동 시간 초과, 3 범위 이탈, 4 드라이버, 5 끼임(전류)
 *
 * ---------------- 핀 ----------------
 *   SW PB13, EN PB14, nFault PB15, nSLEEP PB9, TIM1 CH1~3 PA8/PA9/PA10
 *   SPI1 SCK PA5 / MISO PA6 / MOSI PA7, CS PA4
 *   엔코더 A/B PA0/PA1 (TIM2 엔코더 모드, 참고용)
 *   SERVO_PWM PB4 (TIM3_CH1) -> C1815 -> 팬
 *   I_SENSE_ADC PA3 (ADC1_IN4), VOLT_ADC PB0 (ADC1_IN15)
 *   TEMP_ADC_PWR PA2 (ADC1_IN3), TEMP_ADC_DRV PC3 (ADC1_IN9)
 *   ENS160: I2C4 SCL PC6 / SDA PC7 (주소 0x53 또는 0x52 자동 탐색)
 *   부저:   PB11 BUZ_OUTPUT (GPIO 출력, High = 울림)
 *   MQ-7 CO: PC5 = ADC2_IN11 (CO_ADC)
 *   LED: NORMAL PC10, RUN PC11, Red PB7
 *
 * ---------------- 디버깅 (Live Expressions) ----------------
 *   자석: g_enc_alive, g_enc_raw, g_agc(낮을수록 자석 셈, 255 = 너무 약함/멂), g_jitter, g_mag_noisy,
 *         g_enc_err(읽기 실패), g_enc_jump(튀어서 버린 값), g_last_rx
 *   위치: g_pos, g_target, g_err, g_pct, g_fault_reason, g_cal_state, g_cal_saved, g_cal_maxerr
 *   기타: g_i_ma, g_i_base, g_load_pct, g_vbus_mv, g_fan_now, g_tdrv_c, g_tpwr_c, g_gw_down, g_gw_up,
 *         g_co_ppm, g_co_mv, g_ens_ok, g_ens_valid, g_tvoc, g_eco2, g_ens_aqi, g_ens_err
 */
#include "main.h"
#include <math.h>

/* ---- CubeMX가 만든 다른 파일과의 호환성을 위한 핸들 선언 ---- */
TIM_HandleTypeDef   htim1;
TIM_HandleTypeDef   htim2;
TIM_HandleTypeDef   htim3;
SPI_HandleTypeDef   hspi1;
ADC_HandleTypeDef   hadc1;
ADC_HandleTypeDef   hadc2;
FDCAN_HandleTypeDef hfdcan1;
FDCAN_HandleTypeDef hfdcan2;
I2C_HandleTypeDef   hi2c3;
I2C_HandleTypeDef   hi2c4;
TIM_HandleTypeDef   htim6;
UART_HandleTypeDef  huart1;
UART_HandleTypeDef  huart2;

/* =====================================================================
 *  보드별 설정
 * ===================================================================== */
#define NODE_ID              2          /* 1 = 창문1, 2 = 창문2 */
#define FAN_ON_THIS_NODE     0          /* PB4 에 환풍기가 연결된 노드만 1 */

#define CAN_UP_PORT          1          /* 마스터 쪽 포트: 1 = CAN1(PA11/PA12), 2 = CAN2(PB5/PB6) */
#define CAN_GATEWAY          1          /* 1: 반대쪽 포트와 양방향 중계 */

#define USE_ABI_ENCODER      1          /* TIM2 A/B 엔코더 카운트 (참고용) */

/* =====================================================================
 *  창문 위치 (실측해서 맞추기)
 * ===================================================================== */
#define WIN_FULL_COUNTS      49152      /* 0% -> 100% 이동량 (16384 = 모터 1바퀴) */
#define OPEN_DIR             DIR_CW     /* 여는 방향. 열라고 했는데 닫히면 DIR_CCW 로 */
#define POS_DEADBAND         80
#define HOLD_RESTART         (POS_DEADBAND * 4)   /* 정지 중 이만큼 벗어나야 다시 이동 (v5 는 x2) */
#define RANGE_MARGIN         8192
#define RPM_MOVE_MAX         30.0f
#define RPM_MOVE_MIN         5.0f
#define KP_RPM_PER_REV       60.0f
#define RPM_FIRST            15.0f

/* =====================================================================
 *  자석(AS5047P) 인식 설정  [v5.2]
 * ===================================================================== */
#define ENC_SAMPLES          5          /* 1ms 마다 각도를 몇 번 읽어 중간값을 쓸지 (1~5). 자석이 약할수록 많이 */
#define POS_FROM_STEPS       1          /* 1: 위치 = 모터 스텝 수 (자석 인식이 나쁜 보드),  0: 위치 = 자석 엔코더 */
#define USE_SAVED_CAL        1          /* 1: 엔코더 보정 결과를 플래시에 저장하고 켤 때 불러옴 */
#define ENC_JUMP_MAX         2000       /* 1ms 사이 이보다 크게 바뀌면 잘못 읽은 값으로 버림 (30rpm 이면 1ms 에 약 8카운트) */
#define ENC_JUMP_CONFIRM     20U        /* 계속 튄 값만 들어오면 이 횟수 뒤엔 진짜로 받아들임 */
#define ENC_LOST_MS          300U       /* 이 시간 동안 한 번도 못 읽으면 '엔코더 끊김' -> 이동 멈춤 */
#define MOVE_TIMEOUT_MS      15000U     /* 한 번 이동이 이 시간 넘게 안 끝나면 멈춤 (고장코드 2). 0 = 끔 */
#define BACKOFF_TIMEOUT_MS   2000U      /* 끼임 후 되돌아가기 최대 시간 */

/* =====================================================================
 *  끼임 감지 (INA240)
 * ===================================================================== */
#define ISENSE_CHANNEL       ADC_CHANNEL_4   /* PA3 = I_SENSE_ADC */
#define VSENSE_CHANNEL       ADC_CHANNEL_15  /* PB0 = VOLT_ADC */
#define INA_GAIN             50.0f
#define SHUNT_MOHM           10.0f
#define VBUS_DIVIDER         11.0f
#define PINCH_RATIO          1.6f
#define PINCH_MARGIN_MA      120.0f
#define PINCH_ABS_MA         1500.0f
#define PINCH_CONFIRM_MS     120U
#define PINCH_SETTLE_MS      400U
#define PINCH_BACKOFF_COUNTS 3000
#define PINCH_BACKOFF_RPM    12.0f

/* =====================================================================
 *  온도 (패널/마스터 코드와 같은 값으로 맞출 것)
 * ===================================================================== */
#define TEMP_PWR_CHANNEL     ADC_CHANNEL_3   /* PA2 = TEMP_ADC_PWR */
#define TEMP_DRV_CHANNEL     ADC_CHANNEL_9   /* PC3 = TEMP_ADC_DRV */
#define TEMP_SENSOR          0          /* 0 = NTC 서미스터,  1 = 선형 IC */
#define NTC_R25_OHM          10000.0f
#define NTC_BETA             3950.0f
#define NTC_FIXED_OHM        10000.0f
#define NTC_ON_GND_SIDE      1          /* 1: 3.3V-[고정R]-ADC-[NTC]-GND */
#define LIN_MV_AT_0C         500.0f
#define LIN_MV_PER_C         10.0f

/* =====================================================================
 *  구역 공기질: MQ-7 CO (PC5 = ADC2_IN11) + ENS160 (I2C4)
 * ===================================================================== */
#define CO_CHANNEL           ADC_CHANNEL_11  /* PC5 = ADC2_IN11 */
#define CO_DIVIDER           1.0f       /* PC5 앞 분압비 (5V 모듈 출력을 분압했으면 그 비율, 예: 10k/20k -> 1.5) */
#define CO_PPM_PER_RATIO     50.0f      /* 기준선 대비 1배 오를 때마다 ppm (마스터에서 쓰던 값 그대로) */
#define CO_WARMUP_MS         60000U     /* 예열 시간 (시험할 땐 10000U) */
#define ENS_I2C_TIMING       0x70422731U   /* I2C4 100kHz @ PCLK1 80MHz */
#define AIR_TX_MS            500U       /* 공기질 송신 주기 */

/* =====================================================================
 *  구역 경고 부저 (PB11 BUZ_OUTPUT)
 * ===================================================================== */
#define NODE_BUZZ_LEVEL      1          /* 내 구역 등급이 이 값 이상으로 "오를 때" 울림 (1 주의, 2 경고, 3 위험) */
#define NODE_BUZZ_COUNT      2          /* 울리는 횟수 */
#define NODE_BUZZ_ON_MS      200U       /* 한 번 울리는 시간 */
#define NODE_BUZZ_OFF_MS     100U       /* 사이 쉬는 시간 */
#define NODE_BUZZ_LOCK_MS    5000U      /* 다 울린 뒤 이 시간 안에는 다시 안 울림 */
#define NODE_BUZZ_PASSIVE    1          /* 0: 능동형 부저(전압만 주면 소리)  1: 수동형(소리 안 나면) -> 약 2.5kHz 로 직접 흔듦 */

/* =====================================================================
 *  환풍기 PWM (PB4 / TIM3_CH1 -> C1815)
 * ===================================================================== */
#define FAN_PWM_HZ           25000U
#define FAN_INVERT           0
#define FAN_KICK_MS          400U
#define FAN_UP_MS_PER_PCT    15U
#define FAN_DOWN_MS_PER_PCT  40U

/* =====================================================================
 *  CAN
 * ===================================================================== */
#define CAN_ID_CMD           0x100U
#define CAN_ID_ME            (0x100U + NODE_ID)
#define CAN_ID_ST            (0x110U + NODE_ID)   /* 확장 상태 (패널 표시용) */
#define CAN_ID_MST_ST        0x110U               /* 마스터 상태 */
#define CAN_ID_AIR           (0x120U + NODE_ID)   /* 구역 공기질 (CO/TVOC/eCO2) */
#define CAN_ID_ENV           0x120U               /* 마스터 환경 (먼지/온습도) */
#define CAN_ID_FAN           0x201U

/* ===================== 핀 ===================== */
#define SW1_PORT         GPIOB
#define SW1_PIN          GPIO_PIN_13
#define LED_RUN_PORT     GPIOC
#define LED_RUN_PIN      GPIO_PIN_11
#define LED_NORMAL_PORT  GPIOC
#define LED_NORMAL_PIN   GPIO_PIN_10
#define LED_RED_PORT     GPIOB
#define LED_RED_PIN      GPIO_PIN_7
#define DRV_EN_PORT      GPIOB
#define DRV_EN_PIN       GPIO_PIN_14
#define DRV_NSLP_PORT    GPIOB
#define DRV_NSLP_PIN     GPIO_PIN_9
#define DRV_NFAULT_PORT  GPIOB
#define DRV_NFAULT_PIN   GPIO_PIN_15
#define ENC_CS_PORT      GPIOA
#define ENC_CS_PIN       GPIO_PIN_4

#define ENC_CS_LOW()     HAL_GPIO_WritePin(ENC_CS_PORT, ENC_CS_PIN, GPIO_PIN_RESET)
#define ENC_CS_HIGH()    HAL_GPIO_WritePin(ENC_CS_PORT, ENC_CS_PIN, GPIO_PIN_SET)
#define BUZ_PORT         GPIOB
#define BUZ_PIN          GPIO_PIN_11      /* BUZ_OUTPUT */

/* ===================== PWM ===================== */
#define TIM_CLK_HZ       80000000U
#define PWM_FREQ_HZ      20000U
#define PWM_ARR          ((TIM_CLK_HZ / PWM_FREQ_HZ) - 1U)
#define PWM_HALF         ((PWM_ARR + 1U) / 2U)

/* ===================== 모터 설정 ===================== */
#define FULL_STEPS_PER_REV   200U
#define ELEC_CYCLES_PER_REV  (FULL_STEPS_PER_REV / 4U)
#define AMP_MAX          0.30f
#define AMP_RUN          0.24f
#define AMP_HOLD         0.10f
#define AMP_RAMP_PER_MS  0.0005f
#define ACCEL_RPM_PER_S  60.0f
#define USE_NFAULT_CHECK 0

#define DIR_CW           (+1)
#define DIR_CCW          (-DIR_CW)

/* ===================== AS5047P ===================== */
#define AS_NOP           0x0000U
#define AS_ERRFL         0x0001U
#define AS_DIAAGC        0x3FFCU
#define AS_ANGLECOM      0x3FFFU
#define ENC_CPR          16384
#define ENC_CHECK_MS     100U      /* 진단(표시용) 읽는 주기 */

/* ===================== 엔코더 보정 ===================== */
#define CAL_POINTS       256
#define CAL_RPM          10.0f
#define CAL_LONGPRESS_MS 5000U    /* 5초 누르고 있으면 엔코더 보정 */
#define SW_ZERO_MS       2000U    /* 2초 넘게 누르고 떼면 0도 저장 */
#define CAL_TIMEOUT_MS   20000U
#define CAL_MIN_TRAVEL   12000

/* ===================== 걸림 감지 ===================== */
#define MAG_AGC_LOST     255U
#define JIT_WINDOW_MS    100U      /* 정지 중 각도 흔들림을 재는 구간 */
#define JIT_LIMIT        200       /* 구간 안에서 이 카운트(약 4.4도) 넘게 튀면 '불안정' */
#define JIT_CONFIRM      3U        /* 연속 3구간 같아야 판정 바꿈 */
#define STALL_WATCH_RPM  4.0f      /* v5 는 8rpm: 목표 근처 저속(5rpm)에서도 감시 */
#define STALL_SETTLE_MS  300U
#define STALL_WINDOW_MS  500U
#define STALL_MIN_MOVE   100       /* 5rpm 이면 500ms 에 약 680 카운트 움직여야 정상 */
#define STALL_WINDOWS    3U        /* 연속 3구간(1.5초) 안 움직여야 걸림 */

/* ===================== 상태 ===================== */
typedef enum
{
    ST_IDLE = 0,       /* 드라이버 꺼짐 */
    ST_MOVE,           /* 목표 위치로 이동 중 */
    ST_STOPPING,       /* 도착 감속 */
    ST_HOLD,           /* 정지 (약한 전류로 위치 유지) */
    ST_BACKOFF,        /* 끼임 후 되돌아가는 중 */
    ST_BACKOFF_STOP,   /* 되돌아간 뒤 감속 */
    ST_PINCHED,        /* 끼임 정지 유지 (해제 대기) */
    ST_CAL,            /* 엔코더 보정 중 */
    ST_FAULT
} StepState;

#define CAN_ST_HOLD   0
#define CAN_ST_MOVE   1
#define CAN_ST_FAULT  4

static volatile StepState state = ST_IDLE;
static int16_t  sin_tbl[1024];
static uint32_t phase = 0;
static int32_t  phase_rate_q16 = 0;
static uint32_t last_cyc = 0;
static float    cur_rpm = 0.0f;
static float    amp = 0.0f;
static int8_t   dir = DIR_CW;

volatile int32_t  g_pos = 0;
volatile int8_t   g_enc_sign = 1;
static uint8_t    enc_sign_known = 0;

static int64_t  cmd_phase_total = 0;
static int16_t  cal_lut[CAL_POINTS];
static uint8_t  cal_valid = 0;
static int32_t  step_zero = 0;        /* POS_FROM_STEPS: 0% 일 때의 스텝 카운트 */
static int32_t  Cmd_Counts(void);
static uint8_t  cal_dirty = 0;        /* 플래시에 저장할 보정값이 있음 */
volatile uint8_t  g_cal_saved = 0;   /* 1 = 플래시에 저장된 보정값을 쓰는 중 */
static uint16_t cal_raw_f[CAL_POINTS + 1], cal_raw_b[CAL_POINTS + 1];
static int32_t  cal_k = 0;
static uint8_t  cal_pass = 0;
static int32_t  cal_cmd0 = 0;
static uint32_t cal_start_ms = 0;
static uint16_t enc_raw_now = 0;
static uint16_t zero_raw = 0;
static uint8_t  enc_reprime = 0;       /* 영점을 다시 잡은 직후: 누적 기준만 새로 잡음 */
static uint8_t  zero_dirty = 0;        /* 플래시에 저장할 영점이 있음 */
static uint32_t led_ack_until = 0;     /* 영점 저장 확인 깜빡임 */
static uint32_t enc_last_ok = 0;       /* 마지막으로 각도를 읽은 시각 */
static uint8_t  enc_ever = 0;          /* 한 번이라도 읽혔음 */
volatile uint8_t  g_btn_level = 0;     /* 1 = 2초 넘게 누르는 중, 2 = 5초 넘음 */
volatile uint8_t  g_zero_saved = 0;    /* 1 = 플래시에 0도가 저장되어 있음 */
volatile int32_t  g_zero_off = 0;      /* 부팅 때 저장된 0도와의 차이 (카운트) */
volatile uint16_t g_enc_raw = 0;       /* 중간값 필터 거친 각도 (0~16383) */
volatile int32_t  g_enc_pos = 0;
volatile uint8_t  g_enc_alive = 0;     /* 1 = 최근 ENC_LOST_MS 안에 각도가 읽힘 */
volatile uint16_t g_diag = 0;
volatile uint8_t  g_agc = 0;
volatile uint8_t  g_mag_weak = 0;      /* 칩이 '자석 약함(MAGL)' 이라고 함 (참고용) */
volatile uint8_t  g_mag_lost = 1;      /* MAGL + AGC 최대 (참고용, 동작에는 안 씀) */
volatile uint8_t  g_enc_present = 0;   /* 진단 레지스터가 응답함 */
volatile uint16_t g_jitter = 0;        /* 정지 중 각도 흔들림 (카운트) */
volatile uint8_t  g_mag_noisy = 0;     /* 1 = 정지 중 각도가 튐 */
volatile uint16_t g_last_rx = 0;
volatile uint32_t g_enc_err = 0;       /* 각도 읽기 실패 횟수 */
volatile uint32_t g_enc_jump = 0;      /* 튀어서 버린 값 횟수 */
volatile uint8_t  g_cal_state = 0;     /* 0 = 안 함, 1 = 중, 2 = 완료, 3 = 실패 */
volatile int32_t  g_cal_maxerr = 0;
#if USE_ABI_ENCODER
static uint32_t   abi_zero = 0;
volatile int32_t  g_abi_pos = 0;
#endif
volatile uint8_t  g_fault_reason = 0;

/* 위치 제어 */
volatile int32_t  g_target = 0;
volatile int32_t  g_err = 0;           /* 목표 - 현재 위치 (카운트, 디버깅) */
volatile uint8_t  g_tgt_pct = 0;
volatile uint8_t  g_pct = 0;
static uint8_t    pinch_tgt_pct = 0;
static int32_t    backoff_start = 0;
static uint32_t   backoff_ms = 0;
static uint32_t   move_start_ms = 0;

/* 전류 / 전압 / 온도 */
volatile float    g_i_ma = 0.0f;
volatile float    g_i_base = 0.0f;
volatile uint16_t g_load_pct = 0;
volatile uint32_t g_vbus_mv = 0;
static float      i_offset_mv = 0.0f;
volatile float    g_tdrv_c = -999.0f, g_tpwr_c = -999.0f;   /* 환산 온도 (소수점까지) */
volatile float    g_tdrv_mv = 0.0f, g_tpwr_mv = 0.0f;       /* 핀 전압 mV: 멀티미터 값과 비교용 */

/* 환풍기 / CAN */
volatile uint8_t  g_fan_now = 0;
static uint8_t    fan_tgt = 0;
static uint32_t   fan_kick_until = 0;
static uint32_t   last_cmd_ms = 0;
static uint8_t    got_cmd = 0, hb_seq = 0;
volatile uint8_t  g_zone_lv = 0;       /* 마스터가 알려 준 내 구역 등급 (0x100 [7]) */
volatile uint8_t  g_buzz_on = 0;       /* 부저 울리는 중 */
volatile uint32_t g_buzz_cnt = 0;      /* 부저 울린 횟수 (디버깅) */

void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_TIM1_Init(void);
static void MX_SPI1_Init(void);
static void MX_ADC1_Init(void);
static void MX_ADC2_Init(void);
static void MX_I2C4_Init(void);
static void CAN_InitAll(void);
#if USE_ABI_ENCODER
static void MX_TIM2_Encoder_Init(void);
#endif
#if FAN_ON_THIS_NODE
static void MX_TIM3_FanPwm_Init(void);
#endif

/* ===================== AS5047P (SPI) ===================== */
static int32_t Wrap14(int32_t v)
{
    while (v >  ENC_CPR / 2) v -= ENC_CPR;
    while (v < -ENC_CPR / 2) v += ENC_CPR;
    return v;
}

static uint16_t AS_EvenParity(uint16_t v)
{
    uint16_t x = v & 0x7FFFU;
    x ^= x >> 8; x ^= x >> 4; x ^= x >> 2; x ^= x >> 1;
    return (x & 1U) ? (uint16_t)(v | 0x8000U) : (uint16_t)(v & 0x7FFFU);
}

static uint8_t AS_Transfer(uint16_t tx, uint16_t *rx)
{
    ENC_CS_LOW();
    HAL_StatusTypeDef st = HAL_SPI_TransmitReceive(&hspi1, (uint8_t *)&tx, (uint8_t *)rx, 1, 2);
    ENC_CS_HIGH();
    return (st == HAL_OK);
}

static void AS_ClearError(void)
{
    uint16_t dummy;
    AS_Transfer(AS_EvenParity((uint16_t)(0x4000U | AS_ERRFL)), &dummy);
    AS_Transfer(AS_EvenParity((uint16_t)(0x4000U | AS_NOP)), &dummy);
}

/* 응답 한 프레임 검사: 1 = 쓸 수 있는 값 */
static uint8_t AS_FrameOk(uint16_t rx, uint8_t *ef)
{
    if (rx == 0xFFFFU) return 0;                    /* MISO 가 High 로 붙음 = 응답 없음 */
    if (AS_EvenParity(rx) != rx) return 0;          /* 패리티 틀림 = 깨진 값 */
    if (rx & 0x4000U) { *ef = 1; return 0; }        /* 오류 플래그: 이 프레임 값은 믿지 않음 */
    return 1;
}

/* 레지스터 하나 읽기 (진단용) */
static uint8_t AS_Read(uint16_t addr, uint16_t *val)
{
    uint16_t rx;
    uint8_t  ef = 0;
    uint16_t cmd = AS_EvenParity((uint16_t)(0x4000U | (addr & 0x3FFFU)));
    uint16_t nop = AS_EvenParity((uint16_t)(0x4000U | AS_NOP));

    if (!AS_Transfer(cmd, &rx)) return 0;
    if (!AS_Transfer(nop, &rx)) return 0;
    if (!AS_FrameOk(rx, &ef)) { if (ef) AS_ClearError(); return 0; }
    *val = rx & 0x3FFFU;
    return 1;
}

/* 각도를 ENC_SAMPLES 번 연속 읽어 중간값. 몇 개가 깨져도 남은 값으로 계산 */
static uint8_t AS_ReadAngle(uint16_t *val)
{
    uint16_t cmd = AS_EvenParity((uint16_t)(0x4000U | AS_ANGLECOM));
    uint16_t nop = AS_EvenParity((uint16_t)(0x4000U | AS_NOP));
    uint16_t rx, v[ENC_SAMPLES];
    int32_t  d[ENC_SAMPLES];
    uint8_t  n = 0, ef = 0;

    if (!AS_Transfer(cmd, &rx)) return 0;           /* 첫 응답은 직전 명령의 결과라 버림 */
    for (int i = 0; i < ENC_SAMPLES; i++)
    {
        if (!AS_Transfer((i == ENC_SAMPLES - 1) ? nop : cmd, &rx)) continue;
        g_last_rx = rx;
        if (AS_FrameOk(rx, &ef)) v[n++] = (uint16_t)(rx & 0x3FFFU);
    }
    if (ef) AS_ClearError();
    if (n == 0) return 0;

    /* 0/16383 경계를 넘는 경우를 위해 첫 값 기준 차이로 정렬 */
    for (int i = 0; i < n; i++) d[i] = Wrap14((int32_t)v[i] - (int32_t)v[0]);
    for (int i = 1; i < n; i++)
    {
        int32_t x = d[i]; int j = i - 1;
        while (j >= 0 && d[j] > x) { d[j + 1] = d[j]; j--; }
        d[j + 1] = x;
    }
    int32_t m = (n == 2) ? (d[0] + d[1]) / 2 : d[n / 2];
    int32_t r = ((int32_t)v[0] + m) % ENC_CPR;
    if (r < 0) r += ENC_CPR;
    *val = (uint16_t)r;
    return 1;
}

/* 부팅/영점용: 몇 번 다시 시도 */
static uint8_t AS_ReadAngleRetry(uint16_t *val)
{
    for (int i = 0; i < 20; i++)
    {
        if (AS_ReadAngle(val)) return 1;
        HAL_Delay(5);
    }
    return 0;
}

/* 진단 레지스터: 표시용으로만 읽음 (실패해도 동작에 영향 없음) */
static uint8_t Enc_Probe(void)
{
    for (int tries = 0; tries < 3; tries++)
    {
        uint16_t d;
        if (AS_Read(AS_DIAAGC, &d) && d != 0x3FFFU)
        {
            uint8_t magl = (d >> 11) & 1U;
            g_diag = d;
            g_agc = (uint8_t)(d & 0xFFU);
            g_mag_weak = magl;
            g_mag_lost = (magl && g_agc >= MAG_AGC_LOST) ? 1U : 0U;
            g_enc_present = 1;
            return 1;
        }
    }
    g_enc_present = 0;
    return 0;
}

/* ===================== 엔코더 보정 적용 ===================== */
static uint16_t Enc_Correct(uint16_t raw)
{
    if (!cal_valid) return raw;
    uint32_t idx  = raw >> 6;
    int32_t  frac = raw & 63;
    int32_t  o0 = cal_lut[idx];
    int32_t  o1 = cal_lut[(idx + 1U) % CAL_POINTS];
    int32_t  v  = (int32_t)raw + o0 + ((o1 - o0) * frac) / 64;
    v %= ENC_CPR;
    if (v < 0) v += ENC_CPR;
    return (uint16_t)v;
}

static uint8_t Cal_Build(void)
{
    static uint16_t sraw[CAL_POINTS];
    static int16_t  soff[CAL_POINTS];

    int32_t travel = 0;
    for (int k = 0; k < CAL_POINTS; k++)
        travel += Wrap14((int32_t)cal_raw_f[k + 1] - (int32_t)cal_raw_f[k]);
    if (travel < CAL_MIN_TRAVEL && travel > -CAL_MIN_TRAVEL) return 0;
    int32_t sgn = (travel > 0) ? 1 : -1;

    int64_t sum = 0;
    int32_t maxerr = 0;
    for (int k = 0; k < CAL_POINTS; k++)
    {
        int32_t f = cal_raw_f[k];
        int32_t avg = f + Wrap14((int32_t)cal_raw_b[k] - f) / 2;
        avg %= ENC_CPR; if (avg < 0) avg += ENC_CPR;
        int32_t ideal = (int32_t)cal_raw_f[0] + sgn * k * (ENC_CPR / CAL_POINTS);
        int32_t off = Wrap14(ideal - avg);
        sraw[k] = (uint16_t)avg;
        soff[k] = (int16_t)off;
        sum += off;
    }
    int32_t mean = (int32_t)(sum / CAL_POINTS);
    for (int k = 0; k < CAL_POINTS; k++)
    {
        soff[k] = (int16_t)(soff[k] - mean);
        int32_t a = soff[k] < 0 ? -soff[k] : soff[k];
        if (a > maxerr) maxerr = a;
    }
    g_cal_maxerr = maxerr;

    for (int i = 1; i < CAL_POINTS; i++)
    {
        uint16_t r = sraw[i]; int16_t o = soff[i]; int j = i - 1;
        while (j >= 0 && sraw[j] > r) { sraw[j + 1] = sraw[j]; soff[j + 1] = soff[j]; j--; }
        sraw[j + 1] = r; soff[j + 1] = o;
    }

    for (int b = 0; b < CAL_POINTS; b++)
    {
        int32_t r = b * (ENC_CPR / CAL_POINTS);
        int j = 0;
        while (j < CAL_POINTS && sraw[j] <= r) j++;
        int lo = (j == 0) ? CAL_POINTS - 1 : j - 1;
        int hi = (j == CAL_POINTS) ? 0 : j;
        int32_t rlo = sraw[lo], rhi = sraw[hi];
        int32_t span = rhi - rlo; if (span <= 0) span += ENC_CPR;
        int32_t pos  = r - rlo;   if (pos < 0)   pos += ENC_CPR;
        cal_lut[b] = (int16_t)(soff[lo] + ((int32_t)(soff[hi] - soff[lo]) * pos) / span);
    }
    return 1;
}

/* ===================== 스텝모터 코어 ===================== */
static void Stepper_BuildTable(void)
{
    for (int i = 0; i < 1024; i++)
        sin_tbl[i] = (int16_t)(32767.0f * sinf(2.0f * 3.14159265f * (float)i / 1024.0f));
}

static void Stepper_PwmUpdate(void)
{
    uint32_t now = DWT->CYCCNT;
    uint32_t dt = now - last_cyc;
    last_cyc = now;
    int64_t inc = ((int64_t)phase_rate_q16 * (int64_t)dt) >> 16;
    phase += (uint32_t)inc;
    cmd_phase_total += inc;

    uint32_t idx = phase >> 22;
    int32_t  s   = sin_tbl[idx];
    int32_t  c   = sin_tbl[(idx + 256U) & 1023U];

    float a = amp;
    if (a > AMP_MAX) a = AMP_MAX;
    if (a < 0.0f)    a = 0.0f;
    int32_t amp_cnt = (int32_t)(a * (float)PWM_ARR);

    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, (uint32_t)((int32_t)PWM_HALF + ((c * amp_cnt) >> 15)));
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, (uint32_t)((int32_t)PWM_HALF + ((s * amp_cnt) >> 15)));
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, PWM_HALF);
}

static void Stepper_SetRpm(float rpm)
{
    double f_elec = (double)rpm / 60.0 * (double)ELEC_CYCLES_PER_REV;
    double rate = f_elec * 4294967296.0 / (double)SystemCoreClock * 65536.0;
    phase_rate_q16 = (int32_t)rate * dir;
}

static void Driver_Enable(uint8_t on)
{
    HAL_GPIO_WritePin(DRV_EN_PORT, DRV_EN_PIN, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void Driver_Start(void)
{
    amp = 0.0f;
    cur_rpm = 0.0f;
    last_cyc = DWT->CYCCNT;
    Stepper_PwmUpdate();
    Driver_Enable(1);
}

static void Enter_Fault(uint8_t reason)
{
    Driver_Enable(0);
    amp = 0.0f;
    cur_rpm = 0.0f;
    phase_rate_q16 = 0;
    state = ST_FAULT;
    g_fault_reason = reason;
}

/* 지금 자리를 0%(닫힘)로 */
static void Zero_Here(void)
{
    uint16_t a;
    if (AS_ReadAngleRetry(&a)) { zero_raw = a; enc_last_ok = HAL_GetTick(); enc_ever = 1; }
#if USE_ABI_ENCODER
    abi_zero = __HAL_TIM_GET_COUNTER(&htim2);
#endif
    g_enc_pos = 0;
#if POS_FROM_STEPS
    step_zero = Cmd_Counts();             /* 지금 스텝 수 = 0% */
    g_enc_sign = 1;                       /* 스텝 수는 CW 가 + 로 정해져 있음 */
    enc_sign_known = 1;
#endif
    g_pos = 0;
    g_target = 0;
    g_tgt_pct = 0;
    dir = OPEN_DIR;
    enc_reprime = 1;                      /* (모터-엔코더 방향 관계는 그대로라 다시 배우지 않음) */
}

/* ===================== 0도 저장 (내부 플래시 마지막 페이지) =====================
 *  듀얼 뱅크(기본): Bank2 page 127 = 0x0807F800 (2KB)
 *  싱글 뱅크      : page 127      = 0x0807F000 (4KB)
 *  저장 형식(64비트): [15:0] 0도 raw, [31:16] 0xA55A, [39:32] 체크, [47:40] 방향 부호 */
#define ZERO_MAGIC   0xA55AU

static uint32_t Zero_FlashAddr(uint32_t *bank, uint32_t *page)
{
    *page = 127U;
    if (READ_BIT(FLASH->OPTR, FLASH_OPTR_DBANK)) { *bank = FLASH_BANK_2; return 0x0807F800UL; }
    *bank = FLASH_BANK_1;
    return 0x0807F000UL;
}

static uint8_t Zero_Chk(uint32_t lo, uint8_t sg)
{
    return (uint8_t)(lo ^ (lo >> 8) ^ (lo >> 16) ^ (lo >> 24) ^ sg ^ 0x5AU);
}

static uint8_t Zero_Load(uint16_t *raw, int8_t *sign)
{
    uint32_t bank, page, addr = Zero_FlashAddr(&bank, &page);
    uint32_t lo = *(__IO uint32_t *)addr;
    uint32_t hi = *(__IO uint32_t *)(addr + 4U);
    uint8_t  sg = (uint8_t)(hi >> 8);
    if ((lo >> 16) != ZERO_MAGIC) return 0;
    if ((uint8_t)hi != Zero_Chk(lo, sg)) return 0;
    if (sg != 0x01U && sg != 0xFFU) return 0;
    *raw  = (uint16_t)(lo & 0x3FFFU);
    *sign = (int8_t)sg;
    return 1;
}

static uint8_t Zero_Save(void)
{
    FLASH_EraseInitTypeDef e = {0};
    uint32_t err = 0, bank, page, addr = Zero_FlashAddr(&bank, &page);
    uint8_t  sg = (uint8_t)g_enc_sign;
    uint32_t lo = ((uint32_t)ZERO_MAGIC << 16) | (zero_raw & 0x3FFFU);
    uint32_t hi = ((uint32_t)sg << 8) | Zero_Chk(lo, sg);
    uint8_t  ok;

    HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);
    e.TypeErase = FLASH_TYPEERASE_PAGES; e.Banks = bank; e.Page = page; e.NbPages = 1;
    ok = (HAL_FLASHEx_Erase(&e, &err) == HAL_OK) &&
         (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, addr, ((uint64_t)hi << 32) | lo) == HAL_OK);
    HAL_FLASH_Lock();
    return ok;
}

/* 저장된 0도에 맞춰 위치를 잡음 (가장 가까운 0도, 최대 반 바퀴). 저장된 게 없으면 지금 자리 = 0 */
static void Zero_Sync(void)
{
    uint16_t z, a;
    int8_t   sg;
#if POS_FROM_STEPS
    (void)z; (void)a; (void)sg;
    Zero_Here();                          /* 스텝 모드: 켠 자리 = 0% */
    return;
#endif
    if (!Zero_Load(&z, &sg) || !AS_ReadAngleRetry(&a)) { Zero_Here(); return; }
    enc_last_ok = HAL_GetTick();
    enc_ever = 1;
    zero_raw = z;
    g_enc_sign = sg;
    enc_sign_known = 1;
    g_zero_saved = 1;
#if USE_ABI_ENCODER
    abi_zero = __HAL_TIM_GET_COUNTER(&htim2);
#endif
    g_enc_pos  = Wrap14((int32_t)Enc_Correct(a) - (int32_t)Enc_Correct(zero_raw));
    g_zero_off = g_enc_pos;
    g_pos      = g_enc_pos * g_enc_sign * ((OPEN_DIR == DIR_CW) ? 1 : -1);
    g_target   = 0;                       /* -> 0도로 정렬 이동 */
    g_tgt_pct  = 0;
    dir = OPEN_DIR;
    enc_reprime = 1;
}

/* 플래시 쓰기는 약 20ms 동안 CPU 가 멈추므로, 모터가 완전히 멈춰 있을 때만 */
static void Zero_Service(void)
{
    if (POS_FROM_STEPS) { zero_dirty = 0; return; }   /* 스텝 모드는 켤 때마다 0% 부터라 저장할 필요 없음 */
    if (!zero_dirty || !enc_sign_known || state != ST_HOLD || cur_rpm != 0.0f) return;
    zero_dirty = 0;
    g_zero_saved = Zero_Save();
    if (g_zero_saved) led_ack_until = HAL_GetTick() + 600U;
}

/* ===================== 엔코더 보정값 저장 (플래시 0도 바로 앞 페이지) =====================
 *  듀얼 뱅크(기본): Bank2 page 126 = 0x0807F000 (2KB)
 *  싱글 뱅크      : page 126      = 0x0807E000 (4KB)
 *  형식: [0] 매직 + 체크값, [8~] 보정표 256개 (int16) */
#define CAL_MAGIC    0xCA15A5E1UL

static uint32_t Cal_FlashAddr(uint32_t *bank, uint32_t *page)
{
    *page = 126U;
    if (READ_BIT(FLASH->OPTR, FLASH_OPTR_DBANK)) { *bank = FLASH_BANK_2; return 0x0807F000UL; }
    *bank = FLASH_BANK_1;
    return 0x0807E000UL;
}

static uint32_t Cal_Sum(const int16_t *lut)
{
    uint32_t s = 0x1234567UL;
    for (int i = 0; i < CAL_POINTS; i++) s = s * 31U + (uint16_t)lut[i];
    return s;
}

static uint8_t Cal_Load(void)
{
#if USE_SAVED_CAL
    uint32_t bank, page, addr = Cal_FlashAddr(&bank, &page);
    const int16_t *lut = (const int16_t *)(addr + 8U);
    if (*(__IO uint32_t *)addr != CAL_MAGIC) return 0;
    if (*(__IO uint32_t *)(addr + 4U) != Cal_Sum(lut)) return 0;
    for (int i = 0; i < CAL_POINTS; i++) cal_lut[i] = lut[i];
    cal_valid = 1;
    return 1;
#else
    return 0;
#endif
}

static uint8_t Cal_Save(void)
{
    FLASH_EraseInitTypeDef e = {0};
    uint32_t err = 0, bank, page, addr = Cal_FlashAddr(&bank, &page);
    uint8_t  ok;

    HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);
    e.TypeErase = FLASH_TYPEERASE_PAGES; e.Banks = bank; e.Page = page; e.NbPages = 1;
    ok = (HAL_FLASHEx_Erase(&e, &err) == HAL_OK);
    for (int j = 0; ok && j < CAL_POINTS / 4; j++)
    {
        uint64_t w = 0;
        for (int b = 0; b < 4; b++) w |= (uint64_t)(uint16_t)cal_lut[j * 4 + b] << (16 * b);
        ok = (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, addr + 8U + 8U * (uint32_t)j, w) == HAL_OK);
    }
    if (ok)   /* 매직은 마지막에 써서, 중간에 전원이 꺼지면 '저장 안 됨' 으로 보이게 */
        ok = (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, addr,
                                ((uint64_t)Cal_Sum(cal_lut) << 32) | CAL_MAGIC) == HAL_OK);
    HAL_FLASH_Lock();
    return ok;
}

/* 보정 끝나고 모터가 완전히 멈췄을 때 저장 (약 30ms 동안 CPU 가 멈춤) */
static void Cal_Service(void)
{
#if USE_SAVED_CAL
    if (!cal_dirty || state != ST_HOLD || cur_rpm != 0.0f) return;
    cal_dirty = 0;
    g_cal_saved = Cal_Save();
    if (g_cal_saved) led_ack_until = HAL_GetTick() + 600U;
#endif
}

/* ===================== 목표 % -> 0/50/75/100 네 단계 ===================== */
static uint8_t Snap_Pct(uint8_t p)
{
    if (p < 25U) return 0;
    if (p < 63U) return 50;
    if (p < 88U) return 75;
    return 100;
}

static uint8_t Set_Target_Pct(uint8_t p)       /* 반환: 1 = 목표 적용됨 */
{
    p = Snap_Pct(p);
    if (state == ST_PINCHED || state == ST_BACKOFF || state == ST_BACKOFF_STOP)
    {
        if (p == pinch_tgt_pct) return 1;          /* 같은 목표면 끼임 정지 유지 (처리 완료로 봄) */
        if (state != ST_PINCHED) return 0;         /* 되돌아가는 중엔 나중에 다시 */
        g_fault_reason = 0;
        state = ST_HOLD;
    }
    g_tgt_pct = p;
    g_target = (int32_t)p * WIN_FULL_COUNTS / 100;
    move_start_ms = HAL_GetTick();
    return 1;
}

/* ===================== 끼임 처리: 정지 -> 반대로 살짝 -> 유지 ===================== */
static void Pinch_Start(uint8_t code)
{
    g_fault_reason = code;
    pinch_tgt_pct = g_tgt_pct;
    cur_rpm = 0.0f;
    phase_rate_q16 = 0;
    dir = (int8_t)-dir;
    backoff_start = g_enc_pos;
    backoff_ms = HAL_GetTick();
    state = ST_BACKOFF;
}

/* 되돌아가지 않고 그 자리에서 감속 후 정지 유지 (이동 시간 초과용) */
static void Stop_Hold_Fault(uint8_t code)
{
    g_fault_reason = code;
    pinch_tgt_pct = g_tgt_pct;
    state = ST_BACKOFF_STOP;
}

/* ===================== 이동 속도/방향 결정 (1ms) ===================== */
static float Move_Goal(void)
{
    if (!enc_sign_known)
    {
        int8_t want_dir = (g_target > 0) ? OPEN_DIR : (int8_t)-OPEN_DIR;
        if (g_target == 0 && g_enc_pos > -800 && g_enc_pos < 800) return -1.0f;   /* 원점 근처: 도착 */
        if (want_dir != dir) { if (cur_rpm > 0.0f) return 0.0f; dir = want_dir; }
        return RPM_FIRST;
    }

    int32_t err = g_target - g_pos;
    int32_t aerr = err < 0 ? -err : err;
    if (aerr <= POS_DEADBAND) return -1.0f;

    int8_t want_dir = (err > 0) ? OPEN_DIR : (int8_t)-OPEN_DIR;
    if (want_dir != dir)
    {
        if (cur_rpm > 0.0f) return 0.0f;
        dir = want_dir;
    }
    float rpm = (float)aerr / (float)ENC_CPR * KP_RPM_PER_REV;
    if (rpm > RPM_MOVE_MAX) rpm = RPM_MOVE_MAX;
    if (rpm < RPM_MOVE_MIN) rpm = RPM_MOVE_MIN;
    return rpm;
}

static void Stepper_1ms(void)
{
    float target_rpm = 0.0f, target_amp = 0.0f;
    uint32_t now = HAL_GetTick();

    if ((state == ST_HOLD || state == ST_IDLE) && g_enc_alive)      /* 각도가 읽힐 때만 이동 시작 */
    {
        int32_t err = g_target - g_pos;
        if (err > HOLD_RESTART || err < -HOLD_RESTART)
        {
            if (state == ST_IDLE) Driver_Start();
            move_start_ms = now;
            state = ST_MOVE;
        }
    }

    /* 각도가 끊기면 위치를 모르니 그 자리에서 멈춤 (다시 읽히면 자동으로 이어서 이동) */
    if (!g_enc_alive)
    {
        if (state == ST_MOVE)    state = ST_STOPPING;
        if (state == ST_BACKOFF) state = ST_BACKOFF_STOP;
    }

    if (MOVE_TIMEOUT_MS > 0U && state == ST_MOVE && (now - move_start_ms) >= MOVE_TIMEOUT_MS)
        Stop_Hold_Fault(2);
    if (state == ST_BACKOFF && (now - backoff_ms) >= BACKOFF_TIMEOUT_MS)
        state = ST_BACKOFF_STOP;

    switch (state)
    {
    case ST_MOVE:
    {
        float g = Move_Goal();
        if (g < 0.0f) { state = ST_STOPPING; target_rpm = 0.0f; target_amp = AMP_RUN; break; }
        target_amp = AMP_RUN;
        target_rpm = (amp >= AMP_RUN * 0.95f) ? g : 0.0f;
        break;
    }
    case ST_BACKOFF:
        target_amp = AMP_RUN;
        target_rpm = PINCH_BACKOFF_RPM;
        break;
    case ST_CAL:
        target_amp = AMP_RUN;
        target_rpm = (amp >= AMP_RUN * 0.95f) ? CAL_RPM : 0.0f;
        break;
    case ST_STOPPING:
    case ST_BACKOFF_STOP: target_rpm = 0.0f; target_amp = AMP_RUN;  break;
    case ST_HOLD:
    case ST_PINCHED:      target_rpm = 0.0f; target_amp = AMP_HOLD; break;
    default:              target_rpm = 0.0f; target_amp = 0.0f;     break;
    }

    float step = ACCEL_RPM_PER_S / 1000.0f;
    if (cur_rpm < target_rpm) { cur_rpm += step; if (cur_rpm > target_rpm) cur_rpm = target_rpm; }
    else if (cur_rpm > target_rpm) { cur_rpm -= step; if (cur_rpm < target_rpm) cur_rpm = target_rpm; }
    Stepper_SetRpm(cur_rpm);

    if (amp < target_amp) { amp += AMP_RAMP_PER_MS; if (amp > target_amp) amp = target_amp; }
    else if (amp > target_amp) { amp -= AMP_RAMP_PER_MS; if (amp < target_amp) amp = target_amp; }

    if (state == ST_BACKOFF)
    {
        int32_t back = g_enc_pos - backoff_start;
        if (back < 0) back = -back;
        if (back >= PINCH_BACKOFF_COUNTS) state = ST_BACKOFF_STOP;
    }

    if (cur_rpm == 0.0f)
    {
        if (state == ST_STOPPING)          state = ST_HOLD;
        else if (state == ST_BACKOFF_STOP) state = ST_PINCHED;
    }

    if (!enc_sign_known && state == ST_MOVE && (g_enc_pos > 800 || g_enc_pos < -800))
    {
        int8_t sgn = (g_enc_pos > 0) ? (int8_t)1 : (int8_t)-1;
        g_enc_sign = (dir == DIR_CW) ? sgn : (int8_t)-sgn;
        enc_sign_known = 1;
    }

    g_pos = g_enc_pos * g_enc_sign * ((OPEN_DIR == DIR_CW) ? 1 : -1);
    int32_t p = g_pos * 100 / WIN_FULL_COUNTS;
    g_pct = (uint8_t)(p < 0 ? 0 : (p > 100 ? 100 : p));
    g_err = g_target - g_pos;

    if (enc_sign_known && g_enc_alive && state != ST_CAL && state != ST_FAULT &&
        (g_pos < -RANGE_MARGIN || g_pos > WIN_FULL_COUNTS + RANGE_MARGIN))
        Enter_Fault(3);

#if USE_ABI_ENCODER
    g_abi_pos = (int32_t)(__HAL_TIM_GET_COUNTER(&htim2) - abi_zero);
#endif
}

/* ===================== 자석 안정도: 정지 중 각도 흔들림 =====================
 * 모터가 유지 전류로 붙잡고 있을 때(HOLD/PINCHED) 각도는 거의 안 변해야 정상.
 * 칩이 '자석 약함' 이라고 해도 각도가 안정적이면 '사용 가능' 으로 봄 */
static void Jitter_Update(uint16_t raw)
{
    static uint16_t ref = 0, n = 0;
    static int32_t  lo = 0, hi = 0;
    static uint8_t  bad = 0, good = 0;
    uint8_t still = (state == ST_HOLD || state == ST_PINCHED) && cur_rpm == 0.0f;

    if (!still) { n = 0; return; }                  /* 움직이는 중엔 판정 유지 */
    if (n == 0) { ref = raw; lo = 0; hi = 0; }
    int32_t e = Wrap14((int32_t)raw - (int32_t)ref);
    if (e < lo) lo = e;
    if (e > hi) hi = e;
    if (++n < JIT_WINDOW_MS) return;
    n = 0;

    g_jitter = (uint16_t)(hi - lo);
    if (g_jitter > JIT_LIMIT) { good = 0; if (++bad  >= JIT_CONFIRM) { bad  = JIT_CONFIRM; g_mag_noisy = 1; } }
    else                      { bad  = 0; if (++good >= JIT_CONFIRM) { good = JIT_CONFIRM; g_mag_noisy = 0; } }
}

/* ===================== 엔코더 추적 + 걸림 감지 ===================== */
static void Encoder_Stall_1ms(void)
{
    static uint8_t  have_prev = 0, rej = 0;
    static uint16_t prev = 0;
    static int32_t  win_start_pos = 0;
    static uint32_t win_ms = 0, still_cnt = 0, diag_ms = 0, at_speed_ms = 0;
    uint32_t now = HAL_GetTick();
    uint16_t raw;

    if (++diag_ms >= ENC_CHECK_MS) { diag_ms = 0; Enc_Probe(); }   /* 표시용 진단 (실패해도 정지 안 함) */
    if (state == ST_FAULT) return;

#if POS_FROM_STEPS
    /* 위치는 모터에 보낸 스텝 수로. 각도는 패널 표시/디버깅용으로만 읽음 */
    if (AS_ReadAngle(&raw)) { g_enc_raw = raw; enc_raw_now = raw; Jitter_Update(raw); enc_last_ok = now; enc_ever = 1; }
    g_enc_pos = Cmd_Counts() - step_zero;
    g_enc_alive = 1;
    if (g_fault_reason == 1) g_fault_reason = 0;
    (void)have_prev; (void)rej; (void)prev; (void)win_start_pos; (void)win_ms; (void)still_cnt; (void)at_speed_ms;
    return;
#endif

    if (AS_ReadAngle(&raw))
    {
        uint16_t a = Enc_Correct(raw);
        if (enc_reprime) { enc_reprime = 0; have_prev = 0; win_start_pos = g_enc_pos; }

        int32_t d = have_prev ? Wrap14((int32_t)a - (int32_t)prev) : 0;
        if ((d > ENC_JUMP_MAX || d < -ENC_JUMP_MAX) && ++rej < ENC_JUMP_CONFIRM)
        {
            g_enc_jump++;                                /* 한 번 크게 튄 값은 버림 */
        }
        else
        {
            rej = 0;
            g_enc_pos += d;
            prev = a;
            have_prev = 1;
            g_enc_raw = raw;
            enc_raw_now = raw;
            Jitter_Update(raw);
            enc_last_ok = now;
            enc_ever = 1;
        }
    }
    else g_enc_err++;

    g_enc_alive = (enc_ever && (now - enc_last_ok) < ENC_LOST_MS) ? 1U : 0U;

    /* 고장코드 1 = 각도 안 읽힘. 다시 읽히면 자동으로 지움 (다른 고장코드는 건드리지 않음) */
    if (!g_enc_alive) { if (g_fault_reason == 0) g_fault_reason = 1; }
    else if (g_fault_reason == 1) g_fault_reason = 0;

    /* 걸림: 모터는 도는데 각도가 거의 안 변함 */
    if (state == ST_MOVE && cur_rpm >= STALL_WATCH_RPM && g_enc_alive)
    {
        if (at_speed_ms < STALL_SETTLE_MS) at_speed_ms++;
    }
    else at_speed_ms = 0;

    if (at_speed_ms < STALL_SETTLE_MS)
    {
        win_start_pos = g_enc_pos; win_ms = 0; still_cnt = 0;
        return;
    }
    if (++win_ms < STALL_WINDOW_MS) return;

    int32_t moved = g_enc_pos - win_start_pos;
    if (moved < 0) moved = -moved;
    win_start_pos = g_enc_pos;
    win_ms = 0;

    if (moved < STALL_MIN_MOVE)
    {
        if (++still_cnt >= STALL_WINDOWS) { still_cnt = 0; at_speed_ms = 0; Pinch_Start(2); }
    }
    else still_cnt = 0;
}

/* ===================== ADC (INA240 / 전압 / 온도) ===================== */
static uint16_t Adc1_Read(uint32_t ch)
{
    ADC_ChannelConfTypeDef c = {0};
    c.Channel = ch; c.Rank = ADC_REGULAR_RANK_1;
    c.SamplingTime = ADC_SAMPLETIME_47CYCLES_5;
    c.SingleDiff = ADC_SINGLE_ENDED; c.OffsetNumber = ADC_OFFSET_NONE; c.Offset = 0;
    HAL_ADC_ConfigChannel(&hadc1, &c);
    HAL_ADC_Start(&hadc1);
    if (HAL_ADC_PollForConversion(&hadc1, 2) != HAL_OK) return 0;
    return (uint16_t)HAL_ADC_GetValue(&hadc1);
}

static float Raw_To_mV(uint16_t raw) { return (float)raw * 3300.0f / 4095.0f; }

static void Current_ZeroOffset(void)
{
    float sum = 0.0f;
    for (int i = 0; i < 200; i++) { sum += Raw_To_mV(Adc1_Read(ISENSE_CHANNEL)); HAL_Delay(1); }
    i_offset_mv = sum / 200.0f;
}

static void Current_1ms(void)
{
    static uint32_t move_ms = 0, over_ms = 0, v_ms = 0;
    static StepState prev_state = ST_IDLE;

    float mv = Raw_To_mV(Adc1_Read(ISENSE_CHANNEL)) - i_offset_mv;
    float ma = fabsf(mv) * 1000.0f / (INA_GAIN * SHUNT_MOHM);
    g_i_ma += (ma - g_i_ma) * 0.05f;

    if (++v_ms >= 100U) { v_ms = 0; g_vbus_mv = (uint32_t)(Raw_To_mV(Adc1_Read(VSENSE_CHANNEL)) * VBUS_DIVIDER); }

    float th = g_i_base * PINCH_RATIO;
    if (th < g_i_base + PINCH_MARGIN_MA) th = g_i_base + PINCH_MARGIN_MA;
    if (th > PINCH_ABS_MA) th = PINCH_ABS_MA;
    g_load_pct = (uint16_t)(th > 1.0f ? (g_i_ma * 100.0f / th) : 0.0f);
    if (g_load_pct > 250U) g_load_pct = 250U;

    uint8_t moving = (state == ST_MOVE) && (amp >= AMP_RUN * 0.95f) && (cur_rpm >= RPM_MOVE_MIN * 0.8f);
    if (!moving) { move_ms = 0; over_ms = 0; prev_state = ST_IDLE; return; }
    if (prev_state != ST_MOVE) { move_ms = 0; g_i_base = g_i_ma; }
    prev_state = ST_MOVE;

    if (move_ms < PINCH_SETTLE_MS)
    {
        move_ms++;
        g_i_base += (g_i_ma - g_i_base) * 0.01f;
        return;
    }

    if (g_i_ma > th || g_i_ma > PINCH_ABS_MA)
    {
        if (++over_ms >= PINCH_CONFIRM_MS) { over_ms = 0; Pinch_Start(5); }
    }
    else
    {
        over_ms = 0;
        g_i_base += (g_i_ma - g_i_base) * 0.001f;
    }
}

/* ---- 온도 ---- */
static float Temp_FromMv(float mv)
{
    float t;
#if TEMP_SENSOR == 0
    float r;
    if (mv < 30.0f || mv > 3270.0f) return -999.0f;          /* 단선 / 단락 */
  #if NTC_ON_GND_SIDE
    r = NTC_FIXED_OHM * mv / (3300.0f - mv);
  #else
    r = NTC_FIXED_OHM * (3300.0f - mv) / mv;
  #endif
    t = 1.0f / (1.0f / 298.15f + logf(r / NTC_R25_OHM) / NTC_BETA) - 273.15f;
#else
    t = (mv - LIN_MV_AT_0C) / LIN_MV_PER_C;
#endif
    if (t < -40.0f || t > 150.0f) return -999.0f;
    return t;
}

static float Temp_Filter(float prev, float now)
{
    if (now < -100.0f)  return -999.0f;
    if (prev < -100.0f) return now;
    return prev + (now - prev) * 0.3f;
}

static uint8_t Temp_ToByte(float t)
{
    int v;
    if (t < -100.0f) return 0x80U;
    v = (int)(t + (t >= 0.0f ? 0.5f : -0.5f));
    if (v > 127) v = 127;
    if (v < -127) v = -127;
    return (uint8_t)(int8_t)v;
}

/* 100ms 마다 한 채널씩 번갈아 읽음 (모터 PWM 갱신을 오래 막지 않게) */
static void Temp_100ms(void)
{
    static uint8_t which = 0;
    uint32_t ch = which ? TEMP_PWR_CHANNEL : TEMP_DRV_CHANNEL;
    (void)Adc1_Read(ch);                  /* 첫 변환은 버림: 직전 채널 전압이 샘플 커패시터에 남아 있음 */
    float mv = (Raw_To_mV(Adc1_Read(ch)) + Raw_To_mV(Adc1_Read(ch))) * 0.5f;
    if (which) { g_tpwr_mv = mv; g_tpwr_c = Temp_Filter(g_tpwr_c, Temp_FromMv(mv)); }
    else       { g_tdrv_mv = mv; g_tdrv_c = Temp_Filter(g_tdrv_c, Temp_FromMv(mv)); }
    which ^= 1U;
}

/* ===================== 엔코더 보정 진행 ===================== */
static int32_t Cmd_Counts(void)
{
    return (int32_t)(cmd_phase_total * ENC_CPR / (4294967296LL * (int64_t)ELEC_CYCLES_PER_REV));
}

static void Cal_Start(void)
{
    if (state == ST_IDLE) Driver_Start();
    cur_rpm = 0.0f;
    dir = DIR_CW;
    cal_pass = 0;
    cal_k = 0;
    cal_cmd0 = Cmd_Counts();
    cal_start_ms = HAL_GetTick();
    g_cal_state = 1;
    state = ST_CAL;
}

static void Cal_1ms(void)
{
    if (state != ST_CAL) return;

    if ((HAL_GetTick() - cal_start_ms) >= CAL_TIMEOUT_MS || !g_enc_alive)   /* 시간 초과 / 각도 끊김 = 실패 */
    {
        g_cal_state = 3;
        state = ST_STOPPING;
        return;
    }

    int32_t rel = (Cmd_Counts() - cal_cmd0) * DIR_CW;
    int32_t step = ENC_CPR / CAL_POINTS;

    if (cal_pass == 0)
    {
        while (cal_k <= CAL_POINTS && rel >= cal_k * step) { cal_raw_f[cal_k] = enc_raw_now; cal_k++; }
        if (cal_k > CAL_POINTS) { cal_pass = 1; cal_k = CAL_POINTS; dir = (int8_t)-dir; }
    }
    else
    {
        while (cal_k >= 0 && rel <= cal_k * step) { cal_raw_b[cal_k] = enc_raw_now; cal_k--; }
        if (cal_k < 0)
        {
            /* 보정 전후로 0도 기준 각도가 얼마나 바뀌는지 보고 누적 위치를 그만큼 맞춤 (이미 보정된 상태여도 맞음) */
            int32_t e_old = Wrap14((int32_t)Enc_Correct(enc_raw_now) - (int32_t)Enc_Correct(zero_raw));
            if (Cal_Build())
            {
                cal_valid = 1;
                int32_t e_new = Wrap14((int32_t)Enc_Correct(enc_raw_now) - (int32_t)Enc_Correct(zero_raw));
                g_enc_pos += Wrap14(e_new - e_old);
                enc_reprime = 1;
                cal_dirty = 1;
                g_cal_state = 2;
            }
            else g_cal_state = 3;
            state = ST_STOPPING;
        }
    }
}

/* ===================== SW(PB13) ===================== */
static void Button_1ms(void)
{
    static GPIO_PinState prev = GPIO_PIN_SET;
    static uint32_t lock_until = 0, t_down = 0;
    static uint8_t  long_fired = 0, pressed = 0;
    GPIO_PinState now = HAL_GPIO_ReadPin(SW1_PORT, SW1_PIN);
    uint32_t t = HAL_GetTick();

    if (prev == GPIO_PIN_SET && now == GPIO_PIN_RESET && t >= lock_until)
    {
        t_down = t; pressed = 1; long_fired = 0;
    }

    if (pressed && now == GPIO_PIN_RESET)
    {
        uint32_t held = t - t_down;
        g_btn_level = (held >= CAL_LONGPRESS_MS) ? 2U : (held >= SW_ZERO_MS) ? 1U : 0U;
        /* 5초: 엔코더 보정 */
        if (!long_fired && held >= CAL_LONGPRESS_MS)
        {
            long_fired = 1;
            if (!POS_FROM_STEPS && (state == ST_IDLE || state == ST_HOLD || state == ST_PINCHED) && g_enc_alive) Cal_Start();
        }
    }

    if (pressed && prev == GPIO_PIN_RESET && now == GPIO_PIN_SET)
    {
        uint32_t held = t - t_down;
        pressed = 0;
        g_btn_level = 0;
        lock_until = t + 100;
        if (long_fired || held < 30U) { prev = now; return; }

        if (held >= SW_ZERO_MS)
        {
            /* 2~5초 후 뗌: 지금 자리를 0도로 저장 */
            if (state == ST_HOLD || state == ST_IDLE || state == ST_PINCHED)
            {
                if (state == ST_PINCHED) { g_fault_reason = 0; state = ST_HOLD; }
                Zero_Here();
                zero_dirty = 1;                       /* 멈춰 있을 때 플래시에 저장 (방향 학습 전이면 학습 뒤에) */
                led_ack_until = t + 600U;
            }
        }
        else switch (state)
        {
        case ST_PINCHED:                              /* 짧게: 끼임 해제 -> 마지막 목표로 */
            g_fault_reason = 0;
            state = ST_HOLD;
            break;
        case ST_HOLD:
        case ST_IDLE:                                 /* 짧게: 0도(창문 0%)로 복귀 */
            g_tgt_pct = 0;
            g_target = 0;
            break;
        case ST_FAULT:                                /* 짧게: 고장 해제 + 저장된 0도에 맞춤 */
            if (g_fault_reason != 4U)
            {
                g_fault_reason = 0;
                Zero_Sync();
                Driver_Start();
                state = ST_HOLD;
            }
            break;
        default:
            break;
        }
    }
    prev = now;
}

/* ===================== 환풍기 PWM ===================== */
#if FAN_ON_THIS_NODE
#define FAN_PSC   ((FAN_PWM_HZ >= 2000U) ? 0U : 79U)
#define FAN_ARR   (((TIM_CLK_HZ / (FAN_PSC + 1U)) / FAN_PWM_HZ) - 1U)
static void Fan_Write(uint8_t pct)
{
    uint32_t ccr = (uint32_t)pct * (FAN_ARR + 1U) / 100U;
#if FAN_INVERT
    ccr = (FAN_ARR + 1U) - ccr;
#endif
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, ccr);
}
static void Fan_1ms(void)
{
    static uint16_t acc = 0;
    uint32_t now = HAL_GetTick();

    if (g_fan_now == 0 && fan_tgt > 0 && fan_kick_until == 0) fan_kick_until = now + FAN_KICK_MS;
    if (fan_kick_until && (int32_t)(now - fan_kick_until) < 0 && fan_tgt > 0)
    {
        Fan_Write(100);
        if (g_fan_now == 0) g_fan_now = 1;
        return;
    }
    if (fan_tgt == 0) fan_kick_until = 0;

    if (g_fan_now != fan_tgt)
    {
        uint16_t need = (fan_tgt > g_fan_now) ? FAN_UP_MS_PER_PCT : FAN_DOWN_MS_PER_PCT;
        if (++acc >= need)
        {
            acc = 0;
            g_fan_now = (fan_tgt > g_fan_now) ? (uint8_t)(g_fan_now + 1U) : (uint8_t)(g_fan_now - 1U);
            if (g_fan_now == 0) fan_kick_until = 0;
        }
    }
    else acc = 0;
    Fan_Write(g_fan_now);
}
#endif

/* ===================== MQ-7 일산화탄소 (PC5 / ADC2_IN11) =====================
 *  마스터에서 쓰던 방식 그대로: 예열 뒤 깨끗한 공기의 전압을 기준선으로 잡고,
 *  기준선보다 몇 배 올랐는지로 ppm 을 추정 (절대 정밀값이 아닌 상대 지표) */
volatile int32_t  g_co_ppm = 0, g_co_mv = 0, g_co_base_mv = 0;
volatile uint8_t  g_co_warm = 0, g_co_sensor_ok = 0;
static float      co_f = 0.0f, co_base = 0.0f;

static void Co_100ms(void)
{
    float mv;
    HAL_ADC_Start(&hadc2);
    if (HAL_ADC_PollForConversion(&hadc2, 2) != HAL_OK) return;
    mv = (float)HAL_ADC_GetValue(&hadc2) * 3300.0f / 4095.0f * CO_DIVIDER;
    co_f = (co_f <= 0.0f) ? mv : co_f + (mv - co_f) * 0.1f;
}

static void Co_1s(void)
{
    g_co_mv = (int32_t)co_f;
    g_co_sensor_ok = (co_f > 30.0f && co_f < 3270.0f * CO_DIVIDER) ? 1U : 0U;   /* 0V/3.3V 붙음 = 단선/단락 */
    if (!g_co_warm)
    {
        if (HAL_GetTick() >= CO_WARMUP_MS) { g_co_warm = 1; co_base = co_f; }
        g_co_ppm = 0;
    }
    else
    {
        if (co_f < co_base) co_base = co_f; else co_base *= 1.0002f;    /* 기준선은 천천히 따라 올라감 */
        float ratio = (co_base > 10.0f) ? (co_f / co_base) : 1.0f;
        float ppm = (ratio - 1.0f) * CO_PPM_PER_RATIO;
        g_co_ppm = (int32_t)(ppm > 0.0f ? ppm + 0.5f : 0.0f);
        if (g_co_ppm > 999) g_co_ppm = 999;
    }
    g_co_base_mv = (int32_t)co_base;
}

/* ===================== ENS160 (I2C4: PC6 SCL / PC7 SDA) =====================
 *  TVOC(ppb) / eCO2(ppm) / AQI(1~5). 전원 켤 때마다 약 3분 예열(유효도 1), 첫 사용 1시간은 유효도 2 */
#define ENS_REG_PARTID   0x00U
#define ENS_REG_OPMODE   0x10U
#define ENS_REG_COMMAND  0x12U
#define ENS_REG_TEMPIN   0x13U
#define ENS_REG_STATUS   0x20U       /* 0x20 상태, 0x21 AQI, 0x22~23 TVOC, 0x24~25 eCO2 */
#define ENS_PART_ID      0x0160U

static uint16_t   ens_addr = 0;      /* HAL 용 8비트 주소, 0 = 못 찾음 */
volatile uint8_t  g_ens_ok = 0, g_ens_valid = 3, g_ens_aqi = 0, g_ens_addr7 = 0;
volatile uint16_t g_tvoc = 0, g_eco2 = 0;
volatile uint32_t g_ens_err = 0;
static int16_t    env_t10 = 250;     /* 보정용 온도 x10 (마스터 DHT22, 없으면 25.0도) */
static uint16_t   env_h10 = 500;     /* 보정용 습도 x10 (없으면 50.0%) */
static uint8_t    env_new = 0;

static HAL_StatusTypeDef Ens_Wr(uint8_t reg, uint8_t *d, uint16_t n)
{
    return HAL_I2C_Mem_Write(&hi2c4, ens_addr, reg, I2C_MEMADD_SIZE_8BIT, d, n, 5);
}
static HAL_StatusTypeDef Ens_Rd(uint8_t reg, uint8_t *d, uint16_t n)
{
    return HAL_I2C_Mem_Read(&hi2c4, ens_addr, reg, I2C_MEMADD_SIZE_8BIT, d, n, 5);
}

static uint8_t Ens_Start(void)          /* 찾기 -> 리셋 -> 대기 -> 표준 측정 모드. 성공하면 1 */
{
    static const uint8_t cand[2] = { 0x53U, 0x52U };
    uint8_t id[2], v;
    ens_addr = 0;
    for (int i = 0; i < 2 && ens_addr == 0; i++)
    {
        ens_addr = (uint16_t)(cand[i] << 1);
        if (Ens_Rd(ENS_REG_PARTID, id, 2) != HAL_OK || (uint16_t)(id[0] | (id[1] << 8)) != ENS_PART_ID) ens_addr = 0;
        else g_ens_addr7 = cand[i];
    }
    if (ens_addr == 0) { g_ens_addr7 = 0; return 0; }
    v = 0xF0U; Ens_Wr(ENS_REG_OPMODE, &v, 1);  HAL_Delay(15);     /* 리셋 */
    v = 0x01U; Ens_Wr(ENS_REG_OPMODE, &v, 1);  HAL_Delay(5);      /* 대기(IDLE) */
    v = 0xCCU; Ens_Wr(ENS_REG_COMMAND, &v, 1); HAL_Delay(5);      /* 범용 레지스터 지우기 */
    v = 0x02U; if (Ens_Wr(ENS_REG_OPMODE, &v, 1) != HAL_OK) return 0;   /* 표준 측정 */
    HAL_Delay(20);
    env_new = 1;
    return 1;
}

/* 1초마다: 보정값 쓰기 + 상태/측정값 읽기. 끊기면 모터가 멈춰 있을 때만 5초마다 다시 시작 (재시작에 약 50ms 걸림) */
static void Ens_1s(void)
{
    static uint8_t  bad = 0;
    static uint32_t retry_at = 0;
    uint8_t d[6];

    if (!g_ens_ok)
    {
        uint8_t still = (state == ST_HOLD || state == ST_IDLE || state == ST_FAULT || state == ST_PINCHED) && cur_rpm == 0.0f;
        if (still && (int32_t)(HAL_GetTick() - retry_at) >= 0)
        {
            retry_at = HAL_GetTick() + 5000U;
            HAL_I2C_DeInit(&hi2c4);
            MX_I2C4_Init();
            g_ens_ok = Ens_Start();
            bad = 0;
        }
        return;
    }

    if (env_new)                         /* TEMP_IN = (T + 273.15) x 64,  RH_IN = RH x 512 */
    {
        uint16_t tk = (uint16_t)((int32_t)env_t10 * 32 / 5 + 17482);
        uint16_t rh = (uint16_t)((uint32_t)env_h10 * 256U / 5U);
        uint8_t  w[4] = { (uint8_t)tk, (uint8_t)(tk >> 8), (uint8_t)rh, (uint8_t)(rh >> 8) };
        if (Ens_Wr(ENS_REG_TEMPIN, w, 4) == HAL_OK) env_new = 0;
    }

    if (Ens_Rd(ENS_REG_STATUS, d, 6) != HAL_OK)
    {
        g_ens_err++;
        if (++bad >= 5U) { g_ens_ok = 0; g_ens_valid = 3; }
        return;
    }
    bad = 0;
    if (d[0] & 0x40U) g_ens_err++;                  /* STATER: 측정 오류 */
    g_ens_valid = (uint8_t)((d[0] >> 2) & 0x03U);
    if ((d[0] & 0x02U) || g_eco2 == 0U)             /* NEWDAT */
    {
        g_ens_aqi = (uint8_t)(d[1] & 0x07U);
        g_tvoc    = (uint16_t)(d[2] | (d[3] << 8));
        g_eco2    = (uint16_t)(d[4] | (d[5] << 8));
    }
}

/* ===================== CAN ===================== */
static FDCAN_HandleTypeDef *can_up = 0;     /* 마스터 쪽 */
static FDCAN_HandleTypeDef *can_dn = 0;     /* 반대쪽 (게이트웨이일 때만) */
volatile uint32_t g_gw_down = 0, g_gw_up = 0;   /* 중계한 메시지 수 (디버깅) */

static void CAN_SendOn(FDCAN_HandleTypeDef *hc, uint32_t id, uint8_t *d)
{
    FDCAN_TxHeaderTypeDef h = {0};
    if (hc == 0 || HAL_FDCAN_GetTxFifoFreeLevel(hc) == 0) return;
    h.Identifier = id; h.IdType = FDCAN_STANDARD_ID; h.TxFrameType = FDCAN_DATA_FRAME;
    h.DataLength = FDCAN_DLC_BYTES_8; h.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
    h.BitRateSwitch = FDCAN_BRS_OFF; h.FDFormat = FDCAN_CLASSIC_CAN;
    h.TxEventFifoControl = FDCAN_NO_TX_EVENTS; h.MessageMarker = 0;
    HAL_FDCAN_AddMessageToTxFifoQ(hc, &h, d);
}

/* 자기 메시지는 위쪽 버스로 보내고, 게이트웨이면 아래쪽 버스에도 보냄 */
static void CAN_SendBoth(uint32_t id, uint8_t *d)
{
    CAN_SendOn(can_up, id, d);
#if CAN_GATEWAY
    CAN_SendOn(can_dn, id, d);
#endif
}

static void CAN_BusOffRecover(FDCAN_HandleTypeDef *hc)
{
    if (hc && (hc->Instance->PSR & FDCAN_PSR_BO)) CLEAR_BIT(hc->Instance->CCCR, FDCAN_CCCR_INIT);
}

/* 이 노드가 직접 보내는 ID (중계하지 않음) */
static uint8_t CAN_IsMine(uint32_t id)
{
    if (id == CAN_ID_ME || id == CAN_ID_ST || id == CAN_ID_AIR) return 1;
#if FAN_ON_THIS_NODE
    if (id == CAN_ID_FAN) return 1;
#endif
    return 0;
}

static void CAN_Poll(void)
{
    FDCAN_RxHeaderTypeDef h;
    uint8_t d[8];

    /* 위쪽(마스터 쪽) 버스 */
    while (HAL_FDCAN_GetRxFifoFillLevel(can_up, FDCAN_RX_FIFO0) > 0)
    {
        if (HAL_FDCAN_GetRxMessage(can_up, FDCAN_RX_FIFO0, &h, d) != HAL_OK) break;
        if (CAN_IsMine(h.Identifier)) continue;
#if CAN_GATEWAY
        CAN_SendOn(can_dn, h.Identifier, d);        /* 위 -> 아래 : 전부 */
        g_gw_down++;
#endif
        if (h.Identifier == CAN_ID_ENV)               /* 마스터 온습도 -> ENS160 보정 */
        {
            if (d[6] & 0x01U)
            {
                int16_t  t = (int16_t)((d[2] << 8) | d[3]);
                uint16_t r = (uint16_t)((d[4] << 8) | d[5]);
                if (t != env_t10 || r != env_h10) { env_t10 = t; env_h10 = r; env_new = 1; }
            }
            continue;
        }
        if (h.Identifier != CAN_ID_CMD) continue;
        {
            static uint8_t last_cmd_pct = 0xFFU;      /* 마지막으로 적용한 마스터 목표 */
            uint8_t raw = (d[4] & 0x02U) ? d[4 + NODE_ID] : d[1];   /* 구역별 목표가 있으면 내 구역 것 */
            uint8_t p = Snap_Pct(raw > 100 ? 100 : raw);
            if (p != last_cmd_pct && state != ST_CAL && state != ST_FAULT && Set_Target_Pct(p)) last_cmd_pct = p;
        }
        fan_tgt = d[2] > 100 ? 100 : d[2];
        if (d[4] & 0x02U) g_zone_lv = (uint8_t)((d[7] >> ((NODE_ID - 1) * 2)) & 0x03U);   /* 구역 등급 묶음 */
        last_cmd_ms = HAL_GetTick();
        got_cmd = 1;
    }
#if CAN_GATEWAY
    /* 아래쪽 버스 */
    while (HAL_FDCAN_GetRxFifoFillLevel(can_dn, FDCAN_RX_FIFO0) > 0)
    {
        if (HAL_FDCAN_GetRxMessage(can_dn, FDCAN_RX_FIFO0, &h, d) != HAL_OK) break;
        if (CAN_IsMine(h.Identifier)) continue;
        if (h.Identifier == CAN_ID_CMD || h.Identifier == CAN_ID_MST_ST || h.Identifier == CAN_ID_ENV) continue;   /* 마스터 것은 위로 안 올림 */
        CAN_SendOn(can_up, h.Identifier, d);        /* 아래 -> 위 */
        g_gw_up++;
    }
    CAN_BusOffRecover(can_dn);
#endif
    CAN_BusOffRecover(can_up);
}

/* 0x100+NODE_ID 하트비트 (+ 팬 노드면 0x201) */
static void CAN_Tx100ms(void)
{
    uint8_t st = (state == ST_FAULT || state == ST_PINCHED || state == ST_BACKOFF || state == ST_BACKOFF_STOP) ? CAN_ST_FAULT
               : (state == ST_MOVE || state == ST_STOPPING || state == ST_CAL) ? CAN_ST_MOVE : CAN_ST_HOLD;
    uint16_t ma = (uint16_t)(g_i_ma > 65535.0f ? 65535.0f : g_i_ma);
    uint8_t d[8] = { st, g_pct, g_tgt_pct, g_fault_reason, (uint8_t)(g_load_pct > 250U ? 250U : g_load_pct),
                     (uint8_t)(ma >> 8), (uint8_t)(ma & 0xFFU), hb_seq };
    CAN_SendBoth(CAN_ID_ME, d);
#if FAN_ON_THIS_NODE
    uint8_t f[8] = { g_fan_now, fan_tgt, 0, 0, 0, 0, 0, hb_seq };
    CAN_SendBoth(CAN_ID_FAN, f);
#endif
    hb_seq++;
}

/* 0x110+NODE_ID 확장 상태 : 엔코더 절대각 / 자석 / 전압 / 온도 */
static void CAN_TxStatus100ms(void)
{
    uint8_t magl = 0, magh = 0, cof = 0, fl;
    uint16_t ang, mv;

    if (state == ST_FAULT) Enc_Probe();   /* 고장 중엔 엔코더 처리를 안 하므로 진단만 갱신 */
    if (g_enc_present)
    {
        magl = (g_diag >> 11) & 1U; magh = (g_diag >> 10) & 1U; cof = (g_diag >> 9) & 1U;
    }
    /* bit1 '자석 사용 가능' = 각도가 읽히고 정지 중 흔들림이 작음 (칩의 MAGL 은 bit2 로 참고만) */
    fl = (uint8_t)((g_enc_alive ? 0x01U : 0U) | ((g_enc_alive && !g_mag_noisy) ? 0x02U : 0U) |
                   (magl ? 0x04U : 0U) | (magh ? 0x08U : 0U) | (cof ? 0x10U : 0U) |
                   (cal_valid ? 0x20U : 0U) | ((g_enc_alive && g_mag_noisy) ? 0x40U : 0U));
    {
        int32_t r = g_pos % ENC_CPR;      /* 0도 기준, 여는 방향 + */
        if (r < 0) r += ENC_CPR;
        ang = (uint16_t)r;
    }
    mv  = (uint16_t)(g_vbus_mv > 65535U ? 65535U : g_vbus_mv);

    uint8_t d[8] = { g_agc, fl, (uint8_t)(ang >> 8), (uint8_t)ang, (uint8_t)(mv >> 8), (uint8_t)mv,
                     Temp_ToByte(g_tdrv_c), Temp_ToByte(g_tpwr_c) };
    CAN_SendBoth(CAN_ID_ST, d);
}

/* 0x120+NODE_ID 구역 공기질 */
static void CAN_TxAir(void)
{
    uint16_t co = (uint16_t)(g_co_ppm < 0 ? 0 : g_co_ppm);
    uint16_t tv = g_ens_ok ? g_tvoc : 0U, ec = g_ens_ok ? g_eco2 : 0U;
    uint8_t  fl = (uint8_t)((g_ens_ok ? 0x01U : 0U) | ((g_ens_valid & 0x03U) << 1) |
                            (g_co_warm ? 0x08U : 0U) | (g_co_sensor_ok ? 0x10U : 0U));
    uint8_t  d[8] = { (uint8_t)(co >> 8), (uint8_t)co, (uint8_t)(tv >> 8), (uint8_t)tv,
                      (uint8_t)(ec >> 8), (uint8_t)ec, g_ens_ok ? g_ens_aqi : 0U, fl };
    CAN_SendBoth(CAN_ID_AIR, d);
}

/* ===================== 구역 경고 부저 (PB11) =====================
 *  등급이 NODE_BUZZ_LEVEL 이상으로 "새로 오를 때" NODE_BUZZ_COUNT 번.
 *  다 울린 뒤 5초 안에는 안 울리고, 5초 뒤에도 등급이 마지막으로 알린 것보다 높으면 다시 울림.
 *  등급이 내려가면 기준도 같이 내려가서, 다시 오르면 또 알림 */
static void Buzz_Set(uint8_t on)
{
    g_buzz_on = on;
    HAL_GPIO_WritePin(BUZ_PORT, BUZ_PIN, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void Buzz_1ms(void)
{
    static uint8_t  left = 0, on = 0, announced = 0, ever = 0;
    static uint32_t t0 = 0, done_ms = 0;
    uint32_t now = HAL_GetTick();

    if (left)                                        /* 울리는 중 */
    {
        if (on && (now - t0) >= NODE_BUZZ_ON_MS)
        {
            Buzz_Set(0); on = 0; t0 = now;
            if (--left == 0) { done_ms = now; ever = 1; }
        }
        else if (!on && (now - t0) >= NODE_BUZZ_OFF_MS) { Buzz_Set(1); on = 1; t0 = now; }
        return;
    }

    if (!got_cmd || (now - last_cmd_ms) > 1000U) return;      /* 마스터 명령이 끊기면 판단 안 함 */
    uint8_t lv = g_zone_lv;
    if (lv < announced) announced = lv;                         /* 내려가면 기준도 내림 */
    if (lv >= NODE_BUZZ_LEVEL && lv > announced && (!ever || (now - done_ms) >= NODE_BUZZ_LOCK_MS))
    {
        announced = lv;
        left = NODE_BUZZ_COUNT; on = 1; t0 = now;
        Buzz_Set(1);
        g_buzz_cnt++;
    }
}

/* ===================== 고장 감시 + LED ===================== */
static void Fault_Led_1ms(void)
{
    if (USE_NFAULT_CHECK && HAL_GetTick() >= 500U && state != ST_FAULT)
    {
        static uint16_t low_ms = 0;
        low_ms = (HAL_GPIO_ReadPin(DRV_NFAULT_PORT, DRV_NFAULT_PIN) == GPIO_PIN_RESET) ? low_ms + 1U : 0U;
        if (low_ms >= 5U) Enter_Fault(4);
    }

    uint32_t t = HAL_GetTick();
    uint8_t cmd_lost = !got_cmd || (t - last_cmd_ms) > 1000U;
    GPIO_PinState run = GPIO_PIN_RESET, red = GPIO_PIN_RESET;
    GPIO_PinState normal = (state == ST_FAULT || cmd_lost) ? GPIO_PIN_RESET : GPIO_PIN_SET;

    switch (state)
    {
    case ST_MOVE:
    case ST_STOPPING:     run = GPIO_PIN_SET; break;
    case ST_CAL:          run = ((t / 250U) & 1U) ? GPIO_PIN_SET : GPIO_PIN_RESET; break;
    case ST_BACKOFF:
    case ST_BACKOFF_STOP:
    case ST_PINCHED:      red = ((t / 500U) & 1U) ? GPIO_PIN_SET : GPIO_PIN_RESET; break;
    case ST_FAULT:        red = ((t / 100U) & 1U) ? GPIO_PIN_SET : GPIO_PIN_RESET; break;
    default:              break;
    }
    /* 각도가 안 읽히면 빨간 LED 250ms 간격 깜빡 */
    if (!g_enc_alive && state != ST_FAULT && red == GPIO_PIN_RESET)
        red = ((t / 250U) & 1U) ? GPIO_PIN_SET : GPIO_PIN_RESET;
    if (cmd_lost && state != ST_FAULT && red == GPIO_PIN_RESET)
        red = ((t % 1000U) < 80U) ? GPIO_PIN_SET : GPIO_PIN_RESET;

    if (g_btn_level == 1U) { normal = GPIO_PIN_SET; run = GPIO_PIN_SET; }      /* 지금 떼면 0도 저장 */
    if ((int32_t)(led_ack_until - t) > 0)                                       /* 저장 확인: 빠르게 3번 */
    {
        GPIO_PinState b = ((t / 100U) & 1U) ? GPIO_PIN_SET : GPIO_PIN_RESET;
        normal = run = red = b;
    }

    HAL_GPIO_WritePin(LED_RUN_PORT,    LED_RUN_PIN,    run);
    HAL_GPIO_WritePin(LED_NORMAL_PORT, LED_NORMAL_PIN, normal);
    HAL_GPIO_WritePin(LED_RED_PORT,    LED_RED_PIN,    red);
}

/* ===================== main ===================== */
int main(void)
{
    HAL_Init();
    SystemClock_Config();

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    MX_GPIO_Init();
    MX_SPI1_Init();
#if USE_ABI_ENCODER
    MX_TIM2_Encoder_Init();
#endif
#if FAN_ON_THIS_NODE
    MX_TIM3_FanPwm_Init();
#endif
    Stepper_BuildTable();
    MX_TIM1_Init();
    MX_ADC1_Init();
    MX_ADC2_Init();
    MX_I2C4_Init();
    CAN_InitAll();
    g_ens_ok = Ens_Start();               /* ENS160 없으면 0 -> 나중에 5초마다 다시 찾음 */

    /* DRV8313 깨우기 (nSLEEP HIGH). EN은 아직 LOW라 출력은 꺼진 상태 */
    HAL_GPIO_WritePin(DRV_NSLP_PORT, DRV_NSLP_PIN, GPIO_PIN_SET);
    HAL_Delay(20);

    Current_ZeroOffset();                 /* 모터 출력 꺼진 상태에서 전류 영점 */

    /* 자석 진단이 안 좋아도 고장으로 멈추지 않음: 각도가 읽히기 시작하면 그때부터 이동 */
    Enc_Probe();
    g_cal_saved = Cal_Load();             /* 저장된 엔코더 보정이 있으면 적용 (0도 맞추기 전에) */
    Zero_Sync();                          /* 저장된 0도에 맞춤 (없으면 켠 자리 = 0) */
    Driver_Start();
    state = ST_HOLD;

    last_cyc = DWT->CYCCNT;
    uint32_t last_ms = HAL_GetTick(), t_100 = last_ms, t_st = last_ms + 50U;   /* 상태는 50ms 엇갈려 송신 */
    uint32_t t_co = last_ms + 25U, t_air = last_ms + 75U, t_1s = last_ms + 500U;

    while (1)
    {
        if (TIM1->SR & TIM_SR_UIF)
        {
            TIM1->SR = ~TIM_SR_UIF;
            if (state != ST_FAULT) Stepper_PwmUpdate();
#if NODE_BUZZ_PASSIVE
            {   /* 수동형 부저: TIM1 20kHz 갱신 4번마다 뒤집기 = 약 2.5kHz */
                static uint8_t div = 0;
                if (g_buzz_on) { if (++div >= 4U) { div = 0; BUZ_PORT->ODR ^= BUZ_PIN; } }
            }
#endif
        }
        CAN_Poll();

        uint32_t now = HAL_GetTick();
        if (now != last_ms)
        {
            last_ms = now;
            Encoder_Stall_1ms();
            Current_1ms();
            Cal_1ms();
            Stepper_1ms();
            Button_1ms();
            Fault_Led_1ms();
            Zero_Service();
            Cal_Service();
            Buzz_1ms();
#if FAN_ON_THIS_NODE
            Fan_1ms();
#endif
        }
        if ((now - t_100) >= 100U) { t_100 = now; CAN_Tx100ms(); }
        if ((int32_t)(now - t_st) >= 100) { t_st = now; Temp_100ms(); CAN_TxStatus100ms(); }
        if ((int32_t)(now - t_co) >= 100) { t_co = now; Co_100ms(); }
        if ((int32_t)(now - t_1s) >= 1000) { t_1s = now; Co_1s(); Ens_1s(); }
        if ((int32_t)(now - t_air) >= (int32_t)AIR_TX_MS) { t_air = now; CAN_TxAir(); }
    }
}

/* ===================== 더미 인터럽트 콜백 함수 ===================== */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) { (void)huart; }
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) { (void)htim; }
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc) { (void)hadc; }
void HAL_I2C_MasterRxCpltCallback(I2C_HandleTypeDef *hi2c) { (void)hi2c; }

/* ===================== 초기화 ===================== */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1);

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
    RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV1;
    RCC_OscInitStruct.PLL.PLLN = 10;
    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
    RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
    RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) Error_Handler();

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK) Error_Handler();
}

static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef g = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    HAL_GPIO_WritePin(DRV_EN_PORT,     DRV_EN_PIN,     GPIO_PIN_RESET);
    HAL_GPIO_WritePin(DRV_NSLP_PORT,   DRV_NSLP_PIN,   GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED_RUN_PORT,    LED_RUN_PIN,    GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED_NORMAL_PORT, LED_NORMAL_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED_RED_PORT,    LED_RED_PIN,    GPIO_PIN_RESET);
    HAL_GPIO_WritePin(BUZ_PORT,        BUZ_PIN,        GPIO_PIN_RESET);
    HAL_GPIO_WritePin(ENC_CS_PORT,     ENC_CS_PIN,     GPIO_PIN_SET);

    g.Mode = GPIO_MODE_OUTPUT_PP;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    g.Pin = DRV_EN_PIN | DRV_NSLP_PIN | LED_RED_PIN | BUZ_PIN;
    HAL_GPIO_Init(GPIOB, &g);
    g.Pin = LED_RUN_PIN | LED_NORMAL_PIN;
    HAL_GPIO_Init(GPIOC, &g);
    g.Pin = ENC_CS_PIN;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(ENC_CS_PORT, &g);

    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_PULLUP;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    g.Pin = DRV_NFAULT_PIN | SW1_PIN;
    HAL_GPIO_Init(GPIOB, &g);
}

static void MX_SPI1_Init(void)
{
    GPIO_InitTypeDef g = {0};

    __HAL_RCC_SPI1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    g.Pin = GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7;
    g.Mode = GPIO_MODE_AF_PP;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    g.Alternate = GPIO_AF5_SPI1;
    HAL_GPIO_Init(GPIOA, &g);

    hspi1.Instance = SPI1;
    hspi1.Init.Mode = SPI_MODE_MASTER;
    hspi1.Init.Direction = SPI_DIRECTION_2LINES;
    hspi1.Init.DataSize = SPI_DATASIZE_16BIT;
    hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
    hspi1.Init.CLKPhase = SPI_PHASE_2EDGE;
    hspi1.Init.NSS = SPI_NSS_SOFT;
    hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_32;   /* 2.5MHz: 선이 길어도 덜 깨지게 (v5 는 /16) */
    hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
    hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
    hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    hspi1.Init.CRCPolynomial = 7;
    hspi1.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
    if (HAL_SPI_Init(&hspi1) != HAL_OK) Error_Handler();
}

#if USE_ABI_ENCODER
/* TIM2 엔코더 모드: PA0 = TIM2_CH1(A), PA1 = TIM2_CH2(B), 4체배 (참고용) */
static void MX_TIM2_Encoder_Init(void)
{
    GPIO_InitTypeDef g = {0};
    TIM_Encoder_InitTypeDef enc = {0};
    TIM_MasterConfigTypeDef mst = {0};

    __HAL_RCC_TIM2_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    g.Pin = GPIO_PIN_0 | GPIO_PIN_1;
    g.Mode = GPIO_MODE_AF_PP;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    g.Alternate = GPIO_AF1_TIM2;
    HAL_GPIO_Init(GPIOA, &g);

    htim2.Instance = TIM2;
    htim2.Init.Prescaler = 0;
    htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim2.Init.Period = 0xFFFFFFFFU;
    htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

    enc.EncoderMode = TIM_ENCODERMODE_TI12;
    enc.IC1Polarity = TIM_ICPOLARITY_RISING;
    enc.IC1Selection = TIM_ICSELECTION_DIRECTTI;
    enc.IC1Prescaler = TIM_ICPSC_DIV1;
    enc.IC1Filter = 4;
    enc.IC2Polarity = TIM_ICPOLARITY_RISING;
    enc.IC2Selection = TIM_ICSELECTION_DIRECTTI;
    enc.IC2Prescaler = TIM_ICPSC_DIV1;
    enc.IC2Filter = 4;
    if (HAL_TIM_Encoder_Init(&htim2, &enc) != HAL_OK) Error_Handler();

    mst.MasterOutputTrigger = TIM_TRGO_RESET;
    mst.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
    if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &mst) != HAL_OK) Error_Handler();

    __HAL_TIM_SET_COUNTER(&htim2, 0);
    HAL_TIM_Encoder_Start(&htim2, TIM_CHANNEL_ALL);
}
#endif

#if FAN_ON_THIS_NODE
/* TIM3_CH1 PWM: PB4 (SERVO_PWM) -> C1815 -> 팬 */
static void MX_TIM3_FanPwm_Init(void)
{
    GPIO_InitTypeDef g = {0};
    TIM_OC_InitTypeDef oc = {0};
    TIM_SlaveConfigTypeDef sl = {0};

    __HAL_RCC_TIM3_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    g.Pin = GPIO_PIN_4;
    g.Mode = GPIO_MODE_AF_PP;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    g.Alternate = GPIO_AF2_TIM3;
    HAL_GPIO_Init(GPIOB, &g);

    htim3.Instance = TIM3;
    htim3.Init.Prescaler = FAN_PSC;
    htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim3.Init.Period = FAN_ARR;
    htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    if (HAL_TIM_PWM_Init(&htim3) != HAL_OK) Error_Handler();

    sl.SlaveMode = TIM_SLAVEMODE_DISABLE;
    sl.InputTrigger = TIM_TS_ITR0;
    HAL_TIM_SlaveConfigSynchro(&htim3, &sl);

    oc.OCMode = TIM_OCMODE_PWM1;
    oc.Pulse = 0;
    oc.OCPolarity = TIM_OCPOLARITY_HIGH;
    oc.OCFastMode = TIM_OCFAST_DISABLE;
    if (HAL_TIM_PWM_ConfigChannel(&htim3, &oc, TIM_CHANNEL_1) != HAL_OK) Error_Handler();
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
    Fan_Write(0);
}
#endif

/* ADC1: PA3 전류(IN4) / PB0 전압(IN15) / PA2 전원단 온도(IN3) / PC3 드라이버 온도(IN9) — 읽을 때마다 채널 전환 */
static void MX_ADC1_Init(void)
{
    GPIO_InitTypeDef g = {0};
    RCC_PeriphCLKInitTypeDef pc = {0};

    pc.PeriphClockSelection = RCC_PERIPHCLK_ADC12;
    pc.Adc12ClockSelection = RCC_ADC12CLKSOURCE_SYSCLK;
    HAL_RCCEx_PeriphCLKConfig(&pc);
    __HAL_RCC_ADC12_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    g.Mode = GPIO_MODE_ANALOG;
    g.Pull = GPIO_NOPULL;
    g.Pin = GPIO_PIN_2 | GPIO_PIN_3; HAL_GPIO_Init(GPIOA, &g);   /* TEMP_ADC_PWR, I_SENSE_ADC */
    g.Pin = GPIO_PIN_0;              HAL_GPIO_Init(GPIOB, &g);   /* VOLT_ADC */
    g.Pin = GPIO_PIN_3;              HAL_GPIO_Init(GPIOC, &g);   /* TEMP_ADC_DRV */

    hadc1.Instance = ADC1;
    hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
    hadc1.Init.Resolution = ADC_RESOLUTION_12B;
    hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc1.Init.GainCompensation = 0;
    hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
    hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
    hadc1.Init.LowPowerAutoWait = DISABLE;
    hadc1.Init.ContinuousConvMode = DISABLE;
    hadc1.Init.NbrOfConversion = 1;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
    hadc1.Init.DMAContinuousRequests = DISABLE;
    hadc1.Init.Overrun = ADC_OVR_DATA_OVERWRITTEN;
    hadc1.Init.OversamplingMode = DISABLE;
    if (HAL_ADC_Init(&hadc1) != HAL_OK) Error_Handler();
    HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED);
}

/* ADC2: PC5 = MQ-7 CO (IN11) — 채널 하나만 씀 */
static void MX_ADC2_Init(void)
{
    GPIO_InitTypeDef g = {0};
    ADC_ChannelConfTypeDef c = {0};

    __HAL_RCC_ADC12_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    g.Mode = GPIO_MODE_ANALOG; g.Pull = GPIO_NOPULL;
    g.Pin = GPIO_PIN_5; HAL_GPIO_Init(GPIOC, &g);     /* CO_ADC */

    hadc2.Instance = ADC2;
    hadc2.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
    hadc2.Init.Resolution = ADC_RESOLUTION_12B;
    hadc2.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc2.Init.GainCompensation = 0;
    hadc2.Init.ScanConvMode = ADC_SCAN_DISABLE;
    hadc2.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
    hadc2.Init.LowPowerAutoWait = DISABLE;
    hadc2.Init.ContinuousConvMode = DISABLE;
    hadc2.Init.NbrOfConversion = 1;
    hadc2.Init.DiscontinuousConvMode = DISABLE;
    hadc2.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc2.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
    hadc2.Init.DMAContinuousRequests = DISABLE;
    hadc2.Init.Overrun = ADC_OVR_DATA_OVERWRITTEN;
    hadc2.Init.OversamplingMode = DISABLE;
    if (HAL_ADC_Init(&hadc2) != HAL_OK) Error_Handler();

    c.Channel = CO_CHANNEL; c.Rank = ADC_REGULAR_RANK_1;
    c.SamplingTime = ADC_SAMPLETIME_92CYCLES_5;
    c.SingleDiff = ADC_SINGLE_ENDED; c.OffsetNumber = ADC_OFFSET_NONE; c.Offset = 0;
    if (HAL_ADC_ConfigChannel(&hadc2, &c) != HAL_OK) Error_Handler();
    HAL_ADCEx_Calibration_Start(&hadc2, ADC_SINGLE_ENDED);
}

/* I2C4: PC6 SCL / PC7 SDA (AF8), 100kHz — ENS160. 실패해도 멈추지 않음 (센서 없이도 창문은 동작) */
static void MX_I2C4_Init(void)
{
    GPIO_InitTypeDef g = {0};
    RCC_PeriphCLKInitTypeDef pc = {0};

    pc.PeriphClockSelection = RCC_PERIPHCLK_I2C4;
    pc.I2c4ClockSelection = RCC_I2C4CLKSOURCE_PCLK1;
    HAL_RCCEx_PeriphCLKConfig(&pc);
    __HAL_RCC_I2C4_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    g.Pin = GPIO_PIN_6 | GPIO_PIN_7;
    g.Mode = GPIO_MODE_AF_OD;
    g.Pull = GPIO_PULLUP;                 /* 모듈 풀업이 없을 때 대비 (약함, 모듈에 4.7k 권장) */
    g.Speed = GPIO_SPEED_FREQ_LOW;
    g.Alternate = GPIO_AF8_I2C4;
    HAL_GPIO_Init(GPIOC, &g);

    hi2c4.Instance = I2C4;
    hi2c4.Init.Timing = ENS_I2C_TIMING;
    hi2c4.Init.OwnAddress1 = 0;
    hi2c4.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c4.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c4.Init.OwnAddress2 = 0;
    hi2c4.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
    hi2c4.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c4.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    if (HAL_I2C_Init(&hi2c4) != HAL_OK) return;
    HAL_I2CEx_ConfigAnalogFilter(&hi2c4, I2C_ANALOGFILTER_ENABLE);
    HAL_I2CEx_ConfigDigitalFilter(&hi2c4, 0);
}

/* FDCAN 포트 하나 초기화. port 1 = FDCAN1 (PA11 RX / PA12 TX), port 2 = FDCAN2 (PB5 RX / PB6 TX)
 * 게이트웨이면 양쪽 모두 0x100~0x2FF 수신, 게이트웨이가 아니면 위쪽은 0x100(명령) + 0x120(마스터 온습도) 만 */
static void CAN_InitPort(FDCAN_HandleTypeDef *hc, uint8_t port, uint8_t up)
{
    GPIO_InitTypeDef g = {0};
    FDCAN_FilterTypeDef f = {0};

    if (port == 1)
    {
        __HAL_RCC_GPIOA_CLK_ENABLE();
        g.Pin = GPIO_PIN_11 | GPIO_PIN_12;
        g.Mode = GPIO_MODE_AF_PP; g.Pull = GPIO_NOPULL; g.Speed = GPIO_SPEED_FREQ_HIGH;
        g.Alternate = GPIO_AF9_FDCAN1;
        HAL_GPIO_Init(GPIOA, &g);
        hc->Instance = FDCAN1;
    }
    else
    {
        __HAL_RCC_GPIOB_CLK_ENABLE();
        g.Pin = GPIO_PIN_5 | GPIO_PIN_6;
        g.Mode = GPIO_MODE_AF_PP; g.Pull = GPIO_NOPULL; g.Speed = GPIO_SPEED_FREQ_HIGH;
        g.Alternate = GPIO_AF9_FDCAN2;
        HAL_GPIO_Init(GPIOB, &g);
        hc->Instance = FDCAN2;
    }

    hc->Init.ClockDivider = FDCAN_CLOCK_DIV1;
    hc->Init.FrameFormat = FDCAN_FRAME_CLASSIC;
    hc->Init.Mode = FDCAN_MODE_NORMAL;
    hc->Init.AutoRetransmission = ENABLE;
    hc->Init.TransmitPause = DISABLE;
    hc->Init.ProtocolException = DISABLE;
    hc->Init.NominalPrescaler = 10;                      /* 80MHz/10 = 8MHz, 16tq -> 500kbps */
    hc->Init.NominalSyncJumpWidth = 2;
    hc->Init.NominalTimeSeg1 = 13;
    hc->Init.NominalTimeSeg2 = 2;
    hc->Init.DataPrescaler = 10; hc->Init.DataSyncJumpWidth = 2;
    hc->Init.DataTimeSeg1 = 13;  hc->Init.DataTimeSeg2 = 2;
    hc->Init.StdFiltersNbr = 1;
    hc->Init.ExtFiltersNbr = 0;
    hc->Init.TxFifoQueueMode = FDCAN_TX_FIFO_OPERATION;
    if (HAL_FDCAN_Init(hc) != HAL_OK) Error_Handler();

    f.IdType = FDCAN_STANDARD_ID; f.FilterIndex = 0; f.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
    if (up && !CAN_GATEWAY) { f.FilterType = FDCAN_FILTER_DUAL;  f.FilterID1 = CAN_ID_CMD; f.FilterID2 = CAN_ID_ENV; }   /* 명령 + 마스터 온습도 */
    else                    { f.FilterType = FDCAN_FILTER_RANGE; f.FilterID1 = 0x100;      f.FilterID2 = 0x2FF; }
    HAL_FDCAN_ConfigFilter(hc, &f);
    HAL_FDCAN_ConfigGlobalFilter(hc, FDCAN_REJECT, FDCAN_REJECT, FDCAN_REJECT_REMOTE, FDCAN_REJECT_REMOTE);
    if (HAL_FDCAN_Start(hc) != HAL_OK) Error_Handler();
}

static void CAN_InitAll(void)
{
    RCC_PeriphCLKInitTypeDef pc = {0};
    pc.PeriphClockSelection = RCC_PERIPHCLK_FDCAN;
    pc.FdcanClockSelection = RCC_FDCANCLKSOURCE_PCLK1;   /* 80MHz (FDCAN1/2 공용) */
    HAL_RCCEx_PeriphCLKConfig(&pc);
    __HAL_RCC_FDCAN_CLK_ENABLE();

#if CAN_UP_PORT == 1
    can_up = &hfdcan1; CAN_InitPort(&hfdcan1, 1, 1);
  #if CAN_GATEWAY
    can_dn = &hfdcan2; CAN_InitPort(&hfdcan2, 2, 0);
  #endif
#else
    can_up = &hfdcan2; CAN_InitPort(&hfdcan2, 2, 1);
  #if CAN_GATEWAY
    can_dn = &hfdcan1; CAN_InitPort(&hfdcan1, 1, 0);
  #endif
#endif
}

static void MX_TIM1_Init(void)
{
    GPIO_InitTypeDef g = {0};
    TIM_OC_InitTypeDef oc = {0};

    __HAL_RCC_TIM1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    g.Pin = GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10;
    g.Mode = GPIO_MODE_AF_PP;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    g.Alternate = GPIO_AF6_TIM1;
    HAL_GPIO_Init(GPIOA, &g);

    htim1.Instance = TIM1;
    htim1.Init.Prescaler = 0;
    htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim1.Init.Period = PWM_ARR;
    htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim1.Init.RepetitionCounter = 0;
    htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    if (HAL_TIM_PWM_Init(&htim1) != HAL_OK) Error_Handler();

    oc.OCMode = TIM_OCMODE_PWM1;
    oc.Pulse = PWM_HALF;
    oc.OCPolarity = TIM_OCPOLARITY_HIGH;
    oc.OCNPolarity = TIM_OCNPOLARITY_HIGH;
    oc.OCFastMode = TIM_OCFAST_DISABLE;
    oc.OCIdleState = TIM_OCIDLESTATE_RESET;
    oc.OCNIdleState = TIM_OCNIDLESTATE_RESET;
    if (HAL_TIM_PWM_ConfigChannel(&htim1, &oc, TIM_CHANNEL_1) != HAL_OK) Error_Handler();
    if (HAL_TIM_PWM_ConfigChannel(&htim1, &oc, TIM_CHANNEL_2) != HAL_OK) Error_Handler();
    if (HAL_TIM_PWM_ConfigChannel(&htim1, &oc, TIM_CHANNEL_3) != HAL_OK) Error_Handler();

    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2);
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_3);
}

void Error_Handler(void)
{
    __disable_irq();
    HAL_GPIO_WritePin(DRV_EN_PORT, DRV_EN_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED_RED_PORT, LED_RED_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(BUZ_PORT, BUZ_PIN, GPIO_PIN_RESET);
    while (1) { }
}
