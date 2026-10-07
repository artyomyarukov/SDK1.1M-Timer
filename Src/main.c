/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Variant 2 - Timer with Buzzer and OLED
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "i2c.h"
#include "usart.h"
#include "gpio.h"
#include "tim.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "kb.h"
#include "sdk_uart.h"
#include "pca9538.h"
#include "oled.h"
#include "fonts.h"
#include "buzzer.h"
#include <stdio.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef enum {
    STATE_INPUT,       // Ввод времени
    STATE_COUNTDOWN, // Обратный отсчет
	STATE_PAUSED, // пауза
    STATE_ALARM        // Сигнал окончания времени

} TimerState_t;
/* USER CODE END PTD */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */
TimerState_t state = STATE_INPUT;
uint32_t timer_seconds = 0;   // Заданное время в секундах
uint32_t last_tick = 0;       // Для неблокирующего отсчета 1 сек
uint8_t update_flag = 1;
char str_buf[32];

// Короткий сигнал тревоги (ноты и длительности)
uint32_t alarm_notes[] = { N_D4, 0, N_D4, 0, N_D4, 0 };
uint32_t alarm_delays[] = { 4, 8, 4, 8, 4, 8 };
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
char Read_Keyboard(void);
void Update_Display(void);
void Show_Startup_Legend(void);
/* USER CODE END PFP */

int main(void)
{
  /* MCU Configuration */
  HAL_Init();
  SystemClock_Config();

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_I2C1_Init();
  MX_USART6_UART_Init();
  MX_TIM2_Init();

  /* USER CODE BEGIN 2 */
  oled_Init();
  Buzzer_Init();
  Buzzer_Play(alarm_notes, alarm_delays, 2);
  Show_Startup_Legend();
  Update_Display();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    char key = Read_Keyboard();

    switch (state)
    {
      case STATE_INPUT:
        if (key >= '0' && key <= '9') {
            if (timer_seconds < 1000) { // Ограничение ввода
                timer_seconds = timer_seconds * 10 + (key - '0');
                update_flag = 1;
            }
        }
        else if (key == '*') { // Сброс
            timer_seconds = 0;
            update_flag = 1;
        }
        else if (key == '#') { // Запуск
            if (timer_seconds > 0) {
                state = STATE_COUNTDOWN;
                last_tick = HAL_GetTick();
                update_flag = 1;
            }
        }
        break;

      case STATE_COUNTDOWN:
        // Отсчет ровно 1 секунды без зависания процессора
        if (HAL_GetTick() - last_tick >= 1000) {
            last_tick += 1000;
            if (timer_seconds > 0) {
                timer_seconds--;
                update_flag = 1;
            }
            if (timer_seconds == 0) {
                state = STATE_ALARM;
                update_flag = 1;
            }
        }
        if (key == '#') {
            state = STATE_PAUSED;  // встали на паузу
            update_flag = 1;
        }

        // Прерывание отсчета кнопкой '*'
        if (key == '*') {
            state = STATE_INPUT;
            timer_seconds = 0;
            update_flag = 1;
        }
        break;

      case STATE_PAUSED:
          if (key == '#') {
              last_tick = HAL_GetTick(); // сбросили миллисекунды
              state = STATE_COUNTDOWN;   // снялись с паузы, продолжаем
              update_flag = 1;
          }
          else if (key == '*') {
              state = STATE_INPUT;       // отмена и возврат ко вводу
              timer_seconds = 0;
              update_flag = 1;
          }
          break;

      case STATE_ALARM:
              // Прерывистый писк (250 мс пищит / 250 мс молчит) БЕЗ зависания процессора
              Buzzer_Set_Freq(N_D4);
              if ((HAL_GetTick() / 250) % 2 == 0) {
                  Buzzer_Set_Volume(BUZZER_VOLUME_MAX); // Включить звук
              } else {
                  Buzzer_Set_Volume(BUZZER_VOLUME_MUTE); // Пауза
              }

              // МГНОВЕННАЯ реакция на любое нажатие:
              if (key != 0) {
                  Buzzer_Set_Volume(BUZZER_VOLUME_MUTE); // Сразу глушим звук
                  timer_seconds = 0;
                  state = STATE_INPUT;                   // Возврат в стартовое меню
                  update_flag = 1;
              }
              break;
    }

    if (update_flag) {
        Update_Display();
        update_flag = 0;
    }

    HAL_Delay(25); // Защита от дребезга контактов
  }
  /* USER CODE END 3 */
}

/* USER CODE BEGIN 4 */

/**
 * @brief Опрос матричной клавиатуры (3x4)
 */
char Read_Keyboard(void) {
    static uint8_t key_held = 0;
    uint8_t rows[4] = {ROW1, ROW2, ROW3, ROW4};
    const char map[4][3] = {
        {'1', '2', '3'},
        {'4', '5', '6'},
        {'7', '8', '9'},
        {'*', '0', '#'}
    };

    for (int r = 0; r < 4; r++) {
        uint8_t k = Check_Row(rows[r]);
        int col = -1;
        if (k == 0x04) col = 0;      // Левая кнопка
        else if (k == 0x02) col = 1; // Средняя кнопка
        else if (k == 0x01) col = 2; // Правая кнопка

        if (col != -1) {
            if (!key_held) {
                key_held = 1;
                return map[r][col];
            }
            return 0; // Удержание кнопки
        }
    }
    key_held = 0;
    return 0;
}

/**
 * @brief Отрисовка интерфейса на OLED
 */
void Update_Display(void) {
    oled_Fill(Black);

    if (state == STATE_INPUT) {
        oled_SetCursor(0, 0);
        oled_WriteString("TIMER SETUP", Font_7x10, White);
        oled_SetCursor(0, 20);
        snprintf(str_buf, sizeof(str_buf), "Time: %lu sec", timer_seconds);
        oled_WriteString(str_buf, Font_7x10, White);
        oled_SetCursor(0, 45);
        oled_WriteString("[#]Start  [*]Clr", Font_7x10, White);
    }
    else if (state == STATE_COUNTDOWN) {
        oled_SetCursor(0, 0);
        oled_WriteString("COUNTDOWN...", Font_7x10, White);
        oled_SetCursor(0, 20);
        snprintf(str_buf, sizeof(str_buf), "%02lu:%02lu", timer_seconds / 60, timer_seconds % 60);
        oled_WriteString(str_buf, Font_7x10, White);
        oled_SetCursor(0, 45);
        oled_WriteString("[*] Cancel", Font_7x10, White);
    }
    else if (state == STATE_ALARM) {
        oled_SetCursor(10, 15);
        oled_WriteString("! TIME IS UP !", Font_7x10, White);
        oled_SetCursor(0, 40);
        oled_WriteString("Press any key...", Font_7x10, White);
    }


    else if (state == STATE_PAUSED) {
        oled_SetCursor(0, 0);
        oled_WriteString("PAUSED ||", Font_7x10, White);
        oled_SetCursor(0, 20);
        snprintf(str_buf, sizeof(str_buf), "%02lu:%02lu", timer_seconds / 60, timer_seconds % 60);
        oled_WriteString(str_buf, Font_7x10, White);
        oled_SetCursor(0, 45);
        oled_WriteString("[#]Resume [*]Stop", Font_7x10, White);
    }

    oled_UpdateScreen();
}



/**
 * @brief Заставка с раскладкой клавиатуры при включении
 */
void Show_Startup_Legend(void) {
    oled_Fill(Black);

    // Заголовок
    oled_SetCursor(0, 0);
    oled_WriteString("KEYPAD MAP:", Font_7x10, White);

    // Сетка клавиш (слева)
    oled_SetCursor(0, 14);
    oled_WriteString("[1][2][3]", Font_7x10, White);
    oled_SetCursor(0, 26);
    oled_WriteString("[4][5][6]", Font_7x10, White);
    oled_SetCursor(0, 38);
    oled_WriteString("[7][8][9]", Font_7x10, White);
    oled_SetCursor(0, 50);
    oled_WriteString("[*][0][#]", Font_7x10, White);

    // Подсказка функций (справа)
    oled_SetCursor(70, 14);
    oled_WriteString("#:Start", Font_7x10, White);
    oled_SetCursor(70, 26);
    oled_WriteString("#:Pause", Font_7x10, White);
    oled_SetCursor(70, 38);
    oled_WriteString("*:Reset", Font_7x10, White);

    oled_UpdateScreen();

    // Показываем заставку 4 секунды (или пропускаем по любой нажатой кнопке)
    uint32_t start_time = HAL_GetTick();
    while (HAL_GetTick() - start_time < 10000) {
        if (Read_Keyboard() != 0) {
            break;
        }
        HAL_Delay(20);
    }
}




/* USER CODE END 4 */

/**
  * @brief System Clock Configuration
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 25;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

void Error_Handler(void)
{
}
