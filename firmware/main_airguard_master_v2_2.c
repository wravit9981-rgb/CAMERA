/*
 * main_airguard_master_v2_2.c — 1.3 보드 마스터 (구역별 공기질 판정 버전, v2.2)
 *
 *   [바뀐 점]
 *   - CO 센서는 마스터에서 빠지고 각 창문 노드로 이동 (노드마다 MQ-7 + ENS160)
 *   - 마스터 센서: 미세먼지(PA4 PWM 캡처) + DHT22(PA15) 만
 *   - 구역별 위험 등급: 구역 k = 그 노드의 CO / TVOC  (둘 다 주의 이상이면 한 단계 올림)
 *     공용 등급        = 미세먼지 / CO2(두 구역 eCO2 평균)
 *     종합 등급        = 셋 중 가장 높은 값  -> 부저, 환풍기, 패널 상단, 대시보드
 *     창문 k 목표      = max(구역 k, 공용) 등급의 개방률  (ZONE_WINDOW 0 이면 종합 등급 하나로)
 *   - 이 보드 전압(PB0) / 전류(PA3 INA240) / 온도(PC3 드라이버, PA2 전원단) 측정 -> 0x110
 *   - 먼지 / 온습도 -> 0x120 (패널 표시 + 노드 ENS160 온습도 보정)
 *   - ESP32 텔레메트리: 대시보드가 읽는 키(z1co, z1tvoc, z1lv, co2 평균 ...) 로 확장
 *   - [v2.1] 등급 내릴 때 5초 확인 (창문이 5초 동안 열린 상태 유지 후 닫힘)
 *   - [v2.1] 부저: 종합 등급이 3단계(위험)에 처음 도달할 때만 "삑(0.5) 쉼 삑(0.5) 쉼 삐-(1.5)", 3단계 전에는 절대 안 울림
 *            (구역 경고 부저는 각 창문 노드의 PB11 이 담당)
 *   - [v2.2] 미세먼지 TIM3 채널 뒤바뀜 수정 (CH2 = 주기, CH1 = High)
 *            + 새 펄스가 잡혔을 때만 읽고, 펄스가 끊기면 핀 상태로 0%/100% (값이 안 내려가던 문제)
 *            + 디버깅 키 dust_duty (x10 %), dust_per (주기 us), dust_ne (1 = 펄스 없음)
 *   - [v2.2] 남은 시간 표시: 창문 노드 v5.8 의 0x131/0x132 (닫기 대기 / 끼임 재시도) 수신
 *            + 마스터 등급 하향 확인 남은 시간 -> 텔레메트리 키 (창문 n = 1, 2)
 *              w{n}dn  등급 하향 확인 남은 ms (0 = 확인 중 아님)    w{n}tg  노드가 실제로 가는 목표 %
 *              w{n}cw  노드 닫기 대기 남은 ms                       w{n}wp  대기 중인 닫는 목표 % (255 = 없음)
 *              w{n}pf  끼임 플래그 (bit0 닫기대기 bit1 끼임 bit2 재시도끝 bit3 되돌아가는 중)
 *              w{n}pr  끼임 재시도까지 남은 ms   w{n}pn / w{n}pm  재시도 한 횟수 / 최대   w{n}ps  원인 1 전류 2 전압 3 걸림
 *   그 밖의 기능(스텝모터, 버튼, LED, UART 2채널)은 그대로
 *
 * 핀 (마스터 CubeMX 핀맵)
 *   DUST_PWM PA4 (TIM3_CH2 PWM 입력 캡처)     DHT22 PA15     Buzzer PA0 (TIM2_CH1)
 *   VOLT_ADC PB0 (ADC1_IN15)   I_SENSE_ADC PA3 (ADC1_IN4)   TEMP_ADC_DRV PC3 (ADC1_IN9)   TEMP_ADC_PWR PA2 (ADC1_IN3)
 *   FDCAN1   PA11 RX / PA12 TX      USART1 PC4 TX / PC5 RX,  USART2 PB3 TX / PB4 RX  (둘 다 같은 JSON)
 *   모터     PA8/PA9/PA10 (TIM1), EN PB14, nSLEEP PB9, nFault PB15, SW PB13
 *   LED      NORMAL PC10, RUN PC11, Red PB7      (PB11 PWM, PB12 ADC 는 미사용)
 *
 * CAN 메시지 (500kbps, 표준 ID, 8바이트, 2바이트 값은 큰 바이트 먼저)
 *   송신 0x100 명령    : [0]종합등급 [1]창문목표%(종합) [2]환풍기% [3]seq [4]플래그 bit0 먼지예열끝 bit1 구역별목표
 *                        [5]창문1 목표% [6]창문2 목표% [7]등급 묶음 (bit0-1 구역1, bit2-3 구역2, bit4-5 공용)
 *   송신 0x110 상태    : [0..1]전압mV [2..3]전류mA [4]DRV온도 [5]PWR온도 (int8 도, 0x80 = 없음) [6]플래그 [7]seq
 *   송신 0x120 환경    : [0..1]먼지 µg/m³ [2..3]온도x10 (int16) [4..5]습도x10 [6]bit0 DHT22 정상 bit1 먼지예열끝 [7]seq
 *   수신 0x101/0x102  : [0]상태 [1]현재% [2]목표% [3]고장코드 [4]부하율% [5..6]전류mA [7]seq
 *   수신 0x111/0x112  : [0]AGC [1]플래그 [2..3]절대각 [4..5]전압mV [6]DRV온도 [7]PWR온도
 *   수신 0x121/0x122  : [0..1]CO ppm [2..3]TVOC ppb [4..5]eCO2 ppm [6]AQI [7]플래그
 *                        (bit0 ENS160 응답, bit1-2 유효도, bit3 CO 예열끝, bit4 CO 센서 정상)
 *   수신 0x131/0x132  : [0]플래그 [1]닫기대기 남은 0.1초 [2]재시도 남은 0.1초 [3]재시도 횟수 [4]최대
 *                        [5]끼임 원인 [6]대기 중인 닫는 목표% (0xFF 없음) [7]seq   (창문 노드 v5.8)
 *   수신 0x201 환풍기  : [0]현재% [1]목표% [2]수동 [3]명령끊김 [7]seq
 */

#include "main.h"
#include <math.h>
#include <stdio.h>
#include <stdarg.h>

/* ---- CubeMX가 만든 다른 파일과의 호환성을 위한 핸들 선언 ---- */
ADC_HandleTypeDef   hadc1, hadc2, hadc4;
FDCAN_HandleTypeDef hfdcan1, hfdcan2;
I2C_HandleTypeDef   hi2c3, hi2c4;
IWDG_HandleTypeDef  hiwdg;
SPI_HandleTypeDef   hspi1;
TIM_HandleTypeDef   htim1, htim2, htim3, htim6, htim8;
UART_HandleTypeDef  huart1, huart2;
WWDG_HandleTypeDef  hwwdg;

/* =====================================================================
 *  설정값
 * ===================================================================== */
#define AUTO_VENT           0        /* 1: 위험등급에 따라 마스터 자체 모터로 창문 개폐 (창문은 CAN 노드가 담당) */
#define WARMUP_MS           60000U   /* 먼지 기준선 잡는 시간 (시험할 땐 10000U) */

#define DUST_DIVIDER        1.0f     /* PA4 앞 분압비 */
#define DUST_UG_PER_V       200.0f   /* 1V 상승 = 200 µg/m³ */
#define DUST_NOEDGE_MS      200U     /* 이 시간 동안 펄스가 없으면 핀 상태로 0% / 100% 판단 (TIM3 16비트 = 최대 65ms 주기) */

/* 등급 기준: 주의 / 경고 / 위험  (대시보드 TH 와 같은 값) */
static const float TH_CO[3]   = { 10.0f, 30.0f, 50.0f };       /* ppm   구역 노드 MQ-7   */
static const float TH_TVOC[3] = { 220.0f, 660.0f, 2200.0f };   /* ppb   구역 노드 ENS160 */
static const float TH_DUST[3] = { 35.0f, 75.0f, 115.0f };      /* µg/m³ 마스터          */
static const float TH_CO2[3]  = { 1000.0f, 1500.0f, 2000.0f }; /* ppm   두 구역 eCO2 평균 */
#define LEVEL_DOWN_HOLD_MS  5000U    /* 등급 내릴 때 확인 시간 = 창문 히스테리시스 (5초 동안 계속 낮아야 내려감) */

/* CAN 제어 */
static const uint8_t WIN_PCT_BY_LEVEL[4] = { 0, 50, 75, 100 };   /* 등급별 창문 개방률 (노드는 0/50/75/100 네 단계) */
#define ZONE_WINDOW         1        /* 1: 창문마다 자기 구역 등급으로 따로 개방,  0: 종합 등급 하나로 두 창문 같이 */
#define FAN_START_IDX       15.0f    /* 위험지수(0~100) 이 값부터 환풍기 가동 권장 */
#define FAN_MIN_PCT         35       /* 가동 시 최소 PWM % */
#define CAN_ID_CMD          0x100U
#define CAN_ID_WIN1         0x101U
#define CAN_ID_WIN2         0x102U
#define CAN_ID_MST_ST       0x110U
#define CAN_ID_WIN1_ST      0x111U
#define CAN_ID_WIN2_ST      0x112U
#define CAN_ID_ENV          0x120U
#define CAN_ID_AIR1         0x121U
#define CAN_ID_AIR2         0x122U
#define CAN_ID_WIN1_EV      0x131U     /* 창문 이벤트: 닫기 대기 / 끼임 재시도 남은 시간 */
#define CAN_ID_WIN2_EV      0x132U
#define EV_TIMEOUT_MS       1000U
#define CAN_ID_FAN          0x201U
#define NODE_TIMEOUT_MS     500U     /* 하트비트가 이 시간 넘게 없으면 응답 없음 */
#define AIR_TIMEOUT_MS      2000U    /* 공기질이 이 시간 넘게 없으면 그 구역 값은 판정에서 뺌 */

/* 이 보드 전원 측정 (노드와 같은 회로) */
#define PWR_CH_ISENSE       ADC_CHANNEL_4    /* PA3 I_SENSE_ADC (INA240) */
#define PWR_CH_VBUS         ADC_CHANNEL_15   /* PB0 VOLT_ADC */
#define PWR_CH_TPWR         ADC_CHANNEL_3    /* PA2 TEMP_ADC_PWR */
#define PWR_CH_TDRV         ADC_CHANNEL_9    /* PC3 TEMP_ADC_DRV */
#define INA_GAIN            50.0f            /* INA240A2 */
#define SHUNT_MOHM          10.0f
#define VBUS_DIVIDER        11.0f
#define TEMP_SENSOR         0                /* 0 = NTC,  1 = 선형 IC (노드/패널 설정과 같게) */
#define NTC_R25_OHM         10000.0f
#define NTC_BETA            3950.0f
#define NTC_FIXED_OHM       10000.0f
#define NTC_ON_GND_SIDE     1                /* 1: 3.3V-[고정R]-ADC-[NTC]-GND */
#define LIN_MV_AT_0C        500.0f
#define LIN_MV_PER_C        10.0f

/* 부저 */
#define BUZZ_LEVEL          3        /* 종합 등급이 이 값에 "도달하는 순간"만 울림 (3 = 위험, 그 전엔 절대 안 울림) */
#define BUZZ_COUNT          3        /* 울리는 횟수 */
static const uint16_t BUZZ_ON_MS[BUZZ_COUNT] = { 200U, 200U, 800U };   /* 삑 0.5초, 삑 0.5초, 삐- 1.5초 */
#define BUZZ_OFF_MS         100U     /* 사이 쉬는 시간 (0.5초 간격) */
#define BUZZ_IS_ACTIVE      0        /* 0: PWM 소리(수동형/능동형 모두 가능)  1: 능동형 부저를 단순 ON/OFF */
#define BUZZ_FREQ_HZ        2700U    /* PWM 소리 주파수 */

/* =====================================================================
 *  1.3 보드 핀
 * ===================================================================== */
#define SW1_PORT         GPIOB
#define SW1_PIN          GPIO_PIN_13      /* SW */
#define LED_NORMAL_PORT  GPIOC
#define LED_NORMAL_PIN   GPIO_PIN_10      /* NORMAL_LED */
#define LED_RUN_PORT     GPIOC
#define LED_RUN_PIN      GPIO_PIN_11      /* RUN_LED */
#define LED_RED_PORT     GPIOB
#define LED_RED_PIN      GPIO_PIN_7       /* Red_LED */
#define DRV_EN_PORT      GPIOB
#define DRV_EN_PIN       GPIO_PIN_14      /* EN_OUTPUT -> DRV8313 EN1~3 */
#define DRV_SLP_PORT     GPIOB
#define DRV_SLP_PIN      GPIO_PIN_9       /* nSlp_OUTPUT -> DRV8313 nSLEEP */
#define DRV_NFAULT_PORT  GPIOB
#define DRV_NFAULT_PIN   GPIO_PIN_15      /* nFalt */

#define DUST_PWM_PORT    GPIOA
#define DUST_PWM_PIN     GPIO_PIN_4       /* DUST_PWM = TIM3_CH2 */
#define DHT_PORT         GPIOA
#define DHT_PIN          GPIO_PIN_15      /* DHT22_OUTPUT */
#define BUZZ_PORT        GPIOA
#define BUZZ_PIN         GPIO_PIN_0       /* Buzzer_PWM = TIM2_CH1 */

/* =====================================================================
 *  스텝모터 제어 (기존 그대로)
 * ===================================================================== */
#define TIM_CLK_HZ       80000000U
#define PWM_FREQ_HZ      20000U
#define PWM_ARR          ((TIM_CLK_HZ / PWM_FREQ_HZ) - 1U)
#define PWM_HALF         ((PWM_ARR + 1U) / 2U)

#define FULL_STEPS_PER_REV   200U
#define ELEC_CYCLES_PER_REV  (FULL_STEPS_PER_REV / 4U)
#define AMP_MAX          0.30f
#define AMP_RUN          0.20f
#define AMP_HOLD         0.10f
#define AMP_RAMP_PER_MS  0.0005f
#define RPM_TARGET       30.0f
#define ACCEL_RPM_PER_S  60.0f
#define USE_NFAULT_CHECK 0
#define RUN_TIMEOUT_MS   5000U
#define DIR_CW           (+1)
#define DIR_CCW          (-DIR_CW)

typedef enum { ST_IDLE = 0, ST_RUN, ST_STOPPING, ST_HOLD, ST_FAULT } StepState;

static volatile StepState state = ST_IDLE;
static int16_t  sin_tbl[1024];
static uint32_t phase = 0;
static int32_t  phase_rate_q16 = 0;
static uint32_t last_cyc = 0;
static volatile float  cur_rpm = 0.0f;
static volatile float  amp = 0.0f;
static volatile int8_t dir = DIR_CW;
static uint32_t run_start_ms = 0;
static volatile uint8_t win_open = 0;

void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_TIM1_Init(void);
static void MX_TIM3_Init(void);

/* ---------------- 마이크로초 지연 (DWT) ---------------- */
static inline uint32_t us_now(void) { return DWT->CYCCNT / (SystemCoreClock / 1000000U); }
static void delay_us(uint32_t us)
{
    uint32_t start = DWT->CYCCNT, ticks = us * (SystemCoreClock / 1000000U);
    while ((DWT->CYCCNT - start) < ticks) { }
}

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
    phase += (uint32_t)(((int64_t)phase_rate_q16 * (int64_t)dt) >> 16);

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
    if (on)
    {
        HAL_GPIO_WritePin(DRV_SLP_PORT, DRV_SLP_PIN, GPIO_PIN_SET);
        HAL_Delay(2);
        HAL_GPIO_WritePin(DRV_EN_PORT, DRV_EN_PIN, GPIO_PIN_SET);
    }
    else
    {
        HAL_GPIO_WritePin(DRV_EN_PORT, DRV_EN_PIN, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(DRV_SLP_PORT, DRV_SLP_PIN, GPIO_PIN_RESET);
    }
}

static void Enter_Fault(void)
{
    Driver_Enable(0);
    amp = 0.0f; cur_rpm = 0.0f; phase_rate_q16 = 0;
    state = ST_FAULT;
}

static void Motor_Start(int8_t d)
{
    if (state == ST_RUN || state == ST_STOPPING || state == ST_FAULT) return;
    dir = d;
    if (state == ST_IDLE)
    {
        amp = 0.0f;
        last_cyc = DWT->CYCCNT;
        Stepper_PwmUpdate();
        Driver_Enable(1);
    }
    run_start_ms = HAL_GetTick();
    state = ST_RUN;
}

static void Stepper_1ms(void)
{
    float target_rpm = 0.0f, target_amp = 0.0f;
    switch (state)
    {
    case ST_RUN:
        target_amp = AMP_RUN;
        target_rpm = (amp >= AMP_RUN * 0.95f) ? RPM_TARGET : 0.0f;
        break;
    case ST_STOPPING: target_rpm = 0.0f; target_amp = AMP_RUN;  break;
    case ST_HOLD:     target_rpm = 0.0f; target_amp = AMP_HOLD; break;
    default:          target_rpm = 0.0f; target_amp = 0.0f;     break;
    }

    float step = ACCEL_RPM_PER_S / 1000.0f;
    if (cur_rpm < target_rpm)      { cur_rpm += step; if (cur_rpm > target_rpm) cur_rpm = target_rpm; }
    else if (cur_rpm > target_rpm) { cur_rpm -= step; if (cur_rpm < target_rpm) cur_rpm = target_rpm; }
    Stepper_SetRpm(cur_rpm);

    if (amp < target_amp)      { amp += AMP_RAMP_PER_MS; if (amp > target_amp) amp = target_amp; }
    else if (amp > target_amp) { amp -= AMP_RAMP_PER_MS; if (amp < target_amp) amp = target_amp; }

    if (RUN_TIMEOUT_MS > 0U && state == ST_RUN && (HAL_GetTick() - run_start_ms) >= RUN_TIMEOUT_MS)
        state = ST_STOPPING;

    if (state == ST_STOPPING && cur_rpm == 0.0f)
    {
        state = ST_HOLD;
        dir = (int8_t)-dir;
    }
}

static void Button_1ms(void)
{
    static GPIO_PinState prev = GPIO_PIN_SET;
    static uint32_t lock_until = 0;
    GPIO_PinState now = HAL_GPIO_ReadPin(SW1_PORT, SW1_PIN);
    uint32_t t = HAL_GetTick();

    if (prev == GPIO_PIN_SET && now == GPIO_PIN_RESET && t >= lock_until)
    {
        lock_until = t + 200;
        switch (state)
        {
        case ST_IDLE: Motor_Start(DIR_CW); break;
        case ST_HOLD: Motor_Start(dir);    break;
        case ST_RUN:  state = ST_STOPPING; break;
        default: break;
        }
    }
    prev = now;
}

/* =====================================================================
 *  센서 (먼지: PWM 캡처, DHT22)  — CO 는 이제 각 구역 노드가 잼
 * ===================================================================== */
volatile int32_t dust_mv = 0, dust_base_mv = 0;
volatile int32_t dust_ug = 0;
volatile int16_t temp10 = 0;
volatile uint16_t hum10 = 0;
volatile uint8_t dht_ok = 0, dht_err = 0;
volatile uint8_t risk_level = 0;

static float dust_f = 0.0f;
static float dust_base = 0.0f;
static uint8_t warm_done = 0;

/* PWM 측정을 위한 변수 */
volatile uint32_t pwm_period = 0;
volatile uint32_t pwm_high_dur = 0;
volatile float    dust_duty = 0.0f;

static void Adc_InitOne(ADC_HandleTypeDef *h, ADC_TypeDef *inst, uint32_t ch, uint32_t smp)
{
    ADC_ChannelConfTypeDef c = {0};

    h->Instance                   = inst;
    h->Init.ClockPrescaler        = ADC_CLOCK_SYNC_PCLK_DIV4;
    h->Init.Resolution            = ADC_RESOLUTION_12B;
    h->Init.DataAlign             = ADC_DATAALIGN_RIGHT;
    h->Init.GainCompensation      = 0;
    h->Init.ScanConvMode          = ADC_SCAN_DISABLE;
    h->Init.EOCSelection          = ADC_EOC_SINGLE_CONV;
    h->Init.LowPowerAutoWait      = DISABLE;
    h->Init.ContinuousConvMode    = DISABLE;
    h->Init.NbrOfConversion       = 1;
    h->Init.DiscontinuousConvMode = DISABLE;
    h->Init.ExternalTrigConv      = ADC_SOFTWARE_START;
    h->Init.ExternalTrigConvEdge  = ADC_EXTERNALTRIGCONVEDGE_NONE;
    h->Init.DMAContinuousRequests = DISABLE;
    h->Init.Overrun               = ADC_OVR_DATA_OVERWRITTEN;
    h->Init.OversamplingMode      = DISABLE;
    if (HAL_ADC_Init(h) != HAL_OK) Error_Handler();

    c.Channel      = ch;
    c.Rank         = ADC_REGULAR_RANK_1;
    c.SamplingTime = smp;
    c.SingleDiff   = ADC_SINGLE_ENDED;
    c.OffsetNumber = ADC_OFFSET_NONE;
    c.Offset       = 0;
    if (HAL_ADC_ConfigChannel(h, &c) != HAL_OK) Error_Handler();

    HAL_ADCEx_Calibration_Start(h, ADC_SINGLE_ENDED);
}

static void Sensors_Init(void)
{
    GPIO_InitTypeDef g = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();

    /* DHT22: 오픈드레인 + 풀업 */
    HAL_GPIO_WritePin(DHT_PORT, DHT_PIN, GPIO_PIN_SET);
    g.Mode = GPIO_MODE_OUTPUT_OD;
    g.Pull = GPIO_PULLUP;
    g.Pin  = DHT_PIN;
    HAL_GPIO_Init(DHT_PORT, &g);

    MX_TIM3_Init(); /* PA4 PWM Capture 타이머 초기화 */
}

static uint16_t Adc_Read(ADC_HandleTypeDef *h)
{
    HAL_ADC_Start(h);
    if (HAL_ADC_PollForConversion(h, 2) != HAL_OK) return 0;
    return (uint16_t)HAL_ADC_GetValue(h);
}

static float raw_to_mv(uint16_t raw, float divider)
{
    return (float)raw * 3300.0f / 4095.0f * divider;
}

/* ---- 미세먼지 PWM 주기 및 Duty 측정 ---- */
volatile uint32_t dust_period_us = 0, dust_high_us = 0;   /* 디버깅: 마지막으로 잰 주기 / High 시간 */
volatile uint8_t  dust_noedge = 0;                        /* 1 = 펄스가 안 들어옴 (핀이 계속 High 또는 Low) */

static void Dust_Sample_PWM(void)
{
    static uint32_t last_edge_ms = 0;
    uint32_t now = HAL_GetTick();
    float duty;

    /* [v2.2] 새로 잡힌 주기가 있을 때만 읽음.
     * 예전엔 캡처 레지스터를 그냥 읽어서, 펄스가 멈추면(공기가 깨끗해 출력이 계속 Low 등) 마지막 값이 그대로 남아
     * 수치가 안 내려갔음 */
    if (__HAL_TIM_GET_FLAG(&htim3, TIM_FLAG_CC2))
    {
        /* PA4 = TIM3_CH2 입력 -> CH2: Period(주기, 리셋 기준), CH1: Pulse Width(High 구간)  [v2.2 채널 뒤바뀜 수정] */
        uint32_t period = HAL_TIM_ReadCapturedValue(&htim3, TIM_CHANNEL_2);   /* 읽으면 CC2 플래그도 지워짐 */
        uint32_t high   = HAL_TIM_ReadCapturedValue(&htim3, TIM_CHANNEL_1);
        __HAL_TIM_CLEAR_FLAG(&htim3, TIM_FLAG_CC2 | TIM_FLAG_CC1 | TIM_FLAG_CC2OF | TIM_FLAG_CC1OF);
        if (period == 0 || high > period) return;                            /* 잘못 잡힌 값 */
        dust_period_us = period; dust_high_us = high;
        last_edge_ms = now; dust_noedge = 0;
        duty = (float)high / (float)period * 100.0f;
    }
    else if ((now - last_edge_ms) >= DUST_NOEDGE_MS)
    {
        /* 펄스가 끊김 -> 핀 상태 그대로 0% 또는 100% */
        dust_noedge = 1;
        duty = (HAL_GPIO_ReadPin(DUST_PWM_PORT, DUST_PWM_PIN) == GPIO_PIN_SET) ? 100.0f : 0.0f;
    }
    else return;                                                             /* 다음 펄스 기다림 */

    dust_duty = duty;
    /* Duty 비율을 mV 단위 등가 값으로 환산 (3.3V 기준) */
    float mv = (duty / 100.0f) * 3300.0f * DUST_DIVIDER;
    dust_f = (dust_f <= 0.0f) ? mv : dust_f + (mv - dust_f) * 0.05f;
}

/* ---- 먼지 기준선 갱신 및 환산 (1초 마다) ---- */
static void Sensors_Convert_1s(void)
{
    dust_mv = (int32_t)dust_f;

    if (!warm_done)
    {
        if (HAL_GetTick() >= WARMUP_MS)
        {
            warm_done = 1;
            dust_base = dust_f;
        }
        dust_ug = 0;
    }
    else
    {
        if (dust_f < dust_base) dust_base = dust_f; else dust_base += 0.02f;
        float ug = (dust_f - dust_base) / 1000.0f * DUST_UG_PER_V;
        dust_ug = (int32_t)(ug > 0.0f ? ug + 0.5f : 0.0f);
        if (dust_ug > 999) dust_ug = 999;
    }
    dust_base_mv = (int32_t)dust_base;
}

/* ---- DHT22 읽기 ---- */
static int dht_wait(GPIO_PinState level, uint32_t timeout_us)
{
    uint32_t t0 = us_now();
    while (HAL_GPIO_ReadPin(DHT_PORT, DHT_PIN) != level)
        if ((us_now() - t0) > timeout_us) return -1;
    return (int)(us_now() - t0);
}

static uint8_t DHT22_Read(void)
{
    uint8_t d[5] = {0};

    HAL_GPIO_WritePin(DHT_PORT, DHT_PIN, GPIO_PIN_RESET);
    delay_us(1200);
    HAL_GPIO_WritePin(DHT_PORT, DHT_PIN, GPIO_PIN_SET);

    if (dht_wait(GPIO_PIN_RESET, 200) < 0) return 1;
    if (dht_wait(GPIO_PIN_SET,   200) < 0) return 2;
    if (dht_wait(GPIO_PIN_RESET, 200) < 0) return 3;

    for (int i = 0; i < 40; i++)
    {
        if (dht_wait(GPIO_PIN_SET, 100) < 0) return 4;
        int hi = dht_wait(GPIO_PIN_RESET, 120);
        if (hi < 0) return 5;
        d[i >> 3] <<= 1;
        if (hi > 45) d[i >> 3] |= 1;
    }
    if ((uint8_t)(d[0] + d[1] + d[2] + d[3]) != d[4]) return 6;

    hum10  = (uint16_t)((d[0] << 8) | d[1]);
    int16_t t = (int16_t)(((d[2] & 0x7F) << 8) | d[3]);
    temp10 = (d[2] & 0x80) ? (int16_t)-t : t;
    return 0;
}

/* =====================================================================
 *  이 보드 전원 상태: 전압(PB0) / 전류(PA3, INA240) / 온도(PC3 드라이버부, PA2 전원단)  -> CAN 0x110
 * ===================================================================== */
#define TEMP_NONE   (-999.0f)
volatile uint32_t g_vbus_mv = 0;
volatile float    g_i_ma = 0.0f;
volatile float    g_tdrv_c = TEMP_NONE, g_tpwr_c = TEMP_NONE;   /* 환산 온도 */
volatile float    g_tdrv_mv = 0.0f, g_tpwr_mv = 0.0f;           /* 핀 전압 (멀티미터 비교용) */
static float      i_off_mv = 0.0f;

static float Pwr_Mv(uint32_t ch, uint8_t n)          /* 채널 바꾸고 첫 변환은 버림 (직전 채널 전압이 남아 있음) */
{
    ADC_ChannelConfTypeDef c = {0};
    uint32_t s = 0;
    c.Channel = ch; c.Rank = ADC_REGULAR_RANK_1; c.SamplingTime = ADC_SAMPLETIME_92CYCLES_5;
    c.SingleDiff = ADC_SINGLE_ENDED; c.OffsetNumber = ADC_OFFSET_NONE; c.Offset = 0;
    HAL_ADC_ConfigChannel(&hadc1, &c);
    (void)Adc_Read(&hadc1);
    for (uint8_t i = 0; i < n; i++) s += Adc_Read(&hadc1);
    return raw_to_mv((uint16_t)(s / n), 1.0f);
}

static float Temp_FromMv(float mv)
{
    float t;
#if TEMP_SENSOR == 0
    float r;
    if (mv < 30.0f || mv > 3270.0f) return TEMP_NONE;          /* 단선 / 단락 */
  #if NTC_ON_GND_SIDE
    r = NTC_FIXED_OHM * mv / (3300.0f - mv);
  #else
    r = NTC_FIXED_OHM * (3300.0f - mv) / mv;
  #endif
    t = 1.0f / (1.0f / 298.15f + logf(r / NTC_R25_OHM) / NTC_BETA) - 273.15f;
#else
    t = (mv - LIN_MV_AT_0C) / LIN_MV_PER_C;
#endif
    if (t < -40.0f || t > 150.0f) return TEMP_NONE;
    return t;
}

static float Temp_Filter(float prev, float now)
{
    if (now < -100.0f)  return TEMP_NONE;
    if (prev < -100.0f) return now;
    return prev + (now - prev) * 0.3f;
}

static int Temp_Int(float t) { return (t < -100.0f) ? -128 : (int)(t + (t >= 0.0f ? 0.5f : -0.5f)); }

static uint8_t Temp_ToByte(float t)
{
    int v = Temp_Int(t);
    if (v == -128) return 0x80U;
    if (v > 127) v = 127;
    if (v < -127) v = -127;
    return (uint8_t)(int8_t)v;
}

static void Pwr_Init(void)
{
    GPIO_InitTypeDef g = {0};
    float s = 0.0f;

    __HAL_RCC_ADC12_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE(); __HAL_RCC_GPIOB_CLK_ENABLE(); __HAL_RCC_GPIOC_CLK_ENABLE();
    g.Mode = GPIO_MODE_ANALOG; g.Pull = GPIO_NOPULL;
    g.Pin = GPIO_PIN_2 | GPIO_PIN_3; HAL_GPIO_Init(GPIOA, &g);   /* TEMP_ADC_PWR, I_SENSE_ADC */
    g.Pin = GPIO_PIN_0;              HAL_GPIO_Init(GPIOB, &g);   /* VOLT_ADC */
    g.Pin = GPIO_PIN_3;              HAL_GPIO_Init(GPIOC, &g);   /* TEMP_ADC_DRV */

    Adc_InitOne(&hadc1, ADC1, PWR_CH_VBUS, ADC_SAMPLETIME_92CYCLES_5);
    for (int i = 0; i < 50; i++) { s += Pwr_Mv(PWR_CH_ISENSE, 2); HAL_Delay(1); }   /* 모터 꺼진 상태 전류 영점 */
    i_off_mv = s / 50.0f;
}

static void Pwr_100ms(void)
{
    static uint8_t tcnt = 0;
    float v = Pwr_Mv(PWR_CH_VBUS, 4) * VBUS_DIVIDER;
    float i = fabsf(Pwr_Mv(PWR_CH_ISENSE, 8) - i_off_mv) * 1000.0f / (INA_GAIN * SHUNT_MOHM);
    g_vbus_mv = (g_vbus_mv == 0U) ? (uint32_t)v : (uint32_t)((float)g_vbus_mv + (v - (float)g_vbus_mv) * 0.3f);
    g_i_ma += (i - g_i_ma) * 0.3f;
    if (++tcnt >= 5U)                                   /* 온도는 0.5초마다 */
    {
        tcnt = 0;
        g_tdrv_mv = Pwr_Mv(PWR_CH_TDRV, 4); g_tdrv_c = Temp_Filter(g_tdrv_c, Temp_FromMv(g_tdrv_mv));
        g_tpwr_mv = Pwr_Mv(PWR_CH_TPWR, 4); g_tpwr_c = Temp_Filter(g_tpwr_c, Temp_FromMv(g_tpwr_mv));
    }
}

/* =====================================================================
 *  부저 (PA0 / TIM2_CH1) — 위험 단계 도달 시 0.3초 x 3번
 * ===================================================================== */
#define BUZZ_TIM_HZ   1000000U                              /* TIM2 = 1MHz 타임베이스 */
#define BUZZ_ARR      ((BUZZ_TIM_HZ / BUZZ_FREQ_HZ) - 1U)

static uint8_t  buzz_i = BUZZ_COUNT, buzz_on = 0;          /* buzz_i == BUZZ_COUNT 이면 쉬는 중 */
static uint32_t buzz_t0 = 0;

static void Buzzer_Init(void)
{
    GPIO_InitTypeDef g = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();
#if BUZZ_IS_ACTIVE
    g.Pin = BUZZ_PIN; g.Mode = GPIO_MODE_OUTPUT_PP; g.Pull = GPIO_NOPULL; g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(BUZZ_PORT, &g);
    HAL_GPIO_WritePin(BUZZ_PORT, BUZZ_PIN, GPIO_PIN_RESET);
#else
    TIM_OC_InitTypeDef oc = {0};
    __HAL_RCC_TIM2_CLK_ENABLE();
    g.Pin = BUZZ_PIN; g.Mode = GPIO_MODE_AF_PP; g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW; g.Alternate = GPIO_AF1_TIM2;
    HAL_GPIO_Init(BUZZ_PORT, &g);

    htim2.Instance = TIM2;
    htim2.Init.Prescaler = (TIM_CLK_HZ / BUZZ_TIM_HZ) - 1U;  /* 80MHz -> 1MHz */
    htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim2.Init.Period = BUZZ_ARR;
    htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    if (HAL_TIM_PWM_Init(&htim2) != HAL_OK) Error_Handler();
    oc.OCMode = TIM_OCMODE_PWM1; oc.Pulse = 0;               /* 0 = 소리 없음 */
    oc.OCPolarity = TIM_OCPOLARITY_HIGH; oc.OCFastMode = TIM_OCFAST_DISABLE;
    if (HAL_TIM_PWM_ConfigChannel(&htim2, &oc, TIM_CHANNEL_1) != HAL_OK) Error_Handler();
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);
#endif
}

static void Buzzer_Set(uint8_t on)
{
#if BUZZ_IS_ACTIVE
    HAL_GPIO_WritePin(BUZZ_PORT, BUZZ_PIN, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
#else
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, on ? (BUZZ_ARR + 1U) / 2U : 0U);   /* 50% 듀티 */
#endif
}

static void Buzzer_Start(void)
{
    buzz_i = 0;
    buzz_on = 1;
    buzz_t0 = HAL_GetTick();
    Buzzer_Set(1);
}

/* 0.5 ON - 0.5 OFF - 0.5 ON - 0.5 OFF - 1.5 ON */
static void Buzzer_1ms(void)
{
    if (buzz_i >= BUZZ_COUNT) return;
    uint32_t now = HAL_GetTick();
    if (buzz_on)
    {
        if ((now - buzz_t0) >= BUZZ_ON_MS[buzz_i])
        {
            Buzzer_Set(0); buzz_on = 0; buzz_t0 = now;
            buzz_i++;                                     /* 한 번 울림 끝 (마지막이면 여기서 멈춤) */
        }
    }
    else if ((now - buzz_t0) >= BUZZ_OFF_MS)
    {
        Buzzer_Set(1); buzz_on = 1; buzz_t0 = now;
    }
}

/* =====================================================================
 *  CAN 수신 데이터
 * ===================================================================== */
typedef struct { uint8_t seen; uint32_t last_ms; uint8_t st, pos, tgt, fault, enc; uint16_t ma; } Node_t;
typedef struct { uint8_t seen; uint32_t last_ms; uint16_t co, tvoc, eco2; uint8_t aqi, fl; } Air_t;          /* 0x121/0x122 */
typedef struct { uint8_t seen; uint32_t last_ms; uint8_t agc, fl; uint16_t ang, mv; int8_t tdrv, tpwr; } NodeSt_t; /* 0x111/0x112 */
static Node_t   nd[3];                 /* 0=창문1(0x101) 1=창문2(0x102) 2=환풍기(0x201) */
static Air_t    air[2];                /* 0=구역1 1=구역2 */
static NodeSt_t nst[2];
typedef struct { uint8_t seen; uint32_t last_ms; uint8_t fl, cw_ds, pr_ds, pn, pm, src, wp; } NodeEv_t;        /* 0x131/0x132 */
static NodeEv_t nev[2];
static uint8_t  cmd_win = 0, cmd_win_z[2] = { 0, 0 }, cmd_fan = 0, cmd_seq = 0, st_seq = 0, env_seq = 0;
static uint32_t can_tx_cnt = 0, can_rx_cnt = 0, can_busoff = 0;

static uint8_t Fresh(uint8_t seen, uint32_t last_ms, uint32_t to) { return seen && (HAL_GetTick() - last_ms) < to; }

/* 판정에 쓸 수 있는 구역 값 (못 쓰면 -1) */
static uint8_t Ens_Usable(const Air_t *a)
{
    uint8_t v = (uint8_t)((a->fl >> 1) & 0x03U);
    return (a->fl & 0x01U) && (v == 0U || v == 2U);      /* 0 정상, 2 첫 가동(대략값) 은 사용, 1 예열 / 3 무효 는 제외 */
}
static int32_t Z_Co(int k)
{
    const Air_t *a = &air[k];
    if (!Fresh(a->seen, a->last_ms, AIR_TIMEOUT_MS) || !(a->fl & 0x08U) || !(a->fl & 0x10U)) return -1;
    return a->co;
}
static int32_t Z_Tvoc(int k)
{
    const Air_t *a = &air[k];
    if (!Fresh(a->seen, a->last_ms, AIR_TIMEOUT_MS) || !Ens_Usable(a)) return -1;
    return a->tvoc;
}
static int32_t Z_Eco2(int k)
{
    const Air_t *a = &air[k];
    if (!Fresh(a->seen, a->last_ms, AIR_TIMEOUT_MS) || !Ens_Usable(a) || a->eco2 < 400U) return -1;
    return a->eco2;
}
static int32_t Co2_Avg(void)           /* 두 구역 ENS160 eCO2 평균 (쓸 수 있는 것만) */
{
    int32_t s = 0, n = 0, v;
    for (int k = 0; k < 2; k++) if ((v = Z_Eco2(k)) >= 0) { s += v; n++; }
    return n ? (s + n / 2) / n : -1;
}
static int32_t Max2(int32_t a, int32_t b) { return a > b ? a : b; }

/* =====================================================================
 *  위험 등급 판정 (구역 1, 구역 2, 공용) — 올릴 땐 바로, 내릴 땐 기준의 80% 아래로 3초
 * ===================================================================== */
static uint8_t level_of(float v, const float th[3], float scale)
{
    if (v < 0.0f) return 0;            /* 값 없음 */
    v *= scale;
    return (v < th[0]) ? 0 : (v < th[1]) ? 1 : (v < th[2]) ? 2 : 3;
}

static uint8_t zone_raw(int k, float scale)
{
    uint8_t a = level_of((float)Z_Co(k),   TH_CO,   scale);
    uint8_t b = level_of((float)Z_Tvoc(k), TH_TVOC, scale);
    uint8_t lv = (a > b) ? a : b;
    if (a >= 1 && b >= 1 && lv < 3) lv++;          /* CO 와 TVOC 가 같이 오르면 한 단계 격상 */
    return lv;
}

static uint8_t shared_raw(float scale)
{
    uint8_t a = level_of(warm_done ? (float)dust_ug : -1.0f, TH_DUST, scale);
    uint8_t b = level_of((float)Co2_Avg(), TH_CO2, scale);
    return (a > b) ? a : b;
}

typedef struct { uint8_t lv; uint32_t below_since; } Hys_t;
static Hys_t   hz[2], hs;
volatile uint8_t zone_lv[2] = { 0, 0 }, shared_lv = 0;

static void Hys_Step(Hys_t *h, uint8_t up, uint8_t down)
{
    if (up >= h->lv) { h->lv = up; h->below_since = 0; return; }
    if (down < h->lv)
    {
        if (h->below_since == 0) h->below_since = HAL_GetTick() | 1U;
        if ((HAL_GetTick() - h->below_since) >= LEVEL_DOWN_HOLD_MS)
        {
            h->lv = down;                              /* 한 단계씩이 아니라 현재 수준으로 바로 */
            h->below_since = 0;
        }
    }
    else h->below_since = 0;
}

/* 등급 하향 확인 남은 ms (확인 중이 아니면 0) */
static uint32_t Hold_Left(const Hys_t *h)
{
    uint32_t el;
    if (h->below_since == 0) return 0;
    el = HAL_GetTick() - h->below_since;
    return (el >= LEVEL_DOWN_HOLD_MS) ? 1U : (LEVEL_DOWN_HOLD_MS - el);   /* 다 됐으면 다음 1초 판정 대기 -> 1 */
}

/* 창문 k 의 목표가 내려가기까지 남은 ms.
 * 창문 등급 = 여러 등급 중 최댓값 -> 최댓값을 가진 등급이 "모두" 내려가는 중일 때만 내려감 (가장 늦은 것 기준) */
static uint32_t Win_DownLeft(int k)
{
    const Hys_t *src[3];
    int n = 0;
    uint8_t top = 0;
    uint32_t left = 0;

    src[n++] = &hs;
    if (ZONE_WINDOW) src[n++] = &hz[k];
    else { src[n++] = &hz[0]; src[n++] = &hz[1]; }

    for (int i = 0; i < n; i++) if (src[i]->lv > top) top = src[i]->lv;
    if (top == 0) return 0;                        /* 이미 0 단계 -> 내려갈 것 없음 */
    for (int i = 0; i < n; i++)
    {
        uint32_t l;
        if (src[i]->lv != top) continue;
        l = Hold_Left(src[i]);
        if (l == 0) return 0;                      /* 최고 등급 하나라도 그대로면 안 내려감 */
        if (l > left) left = l;
    }
    return left;
}

static void Risk_1s(void)
{
    static uint8_t prev_level = 0;
    for (int k = 0; k < 2; k++) { Hys_Step(&hz[k], zone_raw(k, 1.0f), zone_raw(k, 1.25f)); zone_lv[k] = hz[k].lv; }
    Hys_Step(&hs, shared_raw(1.0f), shared_raw(1.25f));
    shared_lv = hs.lv;

    uint8_t lv = shared_lv;
    if (zone_lv[0] > lv) lv = zone_lv[0];
    if (zone_lv[1] > lv) lv = zone_lv[1];
    risk_level = lv;

    /* 위험 단계에 "새로 도달한 순간" 부저 */
    if (risk_level >= BUZZ_LEVEL && prev_level < BUZZ_LEVEL) Buzzer_Start();
    prev_level = risk_level;
}

static void AutoVent_1s(void)
{
#if AUTO_VENT
    if (!win_open && risk_level >= 1 && (state == ST_IDLE || state == ST_HOLD))
    {
        Motor_Start(DIR_CW);
        win_open = 1;
    }
    else if (win_open && risk_level == 0 && (state == ST_IDLE || state == ST_HOLD))
    {
        Motor_Start(DIR_CCW);
        win_open = 0;
    }
#endif
}

/* 대시보드와 같은 방식의 위험지수 0~100 (주의/경고/위험 = 25/50/75) */
static float gnorm(float v, const float t[3])
{
    if (v <= 0.0f) return 0.0f;
    if (v < t[0]) return 25.0f * v / t[0];
    if (v < t[1]) return 25.0f + 25.0f * (v - t[0]) / (t[1] - t[0]);
    if (v < t[2]) return 50.0f + 25.0f * (v - t[1]) / (t[2] - t[1]);
    float r = 75.0f + 25.0f * (v - t[2]) / (t[2] * 0.5f);
    return r > 100.0f ? 100.0f : r;
}

/* 환풍기 권장 PWM % (0x100 [2]) : 가장 나쁜 항목의 지수에 비례 */
static uint8_t Fan_Pct(void)
{
    float idx = warm_done ? gnorm((float)dust_ug, TH_DUST) : 0.0f, v;
    if ((v = gnorm((float)Co2_Avg(), TH_CO2)) > idx) idx = v;
    for (int k = 0; k < 2; k++)
    {
        if ((v = gnorm((float)Z_Co(k), TH_CO)) > idx) idx = v;
        if ((v = gnorm((float)Z_Tvoc(k), TH_TVOC)) > idx) idx = v;
    }
    if (idx < FAN_START_IDX) return 0;
    float p = FAN_MIN_PCT + (idx - FAN_START_IDX) * (100.0f - FAN_MIN_PCT) / (100.0f - FAN_START_IDX);
    if (p > 100.0f) p = 100.0f;
    return (uint8_t)p;
}

/* =====================================================================
 *  CAN 마스터 (FDCAN1, PA11 RX / PA12 TX, 500kbps)
 * ===================================================================== */
static void CAN_Init(void)
{
    RCC_PeriphCLKInitTypeDef pc = {0};
    GPIO_InitTypeDef g = {0};
    FDCAN_FilterTypeDef f = {0};

    pc.PeriphClockSelection = RCC_PERIPHCLK_FDCAN;
    pc.FdcanClockSelection  = RCC_FDCANCLKSOURCE_PCLK1;       /* 80MHz (기본값 HSE 는 이 보드에 없음) */
    HAL_RCCEx_PeriphCLKConfig(&pc);
    __HAL_RCC_FDCAN_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    g.Pin = GPIO_PIN_11 | GPIO_PIN_12; g.Mode = GPIO_MODE_AF_PP; g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_HIGH; g.Alternate = GPIO_AF9_FDCAN1;
    HAL_GPIO_Init(GPIOA, &g);

    hfdcan1.Instance = FDCAN1;
    hfdcan1.Init.ClockDivider = FDCAN_CLOCK_DIV1;
    hfdcan1.Init.FrameFormat = FDCAN_FRAME_CLASSIC;
    hfdcan1.Init.Mode = FDCAN_MODE_NORMAL;
    hfdcan1.Init.AutoRetransmission = ENABLE;
    hfdcan1.Init.TransmitPause = DISABLE;
    hfdcan1.Init.ProtocolException = DISABLE;
    hfdcan1.Init.NominalPrescaler = 10;                        /* 80MHz/10 = 8MHz, 16tq -> 500kbps */
    hfdcan1.Init.NominalSyncJumpWidth = 2;
    hfdcan1.Init.NominalTimeSeg1 = 13;                         /* 샘플 포인트 87.5% */
    hfdcan1.Init.NominalTimeSeg2 = 2;
    hfdcan1.Init.DataPrescaler = 10; hfdcan1.Init.DataSyncJumpWidth = 2;
    hfdcan1.Init.DataTimeSeg1 = 13;  hfdcan1.Init.DataTimeSeg2 = 2;
    hfdcan1.Init.StdFiltersNbr = 1;
    hfdcan1.Init.ExtFiltersNbr = 0;
    hfdcan1.Init.TxFifoQueueMode = FDCAN_TX_FIFO_OPERATION;
    if (HAL_FDCAN_Init(&hfdcan1) != HAL_OK) Error_Handler();

    f.IdType = FDCAN_STANDARD_ID; f.FilterIndex = 0;           /* 0x101 ~ 0x2FF 만 받음 */
    f.FilterType = FDCAN_FILTER_RANGE; f.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
    f.FilterID1 = 0x101; f.FilterID2 = 0x2FF;
    HAL_FDCAN_ConfigFilter(&hfdcan1, &f);
    HAL_FDCAN_ConfigGlobalFilter(&hfdcan1, FDCAN_REJECT, FDCAN_REJECT, FDCAN_REJECT_REMOTE, FDCAN_REJECT_REMOTE);
    HAL_FDCAN_ConfigRxFifoOverwrite(&hfdcan1, FDCAN_RX_FIFO0, FDCAN_RX_FIFO_OVERWRITE);
    if (HAL_FDCAN_Start(&hfdcan1) != HAL_OK) Error_Handler();
}

static void CAN_Send(uint32_t id, uint8_t *d)
{
    FDCAN_TxHeaderTypeDef h = {0};
    if (HAL_FDCAN_GetTxFifoFreeLevel(&hfdcan1) == 0) return;
    h.Identifier = id; h.IdType = FDCAN_STANDARD_ID; h.TxFrameType = FDCAN_DATA_FRAME;
    h.DataLength = FDCAN_DLC_BYTES_8; h.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
    h.BitRateSwitch = FDCAN_BRS_OFF; h.FDFormat = FDCAN_CLASSIC_CAN;
    h.TxEventFifoControl = FDCAN_NO_TX_EVENTS; h.MessageMarker = 0;
    if (HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan1, &h, d) == HAL_OK) can_tx_cnt++;
}

static void CAN_Poll(void)
{
    FDCAN_RxHeaderTypeDef h;
    uint8_t d[8];
    while (HAL_FDCAN_GetRxFifoFillLevel(&hfdcan1, FDCAN_RX_FIFO0) > 0)
    {
        if (HAL_FDCAN_GetRxMessage(&hfdcan1, FDCAN_RX_FIFO0, &h, d) != HAL_OK) break;
        uint32_t id = h.Identifier, now = HAL_GetTick();
        if (id == CAN_ID_WIN1 || id == CAN_ID_WIN2 || id == CAN_ID_FAN)
        {
            Node_t *n = &nd[id == CAN_ID_FAN ? 2 : (int)(id - CAN_ID_WIN1)];
            n->seen = 1; n->last_ms = now; can_rx_cnt++;
            if (id != CAN_ID_FAN) { n->st = d[0]; n->pos = d[1]; n->tgt = d[2]; n->fault = d[3]; n->enc = d[4]; n->ma = (uint16_t)((d[5] << 8) | d[6]); }
            else                  { n->pos = d[0]; n->tgt = d[1]; }
        }
        else if (id == CAN_ID_WIN1_ST || id == CAN_ID_WIN2_ST)
        {
            NodeSt_t *s = &nst[id - CAN_ID_WIN1_ST];
            s->seen = 1; s->last_ms = now; can_rx_cnt++;
            s->agc = d[0]; s->fl = d[1]; s->ang = (uint16_t)(((d[2] << 8) | d[3]) & 0x3FFFU);
            s->mv = (uint16_t)((d[4] << 8) | d[5]); s->tdrv = (int8_t)d[6]; s->tpwr = (int8_t)d[7];
        }
        else if (id == CAN_ID_WIN1_EV || id == CAN_ID_WIN2_EV)
        {
            NodeEv_t *e = &nev[id - CAN_ID_WIN1_EV];
            e->seen = 1; e->last_ms = now; can_rx_cnt++;
            e->fl = d[0]; e->cw_ds = d[1]; e->pr_ds = d[2]; e->pn = d[3]; e->pm = d[4]; e->src = d[5]; e->wp = d[6];
        }
        else if (id == CAN_ID_AIR1 || id == CAN_ID_AIR2)
        {
            Air_t *a = &air[id - CAN_ID_AIR1];
            a->seen = 1; a->last_ms = now; can_rx_cnt++;
            a->co = (uint16_t)((d[0] << 8) | d[1]); a->tvoc = (uint16_t)((d[2] << 8) | d[3]);
            a->eco2 = (uint16_t)((d[4] << 8) | d[5]); a->aqi = d[6]; a->fl = d[7];
        }
    }
    /* 버스 오프 자동 복구 */
    if (hfdcan1.Instance->PSR & FDCAN_PSR_BO)
    {
        can_busoff++;
        CLEAR_BIT(hfdcan1.Instance->CCCR, FDCAN_CCCR_INIT);
    }
}

/* 0x100 명령 (100ms) */
static void CAN_Tx100ms(void)
{
    uint8_t lv = risk_level > 3 ? 3 : risk_level;
    cmd_win = WIN_PCT_BY_LEVEL[lv];
    for (int k = 0; k < 2; k++)
    {
        uint8_t wl = zone_lv[k] > shared_lv ? zone_lv[k] : shared_lv;
        cmd_win_z[k] = ZONE_WINDOW ? WIN_PCT_BY_LEVEL[wl > 3 ? 3 : wl] : cmd_win;
    }
    cmd_fan = Fan_Pct();
    uint8_t d[8] = { lv, cmd_win, cmd_fan, cmd_seq++, (uint8_t)((warm_done ? 0x01U : 0U) | 0x02U),
                     cmd_win_z[0], cmd_win_z[1],
                     (uint8_t)((zone_lv[0] & 3U) | ((zone_lv[1] & 3U) << 2) | ((shared_lv & 3U) << 4)) };
    CAN_Send(CAN_ID_CMD, d);
}

/* 0x110 이 보드 전원 상태 (200ms) */
static void CAN_TxStatus(void)
{
    uint16_t mv = (uint16_t)(g_vbus_mv > 65535U ? 65535U : g_vbus_mv);
    uint16_t ma = (uint16_t)(g_i_ma > 65535.0f ? 65535.0f : g_i_ma);
    uint8_t d[8] = { (uint8_t)(mv >> 8), (uint8_t)mv, (uint8_t)(ma >> 8), (uint8_t)ma,
                     Temp_ToByte(g_tdrv_c), Temp_ToByte(g_tpwr_c), (uint8_t)(warm_done ? 1U : 0U), st_seq++ };
    CAN_Send(CAN_ID_MST_ST, d);
}

/* 0x120 먼지 / 온습도 (500ms) */
static void CAN_TxEnv(void)
{
    uint16_t du = (uint16_t)(dust_ug < 0 ? 0 : dust_ug);
    uint16_t hm = dht_ok ? hum10 : 0U;
    int16_t  tp = dht_ok ? temp10 : 0;
    uint8_t d[8] = { (uint8_t)(du >> 8), (uint8_t)du, (uint8_t)((uint16_t)tp >> 8), (uint8_t)tp,
                     (uint8_t)(hm >> 8), (uint8_t)hm,
                     (uint8_t)((dht_ok ? 0x01U : 0U) | (warm_done ? 0x02U : 0U)), env_seq++ };
    CAN_Send(CAN_ID_ENV, d);
}

static uint8_t Node_Alive(int k) { return nd[k].seen && (HAL_GetTick() - nd[k].last_ms) < NODE_TIMEOUT_MS; }
static long    Node_Age(int k)
{
    if (!nd[k].seen) return -1;                    /* 한 번도 안 들어옴 = 미연결 */
    uint32_t a = HAL_GetTick() - nd[k].last_ms;
    return (long)(a > 99999U ? 99999U : a);
}

/* =====================================================================
 *  고장 감시 및 LED
 * ===================================================================== */
static void Fault_Led_1ms(void)
{
    if (USE_NFAULT_CHECK && HAL_GetTick() >= 500U && state != ST_FAULT && state != ST_IDLE)
    {
        static uint16_t low_ms = 0;
        low_ms = (HAL_GPIO_ReadPin(DRV_NFAULT_PORT, DRV_NFAULT_PIN) == GPIO_PIN_RESET) ? low_ms + 1U : 0U;
        if (low_ms >= 5U) Enter_Fault();
    }

    uint32_t t = HAL_GetTick();
    GPIO_PinState normal = GPIO_PIN_RESET, run = GPIO_PIN_RESET, red = GPIO_PIN_RESET;
    switch (state)
    {
    case ST_IDLE:
    case ST_HOLD:     normal = GPIO_PIN_SET; break;
    case ST_RUN:
    case ST_STOPPING: run = GPIO_PIN_SET; break;
    case ST_FAULT:    red = ((t / 100U) & 1U) ? GPIO_PIN_SET : GPIO_PIN_RESET; break;
    default: break;
    }
    if (state != ST_FAULT && risk_level >= 2) red = GPIO_PIN_SET;

    /* CAN 노드가 하나라도 끊기면 빨간 LED 천천히 깜빡 (위험 등급 표시가 우선) */
    uint8_t any_lost = (nd[0].seen && !Node_Alive(0)) || (nd[1].seen && !Node_Alive(1)) || (nd[2].seen && !Node_Alive(2));
    if (state != ST_FAULT && risk_level < 2 && any_lost) red = ((t / 500U) & 1U) ? GPIO_PIN_SET : GPIO_PIN_RESET;

    HAL_GPIO_WritePin(LED_NORMAL_PORT, LED_NORMAL_PIN, normal);
    HAL_GPIO_WritePin(LED_RUN_PORT,    LED_RUN_PIN,    run);
    HAL_GPIO_WritePin(LED_RED_PORT,    LED_RED_PIN,    red);
}

/* =====================================================================
 *  텔레메트리 (UART)
 * ===================================================================== */
#define TELE_USE_USART1    1
#define TELE_USE_USART2    1
#define TELE_BAUD          115200U

static char     tele_buf[1800];
static uint16_t tele_len = 0, tele_pos1 = 0, tele_pos2 = 0;
static volatile uint32_t tele_seq = 0;

static void Tele_OneUartInit(UART_HandleTypeDef *h, USART_TypeDef *inst)
{
    h->Instance                    = inst;
    h->Init.BaudRate               = TELE_BAUD;
    h->Init.WordLength             = UART_WORDLENGTH_8B;
    h->Init.StopBits               = UART_STOPBITS_1;
    h->Init.Parity                 = UART_PARITY_NONE;
    h->Init.Mode                   = UART_MODE_TX_RX;
    h->Init.HwFlowCtl              = UART_HWCONTROL_NONE;
    h->Init.OverSampling           = UART_OVERSAMPLING_16;
    h->Init.OneBitSampling         = UART_ONE_BIT_SAMPLE_DISABLE;
    h->Init.ClockPrescaler         = UART_PRESCALER_DIV1;
    h->AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
    if (HAL_UART_Init(h) != HAL_OK) Error_Handler();
}

static void Tele_UartInit(void)
{
    GPIO_InitTypeDef g = {0};
    g.Mode = GPIO_MODE_AF_PP; g.Pull = GPIO_PULLUP; g.Speed = GPIO_SPEED_FREQ_LOW;
#if TELE_USE_USART1
    __HAL_RCC_USART1_CLK_ENABLE(); __HAL_RCC_GPIOC_CLK_ENABLE();
    g.Pin = GPIO_PIN_4 | GPIO_PIN_5; g.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(GPIOC, &g);
    Tele_OneUartInit(&huart1, USART1);
#endif
#if TELE_USE_USART2
    __HAL_RCC_USART2_CLK_ENABLE(); __HAL_RCC_GPIOB_CLK_ENABLE();
    g.Pin = GPIO_PIN_3 | GPIO_PIN_4; g.Alternate = GPIO_AF7_USART2;
    HAL_GPIO_Init(GPIOB, &g);
    Tele_OneUartInit(&huart2, USART2);
#endif
}

static void Tele_Service(void)
{
#if TELE_USE_USART1
    if (tele_pos1 < tele_len && (USART1->ISR & USART_ISR_TXE_TXFNF)) USART1->TDR = (uint8_t)tele_buf[tele_pos1++];
#endif
#if TELE_USE_USART2
    if (tele_pos2 < tele_len && (USART2->ISR & USART_ISR_TXE_TXFNF)) USART2->TDR = (uint8_t)tele_buf[tele_pos2++];
#endif
}

static uint8_t Tele_Busy(void)
{
#if TELE_USE_USART1
    if (tele_pos1 < tele_len) return 1;
#endif
#if TELE_USE_USART2
    if (tele_pos2 < tele_len) return 1;
#endif
    return 0;
}

static void fmt1(char *out, int v)
{
    int neg = v < 0; if (neg) v = -v;
    sprintf(out, "%s%d.%d", neg ? "-" : "", v / 10, v % 10);
}

static void fmtv(char *out, uint32_t mv)           /* mV -> "12.34" */
{
    uint32_t cv = (mv + 5U) / 10U;
    sprintf(out, "%lu.%02lu", (unsigned long)(cv / 100U), (unsigned long)(cv % 100U));
}

static int tele_n = 0;
static void J(const char *f, ...)
{
    va_list ap;
    if (tele_n < 0 || tele_n >= (int)sizeof(tele_buf)) return;
    va_start(ap, f);
    int r = vsnprintf(tele_buf + tele_n, sizeof(tele_buf) - (size_t)tele_n, f, ap);
    va_end(ap);
    if (r > 0) tele_n += r;
}

/* 구역 하나: 대시보드 키 z{n}co / z{n}tvoc / z{n}lv  + 추가 z{n}co2 / z{n}aqi / z{n}ens / z{n}cow */
static void Tele_Zone(int k)
{
    const Air_t *a = &air[k];
    uint8_t ok = Fresh(a->seen, a->last_ms, AIR_TIMEOUT_MS);
    int32_t co = Z_Co(k), tv = Z_Tvoc(k), ec = Z_Eco2(k);
    J("\"z%dlv\":%u,\"z%dco\":%ld,\"z%dtvoc\":%ld,\"z%dco2\":%ld,\"z%daqi\":%u,\"z%dens\":%d,\"z%dcow\":%u,",
      k + 1, (unsigned)zone_lv[k], k + 1, (long)(co < 0 ? 0 : co), k + 1, (long)(tv < 0 ? 0 : tv),
      k + 1, (long)(ec < 0 ? 0 : ec), k + 1, (unsigned)(ok ? a->aqi : 0U),
      k + 1, (ok && (a->fl & 0x01U)) ? (int)((a->fl >> 1) & 0x03U) : -1,
      k + 1, (unsigned)((ok && (a->fl & 0x08U)) ? 1U : 0U));
}

/* 창문 하나의 남은 시간: w{n}dn / w{n}tg / w{n}cw / w{n}wp / w{n}pf / w{n}pr / w{n}pn / w{n}pm / w{n}ps */
static void Tele_Win(int k)
{
    const NodeEv_t *e = &nev[k];
    uint8_t ok = Fresh(e->seen, e->last_ms, EV_TIMEOUT_MS);
    uint32_t age = ok ? (HAL_GetTick() - e->last_ms) : 0U;
    uint32_t cw = 0, pr = 0;
    if (ok && (e->fl & 0x01U)) { cw = (uint32_t)e->cw_ds * 100U; cw = (cw > age) ? cw - age : 0U; }   /* 받은 뒤 지난 시간만큼 빼기 */
    if (ok && (e->fl & 0x02U)) { pr = (uint32_t)e->pr_ds * 100U; pr = (pr > age) ? pr - age : 0U; }
    J("\"w%ddn\":%lu,\"w%dtg\":%d,\"w%dcw\":%lu,\"w%dwp\":%u,\"w%dpf\":%u,\"w%dpr\":%lu,\"w%dpn\":%u,\"w%dpm\":%u,\"w%dps\":%u,",
      k + 1, (unsigned long)Win_DownLeft(k), k + 1, Node_Alive(k) ? (int)nd[k].tgt : -1,
      k + 1, (unsigned long)cw, k + 1, (unsigned)(ok ? e->wp : 255U), k + 1, (unsigned)(ok ? e->fl : 0U),
      k + 1, (unsigned long)pr, k + 1, (unsigned)(ok ? e->pn : 0U), k + 1, (unsigned)(ok ? e->pm : 0U),
      k + 1, (unsigned)(ok ? e->src : 0U));
}

static void Tele_Send(void)
{
    if (Tele_Busy()) return;
    tele_seq++;
    tele_n = 0;

    char tb[12], hb[12], vb[12], nv[2][12];
    fmt1(tb, dht_ok ? temp10 : 0);
    fmt1(hb, dht_ok ? hum10  : 0);
    fmtv(vb, g_vbus_mv);
    for (int k = 0; k < 2; k++) fmtv(nv[k], Fresh(nst[k].seen, nst[k].last_ms, 1000U) ? nst[k].mv : 0U);

    /* 창문/환풍기 값은 CAN 노드가 보고한 실제 값 (노드가 끊기면 0) */
    int w1 = Node_Alive(0) ? nd[0].pos : 0;
    int w2 = Node_Alive(1) ? nd[1].pos : 0;
    int fn = Node_Alive(2) ? nd[2].pos : 0;
    int32_t co = Max2(Z_Co(0), Z_Co(1)), tv = Max2(Z_Tvoc(0), Z_Tvoc(1)), c2 = Co2_Avg();

    J("{\"seq\":%lu,\"level\":%u,\"slv\":%u,", (unsigned long)tele_seq, (unsigned)risk_level, (unsigned)shared_lv);
    Tele_Zone(0);
    Tele_Zone(1);
    J("\"co\":%ld,\"tvoc\":%ld,\"co2\":%ld,\"dust\":%ld,\"temp\":%s,\"hum\":%s,",
      (long)(co < 0 ? 0 : co), (long)(tv < 0 ? 0 : tv), (long)(c2 < 0 ? 0 : c2), (long)dust_ug, tb, hb);
    J("\"win1\":%d,\"win2\":%d,\"cmd_win\":%u,\"cmd_win1\":%u,\"cmd_win2\":%u,\"fan\":%d,\"cmd_fan\":%u,",
      w1, w2, cmd_win, cmd_win_z[0], cmd_win_z[1], fn, cmd_fan);
    J("\"cur1\":%u.%02u,\"cur2\":%u.%02u,\"st1\":%u,\"st2\":%u,\"flt1\":%u,\"flt2\":%u,\"load1\":%u,\"load2\":%u,",
      nd[0].ma / 1000U, (nd[0].ma % 1000U) / 10U, nd[1].ma / 1000U, (nd[1].ma % 1000U) / 10U,
      nd[0].st, nd[1].st, nd[0].fault, nd[1].fault, nd[0].enc, nd[1].enc);
    Tele_Win(0);
    Tele_Win(1);
    J("\"nodes\":[0,%ld,%ld,%ld],", Node_Age(0), Node_Age(1), Node_Age(2));
    J("\"m_v\":%s,\"m_ma\":%u,\"m_tdrv\":%d,\"m_tpwr\":%d,", vb, (unsigned)g_i_ma, Temp_Int(g_tdrv_c), Temp_Int(g_tpwr_c));
    for (int k = 0; k < 2; k++)
        J("\"n%d_v\":%s,\"n%d_tdrv\":%d,\"n%d_tpwr\":%d,\"n%d_agc\":%u,\"n%d_ang\":%u,",
          k + 1, nv[k], k + 1, (int)nst[k].tdrv, k + 1, (int)nst[k].tpwr, k + 1, (unsigned)nst[k].agc, k + 1, (unsigned)nst[k].ang);
    J("\"can_tx\":%lu,\"can_rx\":%lu,\"busoff\":%lu,\"state\":%d,\"rpm\":%d,\"warm\":%d,\"dht\":%d,\"dust_mv\":%ld,\"dust_base\":%ld,\"dust_duty\":%d,\"dust_per\":%lu,\"dust_ne\":%u}\n",
      (unsigned long)can_tx_cnt, (unsigned long)can_rx_cnt, (unsigned long)can_busoff,
      (int)state, (int)(cur_rpm * (float)dir), warm_done ? 0 : 1, (int)dht_err, (long)dust_mv, (long)dust_base_mv,
      (int)(dust_duty * 10.0f), (unsigned long)dust_period_us, (unsigned)dust_noedge);

    if (tele_n >= (int)sizeof(tele_buf)) { tele_n = 0; return; }   /* 잘린 줄은 보내지 않음 */
    tele_len = (uint16_t)tele_n; tele_pos1 = 0; tele_pos2 = 0;
}

/* =====================================================================
 *  main
 * ===================================================================== */
int main(void)
{
    HAL_Init();
    SystemClock_Config();

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    MX_GPIO_Init();
    Stepper_BuildTable();
    MX_TIM1_Init();
    Sensors_Init();
    Tele_UartInit();
    Buzzer_Init();
    CAN_Init();
    Pwr_Init();                    /* 모터 꺼진 상태에서 전류 영점 */

    last_cyc = DWT->CYCCNT;
    uint32_t last_ms = HAL_GetTick();
    uint32_t t_dust = 0, t_pwr = 30, t_1s = 0, t_dht = 1500, t_can = 0, t_st = 50, t_env = 70;

    while (1)
    {
        if (TIM1->SR & TIM_SR_UIF)
        {
            TIM1->SR = ~TIM_SR_UIF;
            if (state != ST_FAULT) Stepper_PwmUpdate();
        }

        Tele_Service();
        CAN_Poll();

        uint32_t now = HAL_GetTick();
        if (now != last_ms)
        {
            last_ms = now;
            Stepper_1ms();
            Button_1ms();
            Fault_Led_1ms();
            Buzzer_1ms();

            if ((now - t_dust) >= 10U)  { t_dust = now; Dust_Sample_PWM(); }
            if ((int32_t)(now - t_pwr) >= 100) { t_pwr = now; Pwr_100ms(); }
            if ((int32_t)(now - t_st)  >= 200) { t_st  = now; CAN_TxStatus(); }
            if ((int32_t)(now - t_env) >= 500) { t_env = now; CAN_TxEnv(); }
            if ((now - t_can)  >= 100U) { t_can  = now; CAN_Tx100ms(); }
            if (now >= t_dht)
            {
                t_dht = now + 2000U;
                dht_err = DHT22_Read();
                if (dht_err == 0) dht_ok = 1;
            }
            if ((now - t_1s) >= 1000U)
            {
                t_1s = now;
                Sensors_Convert_1s();
                Risk_1s();
                AutoVent_1s();
                Tele_Send();
            }
        }
    }
}

/* =====================================================================
 *  클럭 및 주변장치 초기화 (기존 그대로)
 * ===================================================================== */
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

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    HAL_GPIO_WritePin(GPIOB, DRV_EN_PIN | DRV_SLP_PIN | LED_RED_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOC, LED_NORMAL_PIN | LED_RUN_PIN, GPIO_PIN_RESET);

    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    g.Pin = DRV_EN_PIN | DRV_SLP_PIN | LED_RED_PIN;
    HAL_GPIO_Init(GPIOB, &g);
    g.Pin = LED_NORMAL_PIN | LED_RUN_PIN;
    HAL_GPIO_Init(GPIOC, &g);

    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_PULLUP;
    g.Pin  = DRV_NFAULT_PIN | SW1_PIN;
    HAL_GPIO_Init(GPIOB, &g);
}

/* TIM1 PWM 모터 제어 초기화 */
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

/* TIM3 CH2 PWM Input Capture 초기화 (PA4) */
static void MX_TIM3_Init(void)
{
    GPIO_InitTypeDef g = {0};
    TIM_SlaveConfigTypeDef sSlaveConfig = {0};
    TIM_IC_InitTypeDef sICConfig = {0};

    __HAL_RCC_TIM3_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    /* PA4 -> TIM3_CH2 설정 */
    g.Pin = DUST_PWM_PIN;
    g.Mode = GPIO_MODE_AF_PP;
    g.Pull = GPIO_PULLDOWN;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    g.Alternate = GPIO_AF2_TIM3;
    HAL_GPIO_Init(DUST_PWM_PORT, &g);

    htim3.Instance = TIM3;
    htim3.Init.Prescaler = 79; /* 80MHz / 80 = 1MHz 타임베이스 (1us 단위) */
    htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim3.Init.Period = 0xFFFF;
    htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_IC_Init(&htim3) != HAL_OK) Error_Handler();

    /* Slave Mode: Reset Mode (TI2FP2 신호에서 카운터 리셋) */
    sSlaveConfig.SlaveMode = TIM_SLAVEMODE_RESET;
    sSlaveConfig.InputTrigger = TIM_TS_TI2FP2;
    sSlaveConfig.TriggerPolarity = TIM_INPUTCHANNELPOLARITY_RISING;
    sSlaveConfig.TriggerFilter = 0;
    if (HAL_TIM_SlaveConfigSynchro(&htim3, &sSlaveConfig) != HAL_OK) Error_Handler();

    /* Channel 1: Falling Edge (High 구간 시간 측정) */
    sICConfig.ICPolarity = TIM_ICPOLARITY_FALLING;
    sICConfig.ICSelection = TIM_ICSELECTION_INDIRECTTI;
    sICConfig.ICPrescaler = TIM_ICPSC_DIV1;
    sICConfig.ICFilter = 0;
    if (HAL_TIM_IC_ConfigChannel(&htim3, &sICConfig, TIM_CHANNEL_1) != HAL_OK) Error_Handler();

    /* Channel 2: Rising Edge (전체 주기 측정) */
    sICConfig.ICPolarity = TIM_ICPOLARITY_RISING;
    sICConfig.ICSelection = TIM_ICSELECTION_DIRECTTI;
    if (HAL_TIM_IC_ConfigChannel(&htim3, &sICConfig, TIM_CHANNEL_2) != HAL_OK) Error_Handler();

    HAL_TIM_IC_Start(&htim3, TIM_CHANNEL_1);
    HAL_TIM_IC_Start(&htim3, TIM_CHANNEL_2);
}

void Error_Handler(void)
{
    __disable_irq();
    HAL_GPIO_WritePin(DRV_EN_PORT, DRV_EN_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(DRV_SLP_PORT, DRV_SLP_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED_RED_PORT, LED_RED_PIN, GPIO_PIN_SET);
    while (1) { }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
    (void)file; (void)line;
}
#endif
