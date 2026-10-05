/*
 * main_airguard_can_1v3.c — 1.3 보드 마스터
 *   센서 2종(미세먼지 PWM, DHT22) + 위험등급 + 스텝모터(로컬) + ESP32 텔레메트리
 *   + CAN 마스터 : 명령 0x100 송신, 노드 하트비트 0x101/0x102/0x201 수신
 *   + 부저       : 위험 단계 도달 시 0.3초 x 3번
 *   (CO 센서/ADC 는 사용하지 않음 — 위험등급·환풍기는 미세먼지 기준으로만 판정)
 *
 * 핀 (CubeMX 핀맵 기준)
 *   DUST_PWM PA4 (TIM3_CH2 PWM 입력 캡처)
 *   DHT22    PA15                   Buzzer   PA0 (TIM2_CH1)
 *   FDCAN1   PA11 RX / PA12 TX      USART1   PC4 TX / PC5 RX,  USART2 PB3 TX / PB4 RX
 *   모터     PA8/PA9/PA10 (TIM1), EN PB14, nSLEEP PB9, nFault PB15, SW PB13
 *   LED      NORMAL PC10, RUN PC11, Red PB7
 *
 * CAN 메시지 (500kbps, 표준 ID, 8바이트)
 *   0x100 마스터 명령 : [0]등급 [1]창문목표% [2]환풍기% [3]seq [4]플래그(bit0=예열완료)
 *   0x101/0x102 창문  : [0]상태 [1]현재% [2]목표% [3]고장코드 [4]부하율% [5..6]전류mA [7]seq
 *                       고장코드: 1 엔코더, 2 걸림, 3 범위 이탈, 4 드라이버, 5 끼임(전류)
 *   0x201 환풍기      : [0]현재PWM% [1]목표PWM% [7]seq
 */

#include "main.h"
#include <math.h>
#include <stdio.h>

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
#define AUTO_VENT           0        /* 1: 위험등급에 따라 마스터 자체 모터로 창문 개폐 (창문은 이제 CAN 노드가 담당) */
#define WARMUP_MS           60000U   /* 센서 예열 시간 (최소 1분 권장, 시험할 땐 10000U 로 줄여도 됨) */

#define DUST_DIVIDER        1.0f     /* PA4 앞 분압비 */
#define DUST_UG_PER_V       200.0f   /* 1V 상승 = 200 µg/m³ */

/* 등급 기준 */
static const float TH_DUST[3] = { 35.0f, 75.0f, 115.0f };   /* µg/m³ */
#define LEVEL_DOWN_HOLD_MS  3000U    /* 등급 내릴 때 확인 시간 (확인 후 현재 수준으로 바로 복귀) */

/* CAN 제어 */
static const uint8_t WIN_PCT_BY_LEVEL[4] = { 0, 50, 75, 100 };   /* 등급별 창문 목표 개방률 (노드는 0/50/75/100 네 단계) */
#define FAN_START_IDX       15.0f    /* 위험지수(0~100) 이 값부터 환풍기 가동 */
#define FAN_MIN_PCT         35       /* 가동 시 최소 PWM % */
#define CAN_ID_CMD          0x100U
#define CAN_ID_WIN1         0x101U
#define CAN_ID_WIN2         0x102U
#define CAN_ID_FAN          0x201U
#define NODE_TIMEOUT_MS     500U     /* 하트비트가 이 시간 넘게 없으면 응답 없음 */

/* 부저 */
#define BUZZ_LEVEL          3        /* 이 등급에 "도달하는 순간" 울림 (3 = 위험) */
#define BUZZ_COUNT          3        /* 울리는 횟수 */
#define BUZZ_ON_MS          300U     /* 한 번 울리는 시간 */
#define BUZZ_OFF_MS         300U     /* 사이 쉬는 시간 */
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
 *  센서 (Dust: PWM Capture, DHT22)
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

/* ---- 미세먼지 PWM 주기 및 Duty 측정 ---- */
static void Dust_Sample_PWM(void)
{
    /* TIM3 CH1: Pulse Width(High 구간), CH2: Period(주기) */
    uint32_t period = HAL_TIM_ReadCapturedValue(&htim3, TIM_CHANNEL_2);
    uint32_t high   = HAL_TIM_ReadCapturedValue(&htim3, TIM_CHANNEL_1);

    pwm_period   = period;
    pwm_high_dur = high;

    if (period > 0)
    {
        dust_duty = ((float)high / (float)period) * 100.0f; /* % 단위 */

        /* Duty 비율을 mV 단위 등가 값으로 환산 (3.3V 기준) */
        float mv = (dust_duty / 100.0f) * 3300.0f * DUST_DIVIDER;
        dust_f = (dust_f <= 0.0f) ? mv : dust_f + (mv - dust_f) * 0.05f;
    }
}

/* ---- 기준선 갱신 및 데이터 환산 (1초 마다) ---- */
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
 *  부저 (PA0 / TIM2_CH1) — 위험 단계 도달 시 0.3초 x 3번
 * ===================================================================== */
#define BUZZ_TIM_HZ   1000000U                              /* TIM2 = 1MHz 타임베이스 */
#define BUZZ_ARR      ((BUZZ_TIM_HZ / BUZZ_FREQ_HZ) - 1U)

static uint8_t  buzz_left = 0, buzz_on = 0;
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
    buzz_left = BUZZ_COUNT;
    buzz_on = 1;
    buzz_t0 = HAL_GetTick();
    Buzzer_Set(1);
}

static void Buzzer_1ms(void)
{
    if (buzz_left == 0) return;
    uint32_t now = HAL_GetTick();
    if (buzz_on)
    {
        if ((now - buzz_t0) >= BUZZ_ON_MS)
        {
            Buzzer_Set(0); buzz_on = 0; buzz_t0 = now;
            buzz_left--;                                  /* 한 번 울림 끝 */
        }
    }
    else if ((now - buzz_t0) >= BUZZ_OFF_MS)
    {
        Buzzer_Set(1); buzz_on = 1; buzz_t0 = now;
    }
}

/* =====================================================================
 *  위험 등급 판정 (미세먼지 기준)
 * ===================================================================== */
static uint8_t level_of(float v, const float th[3], float scale)
{
    v *= scale;
    return (v < th[0]) ? 0 : (v < th[1]) ? 1 : (v < th[2]) ? 2 : 3;
}

static uint8_t risk_raw(float scale)
{
    return level_of((float)dust_ug, TH_DUST, scale);
}

static void Risk_1s(void)
{
    static uint32_t below_since = 0;
    static uint8_t  prev_level = 0;

    if (!warm_done) { risk_level = 0; prev_level = 0; return; }

    uint8_t up = risk_raw(1.0f);
    if (up >= risk_level) { risk_level = up; below_since = 0; }
    else
    {
        uint8_t down = risk_raw(1.25f);            /* 기준의 80% 아래인지 */
        if (down < risk_level)
        {
            if (below_since == 0) below_since = HAL_GetTick();
            if ((HAL_GetTick() - below_since) >= LEVEL_DOWN_HOLD_MS)
            {
                risk_level = down;                 /* 한 단계씩이 아니라 현재 수준으로 바로 */
                below_since = 0;
            }
        }
        else below_since = 0;
    }

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

/* 환풍기 PWM % : 미세먼지 수치에 비례 -> 수치가 내려가면 PWM 도 내려감 */
static uint8_t Fan_Pct(void)
{
    if (!warm_done) return 0;
    float idx = gnorm((float)dust_ug, TH_DUST);
    if (idx < FAN_START_IDX) return 0;
    float p = FAN_MIN_PCT + (idx - FAN_START_IDX) * (100.0f - FAN_MIN_PCT) / (100.0f - FAN_START_IDX);
    if (p > 100.0f) p = 100.0f;
    return (uint8_t)p;
}

/* =====================================================================
 *  CAN 마스터 (FDCAN1, PA11 RX / PA12 TX, 500kbps)
 * ===================================================================== */
typedef struct { uint8_t seen; uint32_t last_ms; uint8_t st, pos, tgt, fault, enc; uint16_t ma; } Node_t;
static Node_t   nd[3];                 /* 0=창문1(0x101) 1=창문2(0x102) 2=환풍기(0x201) */
static uint8_t  cmd_win = 0, cmd_fan = 0, cmd_seq = 0;
static uint32_t can_tx_cnt = 0, can_rx_cnt = 0, can_busoff = 0;

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
        int k = (h.Identifier == CAN_ID_WIN1) ? 0 : (h.Identifier == CAN_ID_WIN2) ? 1 : (h.Identifier == CAN_ID_FAN) ? 2 : -1;
        if (k < 0) continue;
        Node_t *n = &nd[k];
        n->seen = 1; n->last_ms = HAL_GetTick(); can_rx_cnt++;
        if (k < 2) { n->st = d[0]; n->pos = d[1]; n->tgt = d[2]; n->fault = d[3]; n->enc = d[4]; n->ma = (uint16_t)((d[5] << 8) | d[6]); }
        else       { n->pos = d[0]; n->tgt = d[1]; }
    }
    /* 버스 오프 자동 복구 */
    if (hfdcan1.Instance->PSR & FDCAN_PSR_BO)
    {
        can_busoff++;
        CLEAR_BIT(hfdcan1.Instance->CCCR, FDCAN_CCCR_INIT);
    }
}

static void CAN_Tx100ms(void)
{
    cmd_win = WIN_PCT_BY_LEVEL[risk_level > 3 ? 3 : risk_level];
    cmd_fan = Fan_Pct();
    uint8_t d[8] = { risk_level, cmd_win, cmd_fan, cmd_seq++, (uint8_t)(warm_done ? 1 : 0), 0, 0, 0 };
    CAN_Send(CAN_ID_CMD, d);
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

static char     tele_buf[700];
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

static void Tele_Send(void)
{
    if (Tele_Busy()) return;
    tele_seq++;

    char tb[12], hb[12];
    fmt1(tb, dht_ok ? temp10 : 0);
    fmt1(hb, dht_ok ? hum10  : 0);

    /* 창문/환풍기 값은 CAN 노드가 보고한 실제 값 (노드가 끊기면 0) */
    int w1 = Node_Alive(0) ? nd[0].pos : 0;
    int w2 = Node_Alive(1) ? nd[1].pos : 0;
    int fn = Node_Alive(2) ? nd[2].pos : 0;

    /* "co" 는 CO 센서를 안 쓰므로 대시보드 호환용으로 0 고정 */
    int n = snprintf(tele_buf, sizeof(tele_buf),
        "{\"seq\":%lu,\"co\":0,\"co2\":0,\"tvoc\":0,\"dust\":%ld,\"temp\":%s,\"hum\":%s,"
        "\"level\":%d,\"win1\":%d,\"win2\":%d,\"fan\":%d,\"cur1\":%u.%02u,\"cur2\":%u.%02u,"
        "\"cmd_win\":%u,\"cmd_fan\":%u,"
        "\"nodes\":[0,%ld,%ld,%ld],\"st1\":%u,\"st2\":%u,\"flt1\":%u,\"flt2\":%u,\"load1\":%u,\"load2\":%u,"
        "\"can_tx\":%lu,\"can_rx\":%lu,\"busoff\":%lu,"
        "\"state\":%d,\"rpm\":%d,\"warm\":%d,\"dht\":%d,"
        "\"dust_mv\":%ld,\"dust_base\":%ld}\n",
        (unsigned long)tele_seq, (long)dust_ug, tb, hb,
        (int)risk_level, w1, w2, fn,
        nd[0].ma / 1000U, (nd[0].ma % 1000U) / 10U, nd[1].ma / 1000U, (nd[1].ma % 1000U) / 10U,
        cmd_win, cmd_fan,
        Node_Age(0), Node_Age(1), Node_Age(2), nd[0].st, nd[1].st, nd[0].fault, nd[1].fault, nd[0].enc, nd[1].enc,
        (unsigned long)can_tx_cnt, (unsigned long)can_rx_cnt, (unsigned long)can_busoff,
        (int)state, (int)(cur_rpm * (float)dir), warm_done ? 0 : 1, (int)dht_err,
        (long)dust_mv, (long)dust_base_mv);

    if (n < 0) n = 0;
    if (n >= (int)sizeof(tele_buf)) n = sizeof(tele_buf) - 1;
    tele_len = (uint16_t)n; tele_pos1 = 0; tele_pos2 = 0;
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

    last_cyc = DWT->CYCCNT;
    uint32_t last_ms = HAL_GetTick();
    uint32_t t_dust = 0, t_1s = 0, t_dht = 1500, t_can = 0;

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
}
#endif
