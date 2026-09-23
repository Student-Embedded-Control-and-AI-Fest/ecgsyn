/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <math.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "ecgsyn_model.h"
#include "ssd1306.h"
#include "ssd1306_fonts.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define ECG_CENTER_MV        (0.4f)
#define ECG_SCALE_X          (1000.0f)
#define VDDA                 (3.3f)
#define DAC_BITS             (12)
#define DAC_MAX              ((1U << DAC_BITS) - 1U)
#define ECG_BLOCK_MAX_SAMPLES 8192


#if defined(USE_TUSTIN)
  #define ECG_METHOD_STR "TUS"
#elif defined(USE_RK4)
  #define ECG_METHOD_STR "RK4"
#else
  #define ECG_METHOD_STR "???"
#endif
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;
DAC_HandleTypeDef hdac1;
I2C_HandleTypeDef hi2c1;
TIM_HandleTypeDef htim6;

/* USER CODE BEGIN PV */
static EcgSynParams params;
static EcgSynContext ecgCtx;

/* two buffers */
static uint16_t *bufA = NULL;
static uint16_t *bufB = NULL;

/* playback side: ISR-owned */
static uint16_t * volatile playBuf = NULL;
static volatile int playLen = 0;
static volatile int playIndex = 0;

/* build side: main-loop-owned */
static uint16_t *buildBuf = NULL;
static int buildLen = 0;

/* flags */
static volatile bool block_wrap_flag = false;
static volatile bool build_request_flag = false;
static volatile bool swap_pending = false;
static volatile bool underrun_flag = false;

/* ECG state continuity */
static float next_x = 1.0f;
static float next_y = 0.0f;
static float next_z = 0.04f;

static uint16_t refDacCode = 0;

static int last_adc_sig = 0;
static int last_adc_ref = 0;
static int last_adc_diff = 0;

static uint32_t build_time_ms = 0;
static uint32_t blocks_built = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_ADC1_Init(void);
static void MX_DAC1_Init(void);
static void MX_I2C1_Init(void);
static void MX_TIM6_Init(void);

/* USER CODE BEGIN PFP */
static uint16_t ecg_mv_to_dac(float ecg_mv);
static bool build_block_into_buffer(uint16_t *dst, int max_len, int *out_len,
                                    float *x_next, float *y_next, float *z_next);
static uint16_t read_adc_channel(uint32_t channel);
static void oled_show_build_info(void);
static void oled_show_error(const char *msg);
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

static uint16_t ecg_mv_to_dac(float ecg_mv)
{
  float volts = (ecg_mv + ECG_CENTER_MV) * ECG_SCALE_X * 0.001f;
  int code = (int)lroundf((volts / VDDA) * (float)DAC_MAX);

  if (code < 0) code = 0;
  if (code > (int)DAC_MAX) code = (int)DAC_MAX;

  return (uint16_t)code;
}

static bool build_block_into_buffer(uint16_t *dst, int max_len, int *out_len,
                                    float *x_next, float *y_next, float *z_next)
{
  float *ecgBlockMv = NULL;
  int len = 0;
  float x_end = 0.0f;
  float y_end = 0.0f;
  float z_end = 0.0f;

  if ((dst == NULL) || (out_len == NULL) || (x_next == NULL) || (y_next == NULL) || (z_next == NULL)) {
    return false;
  }

  params.xinitial = *x_next;
  params.yinitial = *y_next;
  params.zinitial = *z_next;

  if (!build_block_mv(&params, &ecgBlockMv, &len, &ecgCtx, &x_end, &y_end, &z_end)) {
    free_context(&ecgCtx);
    return false;
  }

  if ((len <= 0) || (len > max_len)) {
    if (ecgBlockMv) free(ecgBlockMv);
    free_context(&ecgCtx);
    return false;
  }

  for (int i = 0; i < len; i++) {
      if (!isfinite(ecgBlockMv[i])) {
          if (ecgBlockMv) free(ecgBlockMv);
          free_context(&ecgCtx);
          return false;
      }
      dst[i] = ecg_mv_to_dac(ecgBlockMv[i]);
  }

  *out_len = len;
  *x_next = x_end;
  *y_next = y_end;
  *z_next = z_end;

  free(ecgBlockMv);
  free_context(&ecgCtx);
  return true;
}

static uint16_t read_adc_channel(uint32_t channel)
{
  ADC_ChannelConfTypeDef sConfig = {0};

  sConfig.Channel = channel;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_2CYCLES_5;
  sConfig.SingleDiff = ADC_SINGLE_ENDED;
  sConfig.OffsetNumber = ADC_OFFSET_NONE;
  sConfig.Offset = 0;

  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) {
    Error_Handler();
  }

  if (HAL_ADC_Start(&hadc1) != HAL_OK) {
    Error_Handler();
  }

  if (HAL_ADC_PollForConversion(&hadc1, 10) != HAL_OK) {
    Error_Handler();
  }

  uint16_t val = (uint16_t)HAL_ADC_GetValue(&hadc1);

  if (HAL_ADC_Stop(&hadc1) != HAL_OK) {
    Error_Handler();
  }

  return val;
}

static void oled_show_build_info(void)
{
  char line[32];
  uint32_t sysclk_mhz;
  uint32_t hclk_mhz;
  uint32_t pclk1_mhz;

  SystemCoreClockUpdate();
  sysclk_mhz = SystemCoreClock / 1000000UL;
  hclk_mhz   = HAL_RCC_GetHCLKFreq() / 1000000UL;
  pclk1_mhz  = HAL_RCC_GetPCLK1Freq() / 1000000UL;

  ssd1306_Fill(Black);

  ssd1306_SetCursor(0, 0);
  snprintf(line, sizeof(line), "SYS:%3lu H:%3lu",
           (unsigned long)sysclk_mhz,
           (unsigned long)hclk_mhz);
  ssd1306_WriteString(line, Font_7x10, White);

  ssd1306_SetCursor(0, 16);
  snprintf(line, sizeof(line), "P1:%3lu B:%4lu",
           (unsigned long)pclk1_mhz,
           (unsigned long)build_time_ms);
  ssd1306_WriteString(line, Font_7x10, White);

  ssd1306_SetCursor(0, 32);
  snprintf(line, sizeof(line), "Len:%d %s",
           (int)playLen,
           ECG_METHOD_STR);
  ssd1306_WriteString(line, Font_7x10, White);

  ssd1306_SetCursor(0, 48);
  snprintf(line, sizeof(line), "Blk:%lu",
           (unsigned long)blocks_built);
  ssd1306_WriteString(line, Font_7x10, White);

  ssd1306_UpdateScreen();
}

static void oled_show_error(const char *msg)
{
  ssd1306_Fill(Black);
  ssd1306_SetCursor(0, 0);
  ssd1306_WriteString("ERROR", Font_7x10, White);
  ssd1306_SetCursor(0, 16);
  ssd1306_WriteString((char*)msg, Font_7x10, White);
  ssd1306_UpdateScreen();
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM6)
  {
    if ((playBuf != NULL) && (playLen > 0))
    {
      if (HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_2, DAC_ALIGN_12B_R, playBuf[playIndex]) != HAL_OK) {
        Error_Handler();
      }

      playIndex++;
      if (playIndex >= playLen) {
        playIndex = 0;
        block_wrap_flag = true;

        if (swap_pending) {
          uint16_t *oldPlay = (uint16_t *)playBuf;
          int oldLen = playLen;

          playBuf = buildBuf;
          playLen = buildLen;

          buildBuf = oldPlay;
          buildLen = oldLen;

          swap_pending = false;
          build_request_flag = true;
        } else {
          underrun_flag = true;
        }
      }
    }
  }
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  HAL_Init();
  SystemClock_Config();

  MX_GPIO_Init();
  MX_ADC1_Init();
  MX_DAC1_Init();
  MX_I2C1_Init();
  MX_TIM6_Init();

  /* USER CODE BEGIN 2 */
  HAL_Delay(100);
  ssd1306_Init();

  ssd1306_Fill(Black);
  ssd1306_SetCursor(0, 0);
  ssd1306_WriteString("Building ECG...", Font_7x10, White);
  ssd1306_UpdateScreen();

  ecgsyn_init_default_params(&params);
  ecgsyn_init_context(&ecgCtx);

  params.ecg_fs      = 256;
  params.internal_fs = 256;
  params.n_beats     = 16;
  params.hr_mean     = 60.0f;
  params.hr_std      = 1.0f;
  params.noise_mv    = 0.0f;
  params.seed_init   = 1;

  bufA = (uint16_t*)malloc(ECG_BLOCK_MAX_SAMPLES * sizeof(uint16_t));
  bufB = (uint16_t*)malloc(ECG_BLOCK_MAX_SAMPLES * sizeof(uint16_t));
  if ((bufA == NULL) || (bufB == NULL)) {
    oled_show_error("Buf alloc fail");
    Error_Handler();
  }

  uint32_t t0 = HAL_GetTick();

  int firstLen = 0;

  if (!build_block_into_buffer(bufA, ECG_BLOCK_MAX_SAMPLES, &firstLen,
                               &next_x, &next_y, &next_z)) {
    oled_show_error("Build A fail");
    Error_Handler();
  }
  playLen = firstLen;

  if (!build_block_into_buffer(bufB, ECG_BLOCK_MAX_SAMPLES, &buildLen,
                               &next_x, &next_y, &next_z)) {
    oled_show_error("Build B fail");
    Error_Handler();
  }

  build_time_ms = HAL_GetTick() - t0;
  blocks_built = 2;

  playBuf = bufA;
  buildBuf = bufB;
  playIndex = 0;
  swap_pending = true;
  build_request_flag = false;
  block_wrap_flag = false;
  underrun_flag = false;

  oled_show_build_info();

  if (HAL_DAC_Start(&hdac1, DAC_CHANNEL_1) != HAL_OK) {
    oled_show_error("DAC1 CH1 fail");
    Error_Handler();
  }

  if (HAL_DAC_Start(&hdac1, DAC_CHANNEL_2) != HAL_OK) {
    oled_show_error("DAC1 CH2 fail");
    Error_Handler();
  }

  refDacCode = ecg_mv_to_dac(0.0f);

  if (HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_1, DAC_ALIGN_12B_R, refDacCode) != HAL_OK) {
    oled_show_error("Set DAC ref fail");
    Error_Handler();
  }

  if (HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_2, DAC_ALIGN_12B_R, playBuf[0]) != HAL_OK) {
    oled_show_error("Set DAC sig fail");
    Error_Handler();
  }

  if (HAL_TIM_Base_Start_IT(&htim6) != HAL_OK) {
    oled_show_error("TIM6 start fail");
    Error_Handler();
  }
  /* USER CODE END 2 */

  while (1)
  {
    bool do_wrap = false;
    bool do_build = false;
    bool do_underrun = false;

    __disable_irq();
    do_wrap = block_wrap_flag;
    block_wrap_flag = false;

    do_build = build_request_flag && !swap_pending;
    if (do_build) {
      build_request_flag = false;
    }

    do_underrun = underrun_flag;
    underrun_flag = false;
    __enable_irq();

    if (do_wrap)
    {
      last_adc_sig  = (int)read_adc_channel(ADC_CHANNEL_1);
      last_adc_ref  = (int)read_adc_channel(ADC_CHANNEL_2);
      last_adc_diff = last_adc_sig - last_adc_ref;
    }

    if (do_build)
    {
      if (!build_block_into_buffer(buildBuf, ECG_BLOCK_MAX_SAMPLES, &buildLen,
                                   &next_x, &next_y, &next_z)) {
        oled_show_error("Rebuild fail");
        Error_Handler();
      }

      blocks_built++;

      __disable_irq();
      swap_pending = true;
      __enable_irq();

      oled_show_build_info();
    }

    if (do_underrun)
    {
      oled_show_error("UNDERRUN");
      Error_Handler();
    }
  }
}

/**
  * @brief System Clock Conguration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);
  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSI | RCC_OSCILLATORTYPE_CSI;
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;
  RCC_OscInitStruct.CSIState = RCC_CSI_ON;
  RCC_OscInitStruct.CSICalibrationValue = RCC_CSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLL1_SOURCE_CSI;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 125;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1_VCIRANGE_2;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1_VCORANGE_WIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                              | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2
                              | RCC_CLOCKTYPE_PCLK3;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }

  __HAL_FLASH_SET_PROGRAM_DELAY(FLASH_PROGRAMMING_DELAY_2);
}

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{
  ADC_ChannelConfTypeDef sConfig = {0};

  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_ASYNC_DIV4;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  hadc1.Init.LowPowerAutoWait = DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.NbrOfConversion = 1;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.DMAContinuousRequests = DISABLE;
  hadc1.Init.SamplingMode = ADC_SAMPLING_MODE_NORMAL;
  hadc1.Init.Overrun = ADC_OVR_DATA_PRESERVED;
  hadc1.Init.OversamplingMode = DISABLE;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  sConfig.Channel = ADC_CHANNEL_1;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_2CYCLES_5;
  sConfig.SingleDiff = ADC_SINGLE_ENDED;
  sConfig.OffsetNumber = ADC_OFFSET_NONE;
  sConfig.Offset = 0;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief DAC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_DAC1_Init(void)
{
  DAC_ChannelConfTypeDef sConfig = {0};

  hdac1.Instance = DAC1;
  if (HAL_DAC_Init(&hdac1) != HAL_OK)
  {
    Error_Handler();
  }

  sConfig.DAC_HighFrequency = DAC_HIGH_FREQUENCY_INTERFACE_MODE_DISABLE;
  sConfig.DAC_DMADoubleDataMode = DISABLE;
  sConfig.DAC_SignedFormat = DISABLE;
  sConfig.DAC_SampleAndHold = DAC_SAMPLEANDHOLD_DISABLE;
  sConfig.DAC_Trigger = DAC_TRIGGER_NONE;
  sConfig.DAC_ConnectOnChipPeripheral = DAC_CHIPCONNECT_EXTERNAL;
  sConfig.DAC_UserTrimming = DAC_TRIMMING_FACTORY;

  if (HAL_DAC_ConfigChannel(&hdac1, &sConfig, DAC_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }

  sConfig.DAC_OutputBuffer = DAC_OUTPUTBUFFER_ENABLE;
  if (HAL_DAC_ConfigChannel(&hdac1, &sConfig, DAC_CHANNEL_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{
  hi2c1.Instance = I2C1;
  hi2c1.Init.Timing = 0x60808CD3;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }

  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }

  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief TIM6 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM6_Init(void)
{
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  htim6.Instance = TIM6;
  htim6.Init.Prescaler = 249;
  htim6.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim6.Init.Period = 3905;
  htim6.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim6) != HAL_OK)
  {
    Error_Handler();
  }

  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim6, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  __disable_irq();
  while (1)
  {
  }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
  (void)file;
  (void)line;
}
#endif /* USE_FULL_ASSERT */
