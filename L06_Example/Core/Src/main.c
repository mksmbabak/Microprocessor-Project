/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2023 STMicroelectronics.
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
#include "spi.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <stdlib.h>
#include "bmp2_config.h"
#include "heater_config.h"
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
double temp = 0.0f;     // [degC]
unsigned int temp_int;	// [mdegC]
double press = 0.0f;

uint8_t tx_buffer[8];
const int tx_msg_len = 3;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
// PID Controller Structure
typedef struct {
    float Kp; // Proportional Gain
    float Ki; // Integral Gain
    float Kd; // Derivative Gain
    float prevError; // Previous error
    float integral; // Integral of error
    float setpoint; // Desired value
} PID_Controller;

PID_Controller pid; // PID controller instance

void PID_Init(PID_Controller *pid, float Kp, float Ki, float Kd, float setpoint) {
    pid->Kp = Kp;
    pid->Ki = Ki;
    pid->Kd = Kd;
    pid->prevError = 0.0f;
    pid->integral = 0.0f;
    pid->setpoint = setpoint;
}

float PID_Compute(PID_Controller *pid, float currentTemperature) {
    float error = pid->setpoint - currentTemperature;
    pid->integral += error;
    float derivative = error - pid->prevError;
    float output = (pid->Kp * error) + (pid->Ki * pid->integral) + (pid->Kd * derivative);
    pid->prevError = error;
    return output;
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  if(htim == &htim2)
  {
    static unsigned int cnt = 0;
    cnt++;
    BMP2_ReadData(&bmp2dev, &press, &temp); // Assuming BMP2_ReadData updates both `press` and `temp`

    temp_int = (unsigned int)(temp * 1000); // Convert float temperature to integer (millidegrees)


    // PID computation and control logic
    float controlEffort = PID_Compute(&pid, temp);
    //ControlHeaterFan(temp, pid.setpoint);

  }
}

/* Control Heater and Fan based on PID output and Hysteresis */
void ControlHeaterFan(float currentTemperature, float setpoint) {
    static uint8_t heaterOn = 0;
    static uint8_t fanOn = 0;

    // Define hysteresis threshold
    const float hysteresis = 0.04f; // Degree Celsius

    // Decide on heater or fan based on control effort and hysteresis
    if (currentTemperature < setpoint - hysteresis + 0.03 ) {
        heaterOn = 1; // Turn heater on
        fanOn = 0;    // Ensure fan is off
    } else if (currentTemperature > setpoint + hysteresis) {
        heaterOn = 0; // Ensure heater is off
        fanOn = 1;    // Turn fan on
    } else if(currentTemperature < setpoint - hysteresis) {
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET); // Fan control (INA)
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_8, GPIO_PIN_SET);

    }

    // Actuate the heater or fan based on the above logic
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, heaterOn ? GPIO_PIN_SET : GPIO_PIN_RESET); // Heater control
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, fanOn ? GPIO_PIN_SET : GPIO_PIN_RESET); // Fan control (INA)
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_8, fanOn ? GPIO_PIN_RESET : GPIO_PIN_SET); // Fan control (INB), assuming needed logic
}



// Global or accessible variable declarations
double targetTemperature = 0.0f;  // Target temperature initialized

void SendTemperatureValues() {
    char outputBuffer[50]; // Adjust size as needed
    int messageLength = snprintf(outputBuffer, sizeof(outputBuffer), "Target: %.1f, Current: %.1f\r\n", targetTemperature, temp);
    HAL_UART_Transmit(&huart3, (uint8_t*)outputBuffer, messageLength, 100);
}


char inputBuffer[10]; // Adjust based on expected input size
int inputIndex = 0; // Tracks the position in the input buffer

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart == &huart3) {
        // Check if received character is newline (end of message)
        if (inputBuffer[inputIndex] == '\n') {
            // Null-terminate the string
            inputBuffer[inputIndex] = '\0';

            // Attempt to convert to float
            char* endPtr;
            double newTarget = strtod(inputBuffer, &endPtr);

            // Check if conversion succeeded and the value is in the correct format
            if (endPtr != inputBuffer && *endPtr == '\0' && strchr(inputBuffer, '.') && (endPtr - strchr(inputBuffer, '.')) == 2) {
                // Valid float with one decimal received, update target temperature
                targetTemperature = newTarget;
            } else {
                // Invalid input, prompt for re-entry
                char* errMsg = "Invalid input. Please enter a float value with one decimal:\r\n";
                HAL_UART_Transmit(&huart3, (uint8_t*)errMsg, strlen(errMsg), 100);
            }

            // Reset input buffer for next message
            inputIndex = 0;
        } else {
            // Add received character to buffer if not full, and increment index
            if (inputIndex < sizeof(inputBuffer) - 1) {
                inputIndex++;
            }
        }
        // Prepare to receive the next character
        HAL_UART_Receive_IT(&huart3, (uint8_t*)&inputBuffer[inputIndex], 1);
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
  MX_USART3_UART_Init();
  MX_SPI4_Init();
  MX_TIM2_Init();
  MX_TIM4_Init();
  MX_TIM9_Init();
  /* USER CODE BEGIN 2 */
  BMP2_Init(&bmp2dev);
  PID_Init(&pid, 0.5f, 0.01f, 0.5f, 25); //targetTemperature Example: Kp=2.0, Ki=0.01, Kd=0.5, Setpoint=25°C
  HAL_TIM_Base_Start_IT(&htim2);
  HAL_UART_Receive_IT(&huart3, tx_buffer, tx_msg_len);

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
	  ControlHeaterFan(temp, pid.setpoint);
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

  /** Configure LSE Drive Capability
  */
  HAL_PWR_EnableBkUpAccess();

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_BYPASS;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 4;
  RCC_OscInitStruct.PLL.PLLN = 216;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 3;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Activate the Over-Drive mode
  */
  if (HAL_PWREx_EnableOverDrive() != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV4;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_7) != HAL_OK)
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
