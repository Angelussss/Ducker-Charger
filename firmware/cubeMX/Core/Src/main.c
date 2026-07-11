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
#include "adc.h"
#include "gpio.h"
#include "i2c.h"
#include "spi.h"
#include "tim.h"
#include "usart.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "app/initialization.h"   /* every-boot IC init (TPS25750 patch, INA3221, STPD01) */
#include "app/provisioning.h"     /* one-time BQ34Z100 data-flash provisioning */
#include "system/charge.h"
#include "system/fsm.h"
#include "app/encoder.h"
#include "app/telemetry.h"
#include "display/ili9341.h"
#include "ui/ui_state.h"
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

/* USER CODE BEGIN PV */
static FSM fsm;
static uint32_t lastActivityTick = 0;
static uint32_t lastEncoderActivityTick = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
 * @brief  The application entry point.
 * @retval int
 */
int main(void) {

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick.
   */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_ADC1_Init();
  MX_I2C1_Init();
  MX_SPI1_Init();
  MX_TIM3_Init();
  MX_USART1_UART_Init();
  MX_I2C3_Init();
  /* USER CODE BEGIN 2 */
  /* Boot camp: every IC with volatile config gets initialized here.
   * A failed step degrades the boot (report available for the UI),
   * it never blocks it. */
  (void)System_Initialization();

  /* Fuel-gauge data flash: one-time, marker-guarded, refuses to run
   * unless the pack is at rest. No-op on already-provisioned units. */
  if (Provisioning_Required()) {
    (void)Provisioning_RunGauge();
  }

  Encoder_Init(&htim3);
  Telemetry_Init(&hi2c1, &hi2c3);
  ILI9341_Init(&hspi1);
  UI_Init();

  init();
  PB_FSM_Init(&fsm);
  lastActivityTick = HAL_GetTick();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1) {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    readNCS();

    Event_t evt;
    if (event_pop(&evt)) {
      if (evt == EVT_BUTTON_SHORT || evt == EVT_BUTTON_LONG)
        lastActivityTick = HAL_GetTick();
      PB_FSM_FireEvent(&fsm, evt);
    }

    /* Real knob/button activity also counts against the global inactivity
     * timer -- otherwise SLEEP/DEEP_SLEEP would still fire on schedule
     * while the user is actively turning the encoder. */
    uint32_t encoderActivityTick = Encoder_LastActivityTick();
    if (encoderActivityTick != lastEncoderActivityTick) {
      lastEncoderActivityTick = encoderActivityTick;
      lastActivityTick = HAL_GetTick();
    }

    /* Deep-sleep entry: encoder button held >= BUTTON_DEEPSLEEP_HOLD_MS,
     * then released, pushes EVT_BUTTON_LONG (IDLE/SLEEP -> DEEP_SLEEP).
     * On release and not while held: STOP mode wakes on both EXTI0 edges,
     * so sleeping with the button still down would wake immediately. */
    static uint32_t holdStartTick = 0;
    if (Encoder_IsHeld()) {
      if (holdStartTick == 0)
        holdStartTick = HAL_GetTick();
    } else {
      if (holdStartTick != 0 &&
          HAL_GetTick() - holdStartTick >= BUTTON_DEEPSLEEP_HOLD_MS)
        event_push(EVT_BUTTON_LONG);
      holdStartTick = 0;
    }

    if (HAL_GetTick() - lastActivityTick > INACTIVITY_TIMEOUT_MS) {
      event_push(EVT_INACTIVITY);
      lastActivityTick = HAL_GetTick();
    }

    State_ID_t stateBeforeUpdate = PB_FSM_GetState(&fsm);
    PB_FSM_Update(&fsm);

    /* Leaving DEEP_SLEEP: the UI was frozen mid-screen when the MCU
     * stopped; restart it from the splash and swallow the wake press. */
    if (stateBeforeUpdate == STATE_DEEP_SLEEP &&
        PB_FSM_GetState(&fsm) != STATE_DEEP_SLEEP) {
      UI_OnDeepSleepWake();
    }

    /* Reshape the charge layer's fresh readings for the UI. Must run here
     * and not inside UI_Tick(): the FSM keeps polling sensors in SLEEP
     * (Sleep_Run) but UI_Tick() is skipped below, and the uptime counter,
     * the energy integral and the per-port histories have to keep running
     * while the screen is dark. Telemetry_Poll() rate-limits itself. */
    Telemetry_Poll();

    /* Skip UI work while the FSM has the backlight physically off --
     * avoids pointless SPI/CPU churn into a dark screen. In DEEP_SLEEP
     * this also keeps the wake press latched in the encoder layer until
     * UI_OnDeepSleepWake() consumes it. */
    State_ID_t st = PB_FSM_GetState(&fsm);
    if (st != STATE_SLEEP && st != STATE_DEEP_SLEEP) {
      UI_Tick();
    } else if (st == STATE_SLEEP && Encoder_IsPressed()) {
      /* UI_Tick() is the only consumer of button presses, and it is not
       * running here: turn the raw press into the wake event ourselves,
       * or SLEEP -> IDLE could never fire on a button. Consuming the
       * press also keeps it from navigating the UI after the wake. */
      event_push(EVT_BUTTON_SHORT);
    }
  }
  /* USER CODE END 3 */
}

/**
 * @brief System Clock Configuration
 * @retval None
 */
void SystemClock_Config(void) {
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
   */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE2);

  /** Initializes the RCC Oscillators according to the specified parameters
   * in the RCC_OscInitTypeDef structure.
   */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 84;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
   */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                                RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK) {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
 * @brief  This function is executed in case of error occurrence.
 * @retval None
 */
void Error_Handler(void) {
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1) {
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
void assert_failed(uint8_t *file, uint32_t line) {
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line
     number, ex: printf("Wrong parameters value: file %s on line %d\r\n", file,
     line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
