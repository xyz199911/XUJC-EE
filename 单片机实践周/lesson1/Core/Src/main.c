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
#define PWM_STOP   999
#define PWM_LOW    200   // 约20%占空比
#define PWM_FULL   0   // 100%

// 红灯闪烁间隔(ms)
#define BLINK_INTERVAL 500
// 汉字字模索引（根据 oledfont.h 中顺序定义）
#define CHAR_WEN    0   // 温
#define CHAR_DU     1   // 度
#define CHAR_YI     2   // 异
#define CHAR_CHANG  3   // 常
#define CHAR_ZHENG  4   // 正
#define CHAR_XUE   6   // "学"
#define CHAR_HAO   7   // "号"
#define CHAR_XING  8   // "姓"
#define CHAR_MING  9   // "名"
#define CHAR_NIAN  10  // "年"
#define CHAR_JI    11  // "级"
#define CHAR_ZHUAN 12  // "专"
#define CHAR_YE    13  // "业"
#define CHAR_1 14  // "填你的名字"
#define CHAR_2   15  // "你的名字"记得看oledfont.h
#define CHAR_DIAN  16  // "电"
#define CHAR_ZI    17  // "子"
#define CHAR_FENG   20  // 风
#define CHAR_SU     21  // 速
#define CHAR_KONG   23  // 控
#define CHAR_BAO    24  // 报
#define CHAR_JING   25  // 警
#define CHAR_QI     26  // 器
#define CHAR_GUAN   27  // 关
#define CHAR_JII     28  // 机
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
float temp = 0.0f;
DisplayMode displayMode = MODE_BOOT;   // 默认开机界面
uint8_t uart_rx_data;                 // 串口接收单字节
uint32_t last_blink_time = 0;         // 用于红灯闪烁
uint8_t led_state = 0;                // 红灯当前状态（0灭，1亮），用于闪烁
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        switch (uart_rx_data)
        {
            case 's':  // 开机界面
                displayMode = MODE_BOOT;
                break;
            case 'm':  // 测温模式
                displayMode = MODE_MEASURE;
                break;
            case 'c':  // 关机
                displayMode = MODE_OFF;
                break;
            default:
                break;
        }
        // 重新开启单次接收
        HAL_UART_Receive_IT(&huart1, &uart_rx_data, 1);
    }
}
/**
  * @brief 显示开机界面（个人信息）
  */
void ShowBootScreen(void)
{
    OLED_Clear_Screen();
    // 第一行：学号：EIE24010
    OLED_Display_Char_16X16(0, 0, CHAR_XUE);
    OLED_Display_Char_16X16(0, 16, CHAR_HAO);
    OLED_Display_String_8X16(0, 32, (uint8_t*)":");
    OLED_Display_String_8X16(0, 40, (uint8_t*)"EIE24010");
	OLED_Display_Char_16X16(2, 0, CHAR_XING);
    OLED_Display_Char_16X16(2, 16, CHAR_MING);
    OLED_Display_String_8X16(2, 32, (uint8_t*)":");
    OLED_Display_Char_16X16(2, 40, CHAR_1);
    OLED_Display_Char_16X16(2, 56, CHAR_2);
	 OLED_Display_Char_16X16(4, 0, CHAR_ZHUAN);
    OLED_Display_Char_16X16(4, 16, CHAR_YE);
    OLED_Display_String_8X16(4, 32, (uint8_t*)":");
    OLED_Display_Char_16X16(4, 40, CHAR_DIAN);
    OLED_Display_Char_16X16(4, 56, CHAR_ZI);
	OLED_Display_Char_16X16(6, 0, CHAR_NIAN);
    OLED_Display_Char_16X16(6, 16, CHAR_JI);
    OLED_Display_String_8X16(6, 32, (uint8_t*)":");
    OLED_Display_String_8X16(6, 40, (uint8_t*)"2024");
	
	
    // 关闭风扇和灯
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, PWM_STOP);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_SET);   // 红灯灭（高电平）
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_5, GPIO_PIN_SET);   // 另一个灯灭（若有）
}

/**
  * @brief 测温模式主逻辑（读取温度、控制风扇和红灯、刷新OLED）
  */
void MeasureMode(void)
{
    // 1. 读取温度
    temp = DS18B20_GetTemp_SkipRom();

    // 2. 根据温度控制风扇和红灯
    if (temp < 28.0f)
    {
        // 风扇停，红灯灭
        __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, PWM_STOP);
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_SET);
        // 复位闪烁状态
        led_state = 0;
    }
    else if (temp >= 28.0f && temp < 30.0f)
    {
        // 风扇低速，红灯常亮
        __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, PWM_LOW);
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_RESET);
        // 复位闪烁状态
        led_state = 0;
    }
		 else // temp >= 30.0f
    {
        // 风扇全速，红灯闪烁
        __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, PWM_FULL);
        uint32_t now = HAL_GetTick();
        if (now - last_blink_time >= BLINK_INTERVAL)
        {
            last_blink_time = now;
            led_state = !led_state;                   // 翻转状态
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, led_state ? GPIO_PIN_RESET : GPIO_PIN_SET);
        }
    }
		 // 3. 更新OLED显示（每次清屏重绘，避免残影）
    OLED_Clear_Screen();

    // 标题
  OLED_Display_Char_16X16(0, 0, CHAR_WEN);
    OLED_Display_Char_16X16(0, 16, CHAR_KONG);
    OLED_Display_Char_16X16(0, 32, CHAR_QI);


    // 温度
   OLED_Display_Char_16X16(2, 0, CHAR_WEN);
    OLED_Display_Char_16X16(2, 16, CHAR_DU);
    OLED_Display_String_8X16(2, 32, (uint8_t*)":");
    char temp_str[20];
    sprintf(temp_str, "%.1f C", temp);
    OLED_Display_String_8X16(2, 40, (uint8_t*)temp_str);


    // 风速
		OLED_Display_Char_16X16(4, 0, CHAR_FENG);
    OLED_Display_Char_16X16(4, 16, CHAR_SU);
    OLED_Display_String_8X16(4, 32, (uint8_t*)":");
    uint16_t duty = __HAL_TIM_GET_COMPARE(&htim3, TIM_CHANNEL_2);
    char speed_str[20];
    if (duty == PWM_STOP)
        strcpy(speed_str, "Speed: 0");
    else if (duty == PWM_LOW)
        strcpy(speed_str, "Speed: 1");
    else
        strcpy(speed_str, "Speed: 2");
    OLED_Display_String_8X16(4, 0, (uint8_t*)speed_str);
 // 状态
    char status_str[20];
    if (temp < 28.0f)
		{
			OLED_Display_Char_16X16(6, 0, CHAR_ZHENG);
        OLED_Display_Char_16X16(6, 16, CHAR_CHANG);
		}
        
    else if (temp < 30.0f)
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
  * @brief 关机模式
  */
void OffMode(void)
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
SystemClock_Config();
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

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
  // 初始化DS18B20（等待成功）
    while (DS18B20_Init())
    {
        // 若初始化失败，可加入错误提示（此处简单等待）
        HAL_Delay(500);
    }
 // 启动PWM（默认占空比为0）
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_2);
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, PWM_STOP);

    // 开启串口接收中断
    HAL_UART_Receive_IT(&huart1, &uart_rx_data, 1);

    // 初始显示开机界面
    ShowBootScreen();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
   while (1)
    {
        switch (displayMode)
        {
            case MODE_BOOT:
                ShowBootScreen();
                // 开机界面无需频繁刷新，可加延时减少CPU占用
                HAL_Delay(500);
                break;
						case MODE_MEASURE:
                MeasureMode();
                HAL_Delay(200);   // 200ms刷新一次
                break;
            case MODE_OFF:
                OffMode();
                HAL_Delay(500);
                break;
            default:
                break;
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
  * @param  line: assert_param error line source number
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
