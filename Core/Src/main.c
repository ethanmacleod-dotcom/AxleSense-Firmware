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
#include "ad7124.h"
#include "ad7124_config.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
SPI_HandleTypeDef hspi1;

UART_HandleTypeDef huart2;
UART_HandleTypeDef huart6;

/* USER CODE BEGIN PV */
extern AD7124_ConfigTypeDef AD7124_Handler;
extern uint32_t AD7124_ChannelSamples[AD7124_ENABLED_CHANNELS];
extern AD7124_RegisterTypeDef configA;
extern volatile uint8_t AD7124_LastActiveChannel;
/* ADC initialization status: AD7124_OK should be 0. */
volatile int32_t adc_reset_status  = -99;
volatile int32_t adc_config_status = -99;
/* Fixed-order PCB register readback.
 * rb_seq[0] = ID
 * rb_seq[1] = CH3_MAP
 * rb_seq[2] = CFG1
 * rb_seq[3] = FILT1
 * rb_seq[4] = ADC_CONTROL
 * rb_seq[5] = ERROR_EN
 */
volatile uint32_t rb_magic = 0x12345678U;
volatile uint32_t rb_seq[6] = {0U};
volatile int32_t  rb_st_seq[6] = {-99, -99, -99, -99, -99, -99};
volatile uint8_t  rb_all_reads_ok = 0U;
/* Live conversion diagnostics. */
volatile uint32_t adc_read_errors   = 0U;
volatile uint32_t adc_samples_total = 0U;
volatile uint32_t adc_last_raw      = 0U;
volatile uint32_t adc_last_status   = 0U;
volatile uint8_t  adc_last_channel  = 0xFFU;
/* One-shot strain-input noise measurement. */
volatile uint8_t  noise_done = 0U;
volatile uint32_t noise_window_samples = 0U;
volatile uint32_t noise_raw_min = 0U;
volatile uint32_t noise_raw_max = 0U;
volatile double noise_mean_code = 0.0;
volatile double noise_std_code  = 0.0;
volatile double noise_pp_code   = 0.0;
/* Input-referred estimates use nominal AVDD = 3.3 V and PGA = 64. */
volatile double noise_mean_uV = 0.0;
volatile double noise_std_uV  = 0.0;
volatile double noise_pp_uV   = 0.0;
/* Internal Welford accumulators. */
static uint32_t noise_skip = 256U;
static uint32_t noise_n = 0U;
static uint32_t noise_min_acc = 0xFFFFFFU;
static uint32_t noise_max_acc = 0U;
static double noise_mean_acc = 0.0;
static double noise_m2_acc = 0.0;

/* RS-485 one-way transmit test diagnostics. */
volatile uint32_t rs485_tx_count = 0U;
volatile int32_t rs485_last_status = -99;
static uint32_t rs485_last_tx_ms = 0U;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_SPI1_Init(void);
static void MX_USART6_UART_Init(void);
/* USER CODE BEGIN PFP */
static double NoiseSqrt(double x);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static double NoiseSqrt(double x)
{
    if (x <= 0.0)
    {
        return 0.0;
    }
    double g = (x > 1.0) ? x : 1.0;
    for (uint8_t i = 0U; i < 16U; i++)
    {
        g = 0.5 * (g + x / g);
    }
    return g;
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

  /* USER CODE BEGIN Init */
  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */
  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART2_UART_Init();
  MX_SPI1_Init();
  MX_USART6_UART_Init();
  /* USER CODE BEGIN 2 */
/*
 * PCB AD7124 bring-up test.
 *
 * Hardware under test:
 *   SPI1
 *   CS = AD7124_CS_Pin
 *   strain = CH3, AIN4(+) versus AIN5(-), Setup1
 *
 * SPI1 should be Mode 3 and /64 in the IOC for this reproduction test.
 */
HAL_GPIO_WritePin(AD7124_CS_GPIO_Port, AD7124_CS_Pin, GPIO_PIN_SET);
AD7124_Handler.SPIx   = &hspi1;
AD7124_Handler.csPort = AD7124_CS_GPIO_Port;
AD7124_Handler.csPin  = AD7124_CS_Pin;
/* Reproduce the known previous-PCB driver sequence. */
adc_reset_status =
    (int32_t)AD7124_Reset(&AD7124_Handler, 100U);
adc_config_status =
    (int32_t)AD7124_Config(&AD7124_Handler, &configA);
/*
 * Fixed-order readback. These are the PCB-specific values we care about:
 *
 *   ID          approximately 0x06
 *   CH3_MAP     0x9085  = Setup1, AIN4 vs AIN5
 *   CFG1        0x087E  = bipolar, buffered, AVDD ref, gain 64
 *   FILT1       0x460010
 *   ADC_CONTROL 0x0080
 *   ERROR_EN    0x000004
 *
 * Every read uses the recovered driver, including its CRC checking.
 */
uint32_t rb_tmp = 0U;
rb_magic = 0xA5A5A5A5U;
rb_tmp = 0U;
rb_st_seq[0] = (int32_t)AD7124_ReadRegister(
    &AD7124_Handler, 0x05U, 1U, &rb_tmp);
rb_seq[0] = rb_tmp;
rb_tmp = 0U;
rb_st_seq[1] = (int32_t)AD7124_ReadRegister(
    &AD7124_Handler, AD7124_CH3_MAP_REG, 2U, &rb_tmp);
rb_seq[1] = rb_tmp;
rb_tmp = 0U;
rb_st_seq[2] = (int32_t)AD7124_ReadRegister(
    &AD7124_Handler, AD7124_CFG1_REG, 2U, &rb_tmp);
rb_seq[2] = rb_tmp;
rb_tmp = 0U;
rb_st_seq[3] = (int32_t)AD7124_ReadRegister(
    &AD7124_Handler, AD7124_FILT1_REG, 3U, &rb_tmp);
rb_seq[3] = rb_tmp;
rb_tmp = 0U;
rb_st_seq[4] = (int32_t)AD7124_ReadRegister(
    &AD7124_Handler, AD7124_CONTROL_REG, 2U, &rb_tmp);
rb_seq[4] = rb_tmp;
rb_tmp = 0U;
rb_st_seq[5] = (int32_t)AD7124_ReadRegister(
    &AD7124_Handler, AD7124_ERREN_REG, 3U, &rb_tmp);
rb_seq[5] = rb_tmp;
rb_all_reads_ok = 1U;
for (uint8_t i = 0U; i < 6U; i++)
{
    if (rb_st_seq[i] != (int32_t)AD7124_OK)
    {
        rb_all_reads_ok = 0U;
    }
}
/*
 * FIRST STOP:
 * Inspect adc_reset_status, adc_config_status, rb_seq[], rb_st_seq[],
 * rb_all_reads_ok, and the AD7124_DebugLastRead* globals.
 *
 * Press Resume to continue into the strain-only noise test.
 */

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
while (1)
{
    if (noise_done != 0U)
    {
        /*
         * RS-485 sanity test.
         *
         * PCB routing:
         *   USART2_TX = PA2 -> ADM2587 TXD
         *   USART2_RX = PA3 <- ADM2587 RXD
         *   PA4       = RE/DE direction control
         *
         * Drive PA4 high while transmitting, then return low to receive mode.
         */
        const uint32_t now_ms = HAL_GetTick();

        if ((now_ms - rs485_last_tx_ms) >= 1000U)
        {
            static const uint8_t rs485_msg[] =
                "AXLESENSE RS485 TEST OK\r\n";

            rs485_last_tx_ms = now_ms;

            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);

            rs485_last_status = (int32_t)HAL_UART_Transmit(
                &huart2,
                (uint8_t *)rs485_msg,
                (uint16_t)(sizeof(rs485_msg) - 1U),
                100U
            );

            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET);

            if (rs485_last_status == (int32_t)HAL_OK)
            {
                rs485_tx_count++;
            }
        }

        continue;
    }
    uint32_t status_reg = 0U;
    /*
     * Read STATUS once. With the strain-only ADC config, CH3 is the only
     * enabled conversion channel.
     */
    if (AD7124_ReadRegister(
            &AD7124_Handler,
            AD7124_STATUS_REG,
            1U,
            &status_reg) != AD7124_OK)
    {
        adc_read_errors++;
        continue;
    }
    adc_last_status = status_reg;
    /* STATUS.RDY is active low. */
    if ((status_reg & 0x80U) != 0U)
    {
        continue;
    }
    adc_last_channel = (uint8_t)(status_reg & 0x0FU);
    if (adc_last_channel != 3U)
    {
        adc_read_errors++;
        continue;
    }
    /*
     * Read DATA directly after the STATUS read. This matches the previous-PCB
     * debug harness and avoids doing a second STATUS read before DATA.
     */
    uint32_t raw = 0U;
    if (AD7124_ReadRegister(
            &AD7124_Handler,
            AD7124_DATA_REG,
            3U,
            &raw) != AD7124_OK)
    {
        adc_read_errors++;
        continue;
    }
    adc_last_raw = raw;
    adc_samples_total++;
    /* Allow the digital filter to settle before collecting statistics. */
    if (noise_skip > 0U)
    {
        noise_skip--;
        continue;
    }
    if (noise_n == 0U)
    {
        noise_min_acc = raw;
        noise_max_acc = raw;
        noise_mean_acc = 0.0;
        noise_m2_acc = 0.0;
    }
    if (raw < noise_min_acc)
    {
        noise_min_acc = raw;
    }
    if (raw > noise_max_acc)
    {
        noise_max_acc = raw;
    }
    /* Welford running mean and variance. */
    noise_n++;
    const double x = (double)raw;
    const double delta = x - noise_mean_acc;
    noise_mean_acc += delta / (double)noise_n;
    const double delta2 = x - noise_mean_acc;
    noise_m2_acc += delta * delta2;
    if (noise_n >= 2048U)
    {
        const double variance =
            noise_m2_acc / (double)(noise_n - 1U);
        const double std_codes = NoiseSqrt(variance);
        const double pp_codes =
            (double)(noise_max_acc - noise_min_acc);
        /*
         * CONFIG1 uses AVDD as Vref and gain 64.
         * For a bipolar code:
         *   Vin = ((code / 8388608) - 1) * Vref / gain
         *
         * AVDD is nominally 3.3 V, so the uV values are nominal
         * input-referred estimates. Raw-code statistics remain exact.
         */
        const double nominal_vref_V = 3.3;
        const double pga_gain = 64.0;
        const double code_to_uV =
            (nominal_vref_V / (pga_gain * 8388608.0)) * 1.0e6;
        noise_window_samples = noise_n;
        noise_raw_min = noise_min_acc;
        noise_raw_max = noise_max_acc;
        noise_mean_code = noise_mean_acc;
        noise_std_code  = std_codes;
        noise_pp_code   = pp_codes;
        noise_mean_uV =
            ((noise_mean_acc / 8388608.0) - 1.0) *
            (nominal_vref_V / pga_gain) *
            1.0e6;
        noise_std_uV = std_codes * code_to_uV;
        noise_pp_uV  = pp_codes  * code_to_uV;
        noise_done = 1U;
        /*
         * SECOND STOP:
         * Inspect noise_* plus adc_samples_total and adc_read_errors.
         */

    }
}
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
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

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 16;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
  RCC_OscInitStruct.PLL.PLLQ = 4;
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

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */
  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */
  /* USER CODE END SPI1_Init 1 */
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_HIGH;
  hspi1.Init.CLKPhase = SPI_PHASE_2EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_64;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */
  /* USER CODE END SPI1_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */
  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */
  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */
  /* USER CODE END USART2_Init 2 */

}

/**
  * @brief USART6 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART6_UART_Init(void)
{

  /* USER CODE BEGIN USART6_Init 0 */
  /* USER CODE END USART6_Init 0 */

  /* USER CODE BEGIN USART6_Init 1 */
  /* USER CODE END USART6_Init 1 */
  huart6.Instance = USART6;
  huart6.Init.BaudRate = 115200;
  huart6.Init.WordLength = UART_WORDLENGTH_8B;
  huart6.Init.StopBits = UART_STOPBITS_1;
  huart6.Init.Parity = UART_PARITY_NONE;
  huart6.Init.Mode = UART_MODE_TX_RX;
  huart6.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart6.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart6) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART6_Init 2 */
  /* USER CODE END USART6_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */
  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, LED5_Pin|LED2_Pin|LED1_Pin|LED3_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, LED6_Pin|RS485_RE_DE_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(AD7124_CS_GPIO_Port, AD7124_CS_Pin, GPIO_PIN_SET);

  /*Configure GPIO pins : LED5_Pin LED2_Pin LED1_Pin LED3_Pin */
  GPIO_InitStruct.Pin = LED5_Pin|LED2_Pin|LED1_Pin|LED3_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : LED6_Pin RS485_RE_DE_Pin AD7124_CS_Pin */
  GPIO_InitStruct.Pin = LED6_Pin|RS485_RE_DE_Pin|AD7124_CS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */
  /* USER CODE END MX_GPIO_Init_2 */
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
#ifdef USE_FULL_ASSERT
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
