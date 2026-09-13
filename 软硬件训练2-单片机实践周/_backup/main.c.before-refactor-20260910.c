/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
typedef enum {
    MODE_OFF = 0,
    MODE_BOOT,
    MODE_MEASURE
} DisplayMode;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* ===== PWM 风扇控制（TIM3: Prescaler=71, Period=999 → 1kHz；CCR 越小占空比越大） ===== */
#define PWM_STOP   999   /* CCR=ARR → 0%：风扇停止 */
#define PWM_LOW    200   /* 约 20% 占空比（低速） */
#define PWM_FULL   0     /* 100%（全速） */

/* 红灯闪烁间隔(ms) */
#define BLINK_INTERVAL 500
/* 界面刷新周期(ms)：数值越大刷新越慢、CPU 占用越低 */
#define STATIC_REFRESH_MS   500   /* 开机画面 / 关机界面 */
#define MEASURE_REFRESH_MS  200   /* 测量界面（实际节奏受 DS18B20 转换延时限制，见下） */
/* DS18B20 初始化最大重试次数（防止未接传感器时程序卡死） */
#define DS18B20_INIT_MAX_RETRY 10

/* 温控阈值(℃) */
#define TEMP_LOW_THRESHOLD  28.0f   /* 低于该值：风扇停止 */
#define TEMP_HIGH_THRESHOLD 30.0f   /* 达到该值：风扇全速并红灯报警 */

/* 汉字字模索引（按 oledfont.h 顺序定义，勿随意改动） */
#define CHAR_WEN    0   // 温
#define CHAR_DU     1   // 度
#define CHAR_YI     2   // 异
#define CHAR_CHANG  3   // 常
#define CHAR_ZHENG  4   // 正
#define CHAR_XUE    6   // 学
#define CHAR_HAO    7   // 号
#define CHAR_XING   8   // 姓
#define CHAR_MING   9   // 名
#define CHAR_NIAN   10  // 年
#define CHAR_JI     11  // 级
#define CHAR_ZHUAN  12  // 专
#define CHAR_YE     13  // 业
#define CHAR_1      14  // 电子（对应字模，见 oledfont.h）
#define CHAR_2      15  // 信息（对应字模，见 oledfont.h）
#define CHAR_DIAN   16  // 电
#define CHAR_ZI     17  // 子
#define CHAR_FENG   20  // 风
#define CHAR_SU     21  // 速
#define CHAR_KONG   23  // 空
#define CHAR_BAO    24  // 报
#define CHAR_JING   25  // 警
#define CHAR_QI     26  // 气
#define CHAR_GUAN   27  // 关
#define CHAR_JII    28  // 机
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
static float temp = 0.0f;                   /* 当前温度(℃) */
static DisplayMode displayMode = MODE_BOOT; /* 界面模式，默认开机画面 */
static uint8_t uart_rx_data;                /* 串口接收到的字节 */
static uint32_t last_blink_time = 0;        /* 红灯上次翻转时刻(ms) */
static uint8_t led_state = 0;               /* 红灯当前电平（0=灭 1=亮，用于报警闪烁） */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/**
  * @brief 串口接收完成回调：根据接收到的命令切换界面模式
  *        's' → 开机画面；'m' → 测量模式；'c' → 关机
  */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        switch (uart_rx_data)
        {
            case 's':  /* 开机画面 */
                displayMode = MODE_BOOT;
                break;
            case 'm':  /* 测量模式 */
                displayMode = MODE_MEASURE;
                break;
            case 'c':  /* 关机 */
                displayMode = MODE_OFF;
                break;
            default:
                break;
        }
        /* 重新开启下一次单字节接收 */
        HAL_UART_Receive_IT(&huart1, &uart_rx_data, 1);
    }
}

/**
  * @brief 风扇与红灯控制（依据温度分级）
  * @param t 当前温度(℃)
  * @return 风速档位：0=停 / 1=低速 / 2=全速（供 OLED 显示使用）
  */
static uint8_t FanControl(float t)
{
    if (t < TEMP_LOW_THRESHOLD)
    {
        /* 低温：风扇停止，红灯熄灭 */
        __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, PWM_STOP);
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_SET);
        led_state = 0;
        return 0;
    }
    else if (t < TEMP_HIGH_THRESHOLD)
    {
        /* 中温：风扇低速，红灯熄灭 */
        __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, PWM_LOW);
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_SET);
        led_state = 0;
        return 1;
    }
    else
    {
        /* 高温：风扇全速，红灯按 BLINK_INTERVAL 间隔闪烁报警 */
        __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, PWM_FULL);
        uint32_t now = HAL_GetTick();
        if (now - last_blink_time >= BLINK_INTERVAL)
        {
            last_blink_time = now;
            led_state = !led_state;
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5,
                              led_state ? GPIO_PIN_RESET : GPIO_PIN_SET);
        }
        return 2;
    }
}

/**
  * @brief 开机画面：显示学号、姓名、专业、届别；关闭风扇与蜂鸣器
  */
static void ShowBootScreen(void)
{
    OLED_Clear_Screen();

    /* 学号 */
    OLED_Display_Char_16X16(0, 0, CHAR_XUE);
    OLED_Display_Char_16X16(0, 16, CHAR_HAO);
    OLED_Display_String_8X16(0, 32, (uint8_t*)":");
    OLED_Display_String_8X16(0, 40, (uint8_t*)"EIE24010");

    /* 姓名 */
    OLED_Display_Char_16X16(2, 0, CHAR_XING);
    OLED_Display_Char_16X16(2, 16, CHAR_MING);
    OLED_Display_String_8X16(2, 32, (uint8_t*)":");
    OLED_Display_Char_16X16(2, 40, CHAR_1);
    OLED_Display_Char_16X16(2, 56, CHAR_2);

    /* 专业 */
    OLED_Display_Char_16X16(4, 0, CHAR_ZHUAN);
    OLED_Display_Char_16X16(4, 16, CHAR_YE);
    OLED_Display_String_8X16(4, 32, (uint8_t*)":");
    OLED_Display_Char_16X16(4, 40, CHAR_DIAN);
    OLED_Display_Char_16X16(4, 56, CHAR_ZI);

    /* 届别 */
    OLED_Display_Char_16X16(6, 0, CHAR_NIAN);
    OLED_Display_Char_16X16(6, 16, CHAR_JI);
    OLED_Display_String_8X16(6, 32, (uint8_t*)":");
    OLED_Display_String_8X16(6, 40, (uint8_t*)"2024");

    /* 关闭风扇、红灯与蜂鸣器（高电平=关闭） */
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, PWM_STOP);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_5, GPIO_PIN_SET);
}

/**
  * @brief 测量界面：显示温度、风速档位、状态
  * @param t     当前温度(℃)
  * @param level 风速档位（0/1/2）
  */
static void ShowMeasureScreen(float t, uint8_t level)
{
    char buf[20];

    OLED_Clear_Screen();

    /* 标题：空气 */
    OLED_Display_Char_16X16(0, 0, CHAR_WEN);
    OLED_Display_Char_16X16(0, 16, CHAR_KONG);
    OLED_Display_Char_16X16(0, 32, CHAR_QI);

    /* 温度：xx.x C（定点化显示，避免 sprintf 浮点格式化占用大量代码空间） */
    OLED_Display_Char_16X16(2, 0, CHAR_WEN);
    OLED_Display_Char_16X16(2, 16, CHAR_DU);
    OLED_Display_String_8X16(2, 32, (uint8_t*)":");
    {
        int16_t t_int = (int16_t)t;
        int16_t t_dec = (int16_t)((t - (float)t_int) * 10.0f);
        if (t_dec < 0) t_dec = -t_dec;
        sprintf(buf, "%d.%d C", (int)t_int, (int)t_dec);
    }
    OLED_Display_String_8X16(2, 40, (uint8_t*)buf);

    /* 风速档位：0 / 1 / 2 */
    OLED_Display_Char_16X16(4, 0, CHAR_FENG);
    OLED_Display_Char_16X16(4, 16, CHAR_SU);
    OLED_Display_String_8X16(4, 32, (uint8_t*)":");
    OLED_Display_String_8X16(4, 40,
                             (uint8_t*)(level == 0 ? "0" :
                                        level == 1 ? "1" : "2"));

    /* 状态：正常 / 异常 / 报警 */
    if (t < TEMP_LOW_THRESHOLD)
    {
        OLED_Display_Char_16X16(6, 0, CHAR_ZHENG);
        OLED_Display_Char_16X16(6, 16, CHAR_CHANG);
    }
    else if (t < TEMP_HIGH_THRESHOLD)
    {
        OLED_Display_Char_16X16(6, 0, CHAR_YI);
        OLED_Display_Char_16X16(6, 16, CHAR_CHANG);
    }
    else
    {
        OLED_Display_Char_16X16(6, 0, CHAR_BAO);
        OLED_Display_Char_16X16(6, 16, CHAR_JING);
    }
}

/**
  * @brief 测量模式：读取温度 → 控制风扇/红灯 → 刷新 OLED
  * @note  DS18B20 温度转换本身需约 750ms（见 ds18b20.c），
  *        因此本函数实际耗时约 750ms，界面刷新节奏受其限制。
  */
static void MeasureMode(void)
{
    temp = DS18B20_GetTemp_SkipRom();
    uint8_t level = FanControl(temp);
    ShowMeasureScreen(temp, level);
}

/**
  * @brief 关机界面：显示"关机"，风扇停止，红灯/蜂鸣器关闭
  */
static void OffMode(void)
{
    OLED_Clear_Screen();
    OLED_Display_Char_16X16(0, 0, CHAR_GUAN);
    OLED_Display_Char_16X16(0, 16, CHAR_JII);
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, PWM_STOP);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_5, GPIO_PIN_SET);
    led_state = 0;
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

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
  OLED_Init();

  /* DS18B20 初始化：带最大重试次数，防止未接传感器时程序卡死 */
  uint8_t ds_retry = 0;
  while (DS18B20_Init() != 0)
  {
      if (++ds_retry >= DS18B20_INIT_MAX_RETRY)
      {
          break;   /* 重试超限：跳过传感器，程序继续运行 */
      }
      HAL_Delay(500);
  }

  /* 启动风扇 PWM 并默认停止 */
  HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_2);
  __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, PWM_STOP);

  /* 开启串口中断接收（s/m/c 命令） */
  HAL_UART_Receive_IT(&huart1, &uart_rx_data, 1);

  /* 显示开机画面 */
  ShowBootScreen();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  uint32_t last_refresh = 0;
  while (1)
  {
      /* 非阻塞节流：按当前模式周期刷新，串口命令可被及时响应 */
      uint32_t now = HAL_GetTick();
      uint32_t interval = (displayMode == MODE_MEASURE) ?
                          MEASURE_REFRESH_MS : STATIC_REFRESH_MS;
      if ((now - last_refresh) >= interval)
      {
          last_refresh = now;
          switch (displayMode)
          {
              case MODE_BOOT:
                  ShowBootScreen();
                  break;
              case MODE_MEASURE:
                  MeasureMode();
                  break;
              case MODE_OFF:
                  OffMode();
                  break;
              default:
                  break;
          }
      }
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
  * @param  line: assert_param error line number source
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
