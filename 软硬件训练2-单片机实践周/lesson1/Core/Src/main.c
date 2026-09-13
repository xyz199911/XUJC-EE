/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : 基于 STM32F103 的温度控制器主程序
  * @note           : 三界面状态机（开机/测温/关机），DS18B20 测温 -> 分级驱动
  *                   风扇与红灯 -> OLED 显示，并通过 USART1 中断接收指令、
  *                   轮询上报数据。指令兼容单字节 s/m/c 与字符串 start/measure/close。
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under BSD 3-Clause license,
  * the "License"; You may not use this file except in compliance with the
  * License. You may obtain a copy of the License at:
  *                        opensource.org/licenses/BSD-3-Clause
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "oled.h"
#include "ds18b20.h"
#include "stdio.h"
#include "string.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* 显示界面（工作模式） */
typedef enum
{
    MODE_BOOT = 0,      /* 开机界面：个人信息 */
    MODE_MEASURE,       /* 测温界面           */
    MODE_OFF            /* 关机界面           */
} DisplayMode;

/* 风扇档位 */
typedef enum
{
    FAN_STOP = 0,       /* 停止 */
    FAN_LOW,            /* 低速 */
    FAN_FULL            /* 全速 */
} FanLevel;

/* 红灯工作方式 */
typedef enum
{
    LED_OFF = 0,        /* 熄灭 */
    LED_ON,             /* 常亮 */
    LED_BLINK           /* 闪烁 */
} LedMode;

/* 热状态：与 OLED 文案、串口上报一一对应 */
typedef enum
{
    ST_NORMAL = 0,      /* 正常：t < 28℃            */
    ST_WARN,            /* 异常：28℃ <= t < 30℃    */
    ST_ALARM,           /* 报警：t >= 30℃           */
    ST_FAULT            /* 故障：温度传感器离线      */
} ThermalStatus;

/* 一次温度判决的结果。全机仅此一处做阈值判断，其结果同时驱动风扇、红灯、
   OLED 与串口，避免同一套阈值散落在多处导致改一漏万。 */
typedef struct
{
    FanLevel      fan;      /* 风扇档位 */
    LedMode       led;      /* 红灯方式 */
    ThermalStatus status;   /* 热状态   */
} ThermalAction;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/*------------------- 个人信息（开机界面，改这里即可） -------------------*/
#define STUDENT_ID        "EIE24010"
#define STUDENT_GRADE     "2024"

/*------------------- 温控阈值（℃，严格对应大作业要求） -------------------*/
#define TEMP_TH_LOW       28.0f   /* 低于该值：风扇停、灯灭、正常           */
#define TEMP_TH_HIGH      30.0f   /* 达到该值：风扇全速、灯闪、报警         */
                                  /* 两值之间：风扇低速、红灯常亮、异常     */

/*------------------- 时序参数（ms） -------------------*/
#define MEASURE_PERIOD_MS    1000U  /* 测温周期；DS18B20 12位转换约750ms    */
#define LED_BLINK_HALF_MS    500U   /* 报警红灯闪烁半周期                   */
#define SENSOR_RETRY_MS      2000U  /* 传感器离线后重新探测周期（支持热插拔）*/
#define DS18B20_INIT_RETRY   10U    /* 上电初始化最大重试次数，防止无传感器卡死 */
#define UART_TX_TIMEOUT_MS  100U   /* 串口轮询发送超时                     */

/*------------------- 风扇 PWM（TIM3_CH2，1kHz，ARR=999） -------------------
 * 本工程配套风扇驱动板为“低电平有效”：引脚低电平占比越大，风扇越快。
 * 而 TIM3 配置为 PWM1、高电平有效，因此 CCR 越大、引脚高电平越久、风扇越慢：
 *   停止 -> CCR=ARR（引脚常高）；全速 -> CCR=0（引脚常低）。
 * 若日后更换为高电平有效驱动板，只需把下面三个值改为 0/200/999，业务逻辑无需改动。
 */
#define FAN_CCR_STOP      999U
#define FAN_CCR_LOW       200U
#define FAN_CCR_FULL      0U

/*------------------- 16x16 汉字字模索引（严格对应 oledfont.h，勿错位） ----*/
#define FONT_WEN          0   /* 温 */
#define FONT_DU           1   /* 度 */
#define FONT_YI           2   /* 异 */
#define FONT_CHANG        3   /* 常 */
#define FONT_ZHENG        4   /* 正 */
#define FONT_XUE          6   /* 学 */
#define FONT_HAO          7   /* 号 */
#define FONT_XING         8   /* 姓 */
#define FONT_MING         9   /* 名 */
#define FONT_NIAN         10  /* 年 */
#define FONT_JI           11  /* 级 */
#define FONT_ZHUAN        12  /* 专 */
#define FONT_YE           13  /* 业 */
#define FONT_NAME_A       14  /* 姓名第 1 字（字模见 oledfont.h） */
#define FONT_NAME_B       15  /* 姓名第 2 字（字模见 oledfont.h） */
#define FONT_DIAN         16  /* 电 */
#define FONT_ZI           17  /* 子 */
#define FONT_FENG         20  /* 风 */
#define FONT_SU           21  /* 速 */
#define FONT_KONG         23  /* 控 */
#define FONT_BAO          24  /* 报 */
#define FONT_JING         25  /* 警 */
#define FONT_QI           26  /* 器 */
#define FONT_GUAN         27  /* 关 */
#define FONT_JI_M         28  /* 机 */

/* 串口字符串指令行缓冲长度 */
#define CMD_BUF_SIZE      16U
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
static uint8_t  s_rx_byte;                         /* 串口单字节接收缓冲       */
static volatile DisplayMode g_req_mode = MODE_BOOT;/* 串口请求的模式（ISR 写入）*/
static DisplayMode g_mode = MODE_BOOT;             /* 当前生效模式（主循环消费）*/

static char     s_cmd_buf[CMD_BUF_SIZE];           /* 字符串指令行缓冲         */
static uint8_t  s_cmd_len;                         /* 行缓冲已收字节数         */
static volatile uint8_t s_cmd_line_ready;          /* 收到完整一行标志         */

static uint8_t       s_sensor_ok = 0U;              /* DS18B20 是否在线         */
static float         s_temperature = 0.0f;          /* 最近一次温度（℃）       */
static ThermalAction s_action = { FAN_STOP, LED_OFF, ST_FAULT }; /* 当前执行动作 */
static ThermalStatus s_last_drawn_status = (ThermalStatus)0xFF;  /* 上次已绘制状态 */

static uint8_t  s_led_lit = 0U;                    /* 闪烁红灯当前是否点亮     */
static uint32_t s_last_blink_tick = 0U;             /* 上次翻转时刻             */
static uint32_t s_last_measure_tick = 0U;           /* 上次测温时刻             */
static uint32_t s_last_probe_tick = 0U;             /* 上次传感器探测时刻       */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
static void Buzzer_Off(void);
static void RedLed_Write(uint8_t on);
static void RedLed_Task(LedMode mode, uint32_t now_ms);
static void Fan_SetLevel(FanLevel level);
static void Actuator_Idle(void);
static ThermalAction Thermal_Decide(float temperature, uint8_t sensor_ok);
static void Format_Temperature(char *out, float temperature);
static void UI_DrawBoot(void);
static void UI_DrawOff(void);
static void UI_DrawMeasureFrame(void);
static void UI_UpdateMeasure(float temperature, FanLevel level, ThermalStatus status);
static void Uart_SendString(const char *str);
static void Uart_Report(float temperature, FanLevel level, ThermalStatus status);
static void Command_HandleLine(char *line);
static void Task_Measure(uint32_t now_ms);
static void App_SwitchMode(DisplayMode target);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/*============================================================================
 *                          执行器底层（风扇 / 灯 / 蜂鸣器）
 *============================================================================*/

/* 关闭蜂鸣器：PE5 高电平为不响 */
static void Buzzer_Off(void)
{
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_5, GPIO_PIN_SET);
}

/* 置位红灯：PB5 低电平点亮，on=1 亮、on=0 灭 */
static void RedLed_Write(uint8_t on)
{
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, on ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

/* 设置风扇档位，集中完成档位 -> CCR 的映射 */
static void Fan_SetLevel(FanLevel level)
{
    uint32_t ccr = FAN_CCR_STOP;

    switch (level)
    {
        case FAN_LOW:  ccr = FAN_CCR_LOW;  break;
        case FAN_FULL: ccr = FAN_CCR_FULL; break;
        case FAN_STOP:
        default:       ccr = FAN_CCR_STOP; break;
    }
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, ccr);
}

/* 非测量界面的安全输出：风扇停、红灯灭、蜂鸣器关 */
static void Actuator_Idle(void)
{
    Fan_SetLevel(FAN_STOP);
    RedLed_Write(0U);
    Buzzer_Off();
    s_led_lit = 0U;
}

/* 红灯时基任务，每轮主循环调用，全程非阻塞。
   OFF 灭 / ON 常亮 / BLINK 以 LED_BLINK_HALF_MS 为半周期翻转。 */
static void RedLed_Task(LedMode mode, uint32_t now_ms)
{
    switch (mode)
    {
        case LED_ON:
            RedLed_Write(1U);
            s_led_lit = 1U;
            break;

        case LED_BLINK:
            if ((uint32_t)(now_ms - s_last_blink_tick) >= LED_BLINK_HALF_MS)
            {
                s_last_blink_tick = now_ms;
                s_led_lit = (uint8_t)!s_led_lit;
                RedLed_Write(s_led_lit);
            }
            break;

        case LED_OFF:
        default:
            RedLed_Write(0U);
            s_led_lit = 0U;
            break;
    }
}

/*============================================================================
 *                          温度判决（唯一的阈值逻辑）
 *============================================================================*/
static ThermalAction Thermal_Decide(float temperature, uint8_t sensor_ok)
{
    ThermalAction action;

    if (sensor_ok == 0U)
    {
        /* 传感器离线：执行器进入安全态，上报故障 */
        action.fan = FAN_STOP;
        action.led = LED_OFF;
        action.status = ST_FAULT;
        return action;
    }

    if (temperature < TEMP_TH_LOW)
    {
        action.fan = FAN_STOP;   /* <28℃：风扇停、灯灭 */
        action.led = LED_OFF;
        action.status = ST_NORMAL;
    }
    else if (temperature < TEMP_TH_HIGH)
    {
        action.fan = FAN_LOW;    /* 28~30℃：风扇低速、红灯常亮 */
        action.led = LED_ON;
        action.status = ST_WARN;
    }
    else
    {
        action.fan = FAN_FULL;   /* >=30℃：风扇全速、红灯闪烁 */
        action.led = LED_BLINK;
        action.status = ST_ALARM;
    }
    return action;
}

/*============================================================================
 *                          OLED 界面
 *============================================================================*/

/* 温度 -> "xx.x C"。全程整数运算取一位小数并四舍五入，
   不使用 sprintf 的 %f 浮点格式化（避免依赖 microlib 浮点库、节省 Flash）。 */
static void Format_Temperature(char *out, float temperature)
{
    int tenths;     /* 十分之一℃为单位的整数 */
    int whole;      /* 整数部分 */
    int frac;       /* 小数部分（一位） */

    if (temperature >= 0.0f)
    {
        tenths = (int)(temperature * 10.0f + 0.5f);
    }
    else
    {
        tenths = (int)(-temperature * 10.0f + 0.5f);
    }
    whole = tenths / 10;
    frac  = tenths % 10;

    if (temperature < 0.0f)
    {
        sprintf(out, "-%d.%d C", whole, frac);
    }
    else
    {
        sprintf(out, "%d.%d C", whole, frac);
    }
}

/* 开机界面：学号 / 姓名 / 专业 / 年级（坐标沿用已验证布局） */
static void UI_DrawBoot(void)
{
    (void)OLED_Clear_Screen();

    /* 学号 */
    (void)OLED_Display_Char_16X16(0, 0, FONT_XUE);
    (void)OLED_Display_Char_16X16(0, 16, FONT_HAO);
    (void)OLED_Display_String_8X16(0, 32, (uint8_t*)":");
    (void)OLED_Display_String_8X16(0, 40, (uint8_t*)STUDENT_ID);

    /* 姓名 */
    (void)OLED_Display_Char_16X16(2, 0, FONT_XING);
    (void)OLED_Display_Char_16X16(2, 16, FONT_MING);
    (void)OLED_Display_String_8X16(2, 32, (uint8_t*)":");
    (void)OLED_Display_Char_16X16(2, 40, FONT_NAME_A);
    (void)OLED_Display_Char_16X16(2, 56, FONT_NAME_B);

    /* 专业 */
    (void)OLED_Display_Char_16X16(4, 0, FONT_ZHUAN);
    (void)OLED_Display_Char_16X16(4, 16, FONT_YE);
    (void)OLED_Display_String_8X16(4, 32, (uint8_t*)":");
    (void)OLED_Display_Char_16X16(4, 40, FONT_DIAN);
    (void)OLED_Display_Char_16X16(4, 56, FONT_ZI);

    /* 年级 */
    (void)OLED_Display_Char_16X16(6, 0, FONT_NIAN);
    (void)OLED_Display_Char_16X16(6, 16, FONT_JI);
    (void)OLED_Display_String_8X16(6, 32, (uint8_t*)":");
    (void)OLED_Display_String_8X16(6, 40, (uint8_t*)STUDENT_GRADE);
}

/* 关机界面 */
static void UI_DrawOff(void)
{
    (void)OLED_Clear_Screen();
    (void)OLED_Display_Char_16X16(0, 0, FONT_GUAN);
    (void)OLED_Display_Char_16X16(0, 16, FONT_JI_M);
}

/* 测温界面静态框架：进入时只绘制一次（标题 + 各字段标签），
   之后仅局部刷新数值，避免周期性整屏清屏造成的闪烁。 */
static void UI_DrawMeasureFrame(void)
{
    (void)OLED_Clear_Screen();

    /* 标题：温控器 */
    (void)OLED_Display_Char_16X16(0, 0, FONT_WEN);
    (void)OLED_Display_Char_16X16(0, 16, FONT_KONG);
    (void)OLED_Display_Char_16X16(0, 32, FONT_QI);

    /* 温度： */
    (void)OLED_Display_Char_16X16(2, 0, FONT_WEN);
    (void)OLED_Display_Char_16X16(2, 16, FONT_DU);
    (void)OLED_Display_String_8X16(2, 32, (uint8_t*)":");

    /* 风速： */
    (void)OLED_Display_Char_16X16(4, 0, FONT_FENG);
    (void)OLED_Display_Char_16X16(4, 16, FONT_SU);
    (void)OLED_Display_String_8X16(4, 32, (uint8_t*)":");
}

/* 擦除一行 8x16 文本从 col 起到行尾（上下两页），用于数值局部无残影刷新 */
static void UI_EraseText(uint8_t page, uint8_t col)
{
    static const uint8_t s_zeros[128] = {0};
    uint8_t width = (uint8_t)(128U - col);

    (void)OLED_Set_Position(page, col);
    (void)OLED_Write_Data((uint8_t*)s_zeros, width);
    (void)OLED_Set_Position((uint8_t)(page + 1U), col);
    (void)OLED_Write_Data((uint8_t*)s_zeros, width);
}

/* 测温界面动态刷新：温度、风速档每帧覆盖；状态仅在变化时重绘（消除残影） */
static void UI_UpdateMeasure(float temperature, FanLevel level, ThermalStatus status)
{
    char text[20];

    /* 温度数值 */
    UI_EraseText(2, 40);
    if (status == ST_FAULT)
    {
        (void)OLED_Display_String_8X16(2, 40, (uint8_t*)"--.- C");
    }
    else
    {
        Format_Temperature(text, temperature);
        (void)OLED_Display_String_8X16(2, 40, (uint8_t*)text);
    }

    /* 风速档位 0/1/2 */
    UI_EraseText(4, 40);
    if (status == ST_FAULT)
    {
        (void)OLED_Display_String_8X16(4, 40, (uint8_t*)"-");
    }
    else
    {
        text[0] = (char)('0' + (uint8_t)level);
        text[1] = '\0';
        (void)OLED_Display_String_8X16(4, 40, (uint8_t*)text);
    }

    /* 状态：正常 / 异常 / 报警 / ERR，仅在状态跳变时重写一次 */
    if (status != s_last_drawn_status)
    {
        static const uint8_t s_blank[32] = {0};
        s_last_drawn_status = status;

        (void)OLED_Set_Position(6, 0);
        (void)OLED_Write_Data((uint8_t*)s_blank, 32);
        (void)OLED_Set_Position(7, 0);
        (void)OLED_Write_Data((uint8_t*)s_blank, 32);

        switch (status)
        {
            case ST_NORMAL:
                (void)OLED_Display_Char_16X16(6, 0, FONT_ZHENG);
                (void)OLED_Display_Char_16X16(6, 16, FONT_CHANG);
                break;
            case ST_WARN:
                (void)OLED_Display_Char_16X16(6, 0, FONT_YI);
                (void)OLED_Display_Char_16X16(6, 16, FONT_CHANG);
                break;
            case ST_ALARM:
                (void)OLED_Display_Char_16X16(6, 0, FONT_BAO);
                (void)OLED_Display_Char_16X16(6, 16, FONT_JING);
                break;
            case ST_FAULT:
            default:
                /* 字库无“故障”二字，以 ASCII ERR 表示 */
                (void)OLED_Display_String_8X16(6, 0, (uint8_t*)"ERR");
                break;
        }
    }
}

/*============================================================================
 *                          串口通信（中断接收 / 轮询发送）
 *============================================================================*/
static void Uart_SendString(const char *str)
{
    (void)HAL_UART_Transmit(&huart1, (uint8_t*)str, (uint16_t)strlen(str),
                            UART_TX_TIMEOUT_MS);
}

/* 轮询上报一帧测量结果，与 OLED 显示同源，格式便于串口助手直接查看 */
static void Uart_Report(float temperature, FanLevel level, ThermalStatus status)
{
    char line[48];
    char temp_text[16];
    const char *status_text;

    if (status == ST_FAULT)
    {
        Uart_SendString("T:--.-C Fan:- Status:FAULT\r\n");
        return;
    }

    switch (status)
    {
        case ST_NORMAL: status_text = "NORMAL"; break;
        case ST_WARN:   status_text = "WARN";   break;
        case ST_ALARM:  status_text = "ALARM";  break;
        default:        status_text = "UNKNOWN";break;
    }
    Format_Temperature(temp_text, temperature);
    sprintf(line, "T:%s Fan:%d Status:%s\r\n", temp_text, (int)level, status_text);
    Uart_SendString(line);
}

/* 解析一整条字符串指令（不区分大小写） */
static void Command_HandleLine(char *line)
{
    char *p;

    for (p = line; *p != '\0'; p++)
    {
        if ((*p >= 'A') && (*p <= 'Z'))
        {
            *p = (char)(*p + 32);   /* 统一转小写 */
        }
    }

    if (strcmp(line, "start") == 0)
    {
        g_req_mode = MODE_BOOT;
    }
    else if (strcmp(line, "measure") == 0)
    {
        g_req_mode = MODE_MEASURE;
    }
    else if (strcmp(line, "close") == 0)
    {
        g_req_mode = MODE_OFF;
    }
    /* 其余未知指令忽略 */
}

/* 串口接收完成回调（中断上下文）：只做极简的缓冲与置位，不做耗时操作。
   - 单字节 s/m/c 立即生效；
   - start/measure/close 以回车/换行结尾，主循环轮询解析。
   两者结果幂等，混用不会产生副作用。 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    char ch;

    if (huart->Instance != USART1)
    {
        return;
    }
    ch = (char)s_rx_byte;

    /* 单字节快捷指令 */
    if (ch == 's')
    {
        g_req_mode = MODE_BOOT;
    }
    else if (ch == 'm')
    {
        g_req_mode = MODE_MEASURE;
    }
    else if (ch == 'c')
    {
        g_req_mode = MODE_OFF;
    }

    /* 字符串指令行累积 */
    if ((ch == '\r') || (ch == '\n'))
    {
        if (s_cmd_len > 0U)
        {
            s_cmd_buf[s_cmd_len] = '\0';
            s_cmd_line_ready = 1U;
        }
    }
    else if ((ch >= ' ') && (s_cmd_len < (CMD_BUF_SIZE - 1U)))
    {
        s_cmd_buf[s_cmd_len++] = ch;
    }
    else
    {
        /* 超长或非法字符：丢弃，避免缓冲溢出 */
    }

    /* 重新开启下一次单字节中断接收 */
    (void)HAL_UART_Receive_IT(&huart1, &s_rx_byte, 1U);
}

/* 串口异常（溢出/噪声/帧错误）后自动重启接收，保证链路不永久掉线 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        (void)HAL_UART_Receive_IT(&huart1, &s_rx_byte, 1U);
    }
}

/*============================================================================
 *                          模式切换与测温周期任务
 *============================================================================*/

/* 模式切换边沿动作：只在模式真正变化时执行一次，静态界面不重复绘制 */
static void App_SwitchMode(DisplayMode target)
{
    if (target == g_mode)
    {
        return;
    }
    g_mode = target;
    s_last_drawn_status = (ThermalStatus)0xFF;   /* 强制状态行重绘 */

    switch (g_mode)
    {
        case MODE_BOOT:
            Actuator_Idle();
            UI_DrawBoot();
            Uart_SendString("[MODE] BOOT (info)\r\n");
            break;

        case MODE_OFF:
            Actuator_Idle();
            UI_DrawOff();
            Uart_SendString("[MODE] OFF\r\n");
            break;

        case MODE_MEASURE:
            UI_DrawMeasureFrame();
            s_last_measure_tick = 0U;            /* 进入后立即测第一次 */
            Uart_SendString("[MODE] MEASURE\r\n");
            break;

        default:
            break;
    }
}

/* 测温周期任务：传感器容错 -> 读温 -> 判决 -> 驱动执行器与界面 -> 轮询上报 */
static void Task_Measure(uint32_t now_ms)
{
    /* 离线时周期性重探，恢复后自动回到正常测量，支持热插拔 */
    if (s_sensor_ok == 0U)
    {
        if ((uint32_t)(now_ms - s_last_probe_tick) >= SENSOR_RETRY_MS)
        {
            s_last_probe_tick = now_ms;
            if (DS18B20_Init() == 0U)
            {
                s_sensor_ok = 1U;
                Uart_SendString("[SENSOR] DS18B20 online\r\n");
            }
        }
    }

    if ((uint32_t)(now_ms - s_last_measure_tick) >= MEASURE_PERIOD_MS)
    {
        s_last_measure_tick = now_ms;

        if (s_sensor_ok != 0U)
        {
            s_temperature = DS18B20_GetTemp_SkipRom();
        }
        s_action = Thermal_Decide(s_temperature, s_sensor_ok);
        Fan_SetLevel(s_action.fan);
        UI_UpdateMeasure(s_temperature, s_action.fan, s_action.status);
        Uart_Report(s_temperature, s_action.fan, s_action.status);
    }
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */
  uint8_t init_retry;
  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_TIM1_Init();
  MX_USART1_UART_Init();
  MX_TIM3_Init();
  /* USER CODE BEGIN 2 */
  /* 1. 第一时间把蜂鸣器、红灯置于安全态，避免上电初始化过程中误鸣误亮 */
  Buzzer_Off();
  RedLed_Write(0U);

  /* 2. 初始化 OLED */
  (void)OLED_Init();

  /* 3. DS18B20 初始化：有限次重试，失败不卡死，转由运行期容错（界面提示 ERR） */
  s_sensor_ok = 0U;
  for (init_retry = 0U; init_retry < DS18B20_INIT_RETRY; init_retry++)
  {
      if (DS18B20_Init() == 0U)
      {
          s_sensor_ok = 1U;
          break;
      }
      HAL_Delay(200);
  }

  /* 4. 启动风扇 PWM，默认停止 */
  (void)HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_2);
  Fan_SetLevel(FAN_STOP);

  /* 5. 开启串口中断接收（s/m/c 或 start/measure/close） */
  (void)HAL_UART_Receive_IT(&huart1, &s_rx_byte, 1U);

  /* 6. 进入开机界面 */
  g_mode = MODE_BOOT;
  g_req_mode = MODE_BOOT;
  UI_DrawBoot();
  Actuator_Idle();
  Uart_SendString("\r\nSTM32 Thermal Controller Ready.\r\n");
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
      uint32_t now = HAL_GetTick();

      /* (1) 串口字符串指令：中断负责接收，此处轮询解析 */
      if (s_cmd_line_ready != 0U)
      {
          Command_HandleLine(s_cmd_buf);
          s_cmd_len = 0U;
          s_cmd_line_ready = 0U;
      }

      /* (2) 模式切换：边沿动作只执行一次，静态界面不再周期重绘 */
      App_SwitchMode(g_req_mode);

      /* (3) 测温模式周期任务 */
      if (g_mode == MODE_MEASURE)
      {
          Task_Measure(now);
      }

      /* (4) 红灯时基任务（非阻塞；非测温模式恒灭） */
      RedLed_Task((g_mode == MODE_MEASURE) ? s_action.led : LED_OFF, now);
  /* USER CODE END WHILE */

  /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line number source line number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
