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
#include "CS4270-Codec.h"
#include "../../../deps/DaisySP/Source/daisysp.h"
#include "../../../util/reverbsc_int16.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

#define SAMPLING_FREQUENCY_HZ 48000.0f

#define UINT16_TO_FLOAT 0.00001525878f
#define INT16_TO_FLOAT 	0.00003051757f
#define FLOAT_TO_INT16 	32768.0f

#define AUDIO_BUFFER_SIZE 48

// Number of ADC channels for controls (potentiometers)
#define CONTROL_SETTING_COUNT 7

// Controls have to be changed by +- this percentage amount to update FX parameters
#define CONTROL_SETTING_CHANGE_THRESHOLD 0.005f

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;
DMA_HandleTypeDef hdma_adc1;

I2C_HandleTypeDef hi2c1;

SAI_HandleTypeDef hsai_BlockA1;
SAI_HandleTypeDef hsai_BlockB1;
DMA_HandleTypeDef hdma_sai1_a;
DMA_HandleTypeDef hdma_sai1_b;

TIM_HandleTypeDef htim3;

/* USER CODE BEGIN PV */


/* NON-CACHEABLE MEMORY */
int32_t adcBuf[AUDIO_BUFFER_SIZE] 		__attribute__ ((section(".audiobuffer")));
int32_t dacBuf[AUDIO_BUFFER_SIZE] 		__attribute__ ((section(".audiobuffer")));

static volatile int32_t *inBufPtr 		__attribute__ ((section(".audiobuffer")));
static volatile int32_t *outBufPtr 		__attribute__ ((section(".audiobuffer")));

volatile uint8_t processHalfFlag		__attribute__ ((section(".audiobuffer")));

volatile uint8_t controlSettingsLocked	__attribute__ ((section(".audiobuffer")));
volatile uint8_t controlSettingsFlag	__attribute__ ((section(".audiobuffer")));

volatile uint16_t controlSettingBuf[CONTROL_SETTING_COUNT] 	__attribute__ ((section(".audiobuffer")));
volatile float controlSetting[CONTROL_SETTING_COUNT]		__attribute__ ((section(".audiobuffer")));
float controlSettingAtAlt[CONTROL_SETTING_COUNT];  // Used for knob movement detection for alternate interval modes
volatile float  prevControlSetting[CONTROL_SETTING_COUNT]	__attribute__ ((section(".audiobuffer"))); // TODO Moved to global, see if knob turning works better


volatile float delayMix						        __attribute__ ((section(".audiobuffer")));
volatile float reverbMix						        __attribute__ ((section(".audiobuffer")));
volatile float verbTime					       	__attribute__ ((section(".audiobuffer")));
volatile float verbFreq					        __attribute__ ((section(".audiobuffer")));


// Expression
bool expressionControl = false;

//Bypass footswitch
uint32_t bypass_threshold = 100;
uint32_t bypass_wait_timer = 100;
bool bypass = true;
uint8_t bypass_reset = 0;

uint32_t quiet_bypass_threshold = 40;  // TODO Test minumum threshold this will work (in milliseconds)
uint32_t quiet_bypass_wait_timer = 40;
uint8_t quiet_bypass_reset = 0;

//Aux footswitch
uint32_t aux_threshold = 100;
uint32_t aux_wait_timer = 100;
bool aux = false;
uint8_t aux_reset = 0;

// Toggle switches
int leftTogglePosition = 0; // 0=up, 1=middle. 2=down
int rightTogglePosition = 0;

float volume = 1.0;
float counter = -0.0001;


//Reverb
using namespace daisysp;
ReverbSc16 verb;


// Delay
#define MAX_DELAY static_cast<size_t>(48000 * 2.f) // 2 second max delay
DelayLine<float, MAX_DELAY> delayLine;

struct delay
{
    DelayLine<float, MAX_DELAY> *del;
    float                        currentDelay;
    float                        delayTarget;
    float                        feedback;
    float                        active = false;

    float Process(float in)
    {
        //set delay times
        fonepole(currentDelay, delayTarget, .0002f);
        del->SetDelay(currentDelay);

        float read = del->Read();
        if (active) {
            del->Write((feedback * read) + in);
        } else {
            del->Write((feedback * read)); // if not active, don't write any new sound to buffer
        }

        return read;
    }
};

delay             delay1;



/*
float signedINT16_to_float(int16_t s)
{
    return s * INT16_TO_FLOAT;
}

int16_t float_to_signedINT16(float sample)
{
    // Clamp the float to range [-1.0, 1.0]
    float clamped = std::max(-1.0f, std::min(1.0f, sample));

    // Scale to 16-bit integer range
    return (int16_t)(clamped * FLOAT_TO_INT16);
}
*/


/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void PeriphCommonClock_Config(void);
static void MPU_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_SAI1_Init(void);
static void MX_I2C1_Init(void);
static void MX_ADC1_Init(void);
static void MX_TIM3_Init(void);
/* USER CODE BEGIN PFP */


void Process_HalfBuffer() {

    controlSettingsLocked = 1;


    // Input samples  ( TODO: Remove right channel, not used in this hardware)
    static float leftIn	= 0.0f;
    static float rightIn = 0.0f;

    // Output samples
    static float leftOut	= 0.0f;
    static float rightOut	= 0.0f;

    // Timers used for footswitch debouncing and hardware mute for quiet relay bypass
    if (bypass_wait_timer < bypass_threshold) {
        bypass_wait_timer += 1;
    }

    if (quiet_bypass_wait_timer < quiet_bypass_threshold) {
        quiet_bypass_wait_timer += 1;
    }

    if (aux_wait_timer < aux_threshold) {
        aux_wait_timer += 1;
    }



    // Loop through half of audio buffer (double buffering), convert int->float, apply processing, convert float->int, set output buffers
    for (uint16_t sampleIndex = 0; sampleIndex < (AUDIO_BUFFER_SIZE/2) - 1; sampleIndex += 2) {

        /*
        * Convert current input samples (24-bits) to floats (two I2S data lines, two channels per data line)
        */

        // Extract 24-bits via bit mask
        inBufPtr[sampleIndex]		&= 0xFFFFFF;
        inBufPtr[sampleIndex + 1]	&= 0xFFFFFF;

        // Check if number is negative (sign bit)
        if (inBufPtr[sampleIndex] & 0x800000) {
            inBufPtr[sampleIndex] |= ~0xFFFFFF;
        }

        if (inBufPtr[sampleIndex + 1] & 0x800000) {
            inBufPtr[sampleIndex + 1] |= ~0xFFFFFF;
        }

        // Normalise to float (-1.0, +1.0)
        leftIn  = (float) inBufPtr[sampleIndex]     / (float) (0x7FFFFF);
        rightIn = (float) inBufPtr[sampleIndex + 1] / (float) (0x7FFFFF);

        /////////////////////////////////////////////////////////////////////////
        // BEGIN DSP ////////////////////////////////////////////////////////////
        ///////////////////////////////////////////////////////////////////////// 

        float dryLevelAdjust = 1.2; // Level adjustment for dry signal to match true bypass level
        // (Note the perceptive levels seem different on amp vs through audio interface) - why?

    	  float sendl, sendr, wetl, wetr;  // Reverb Inputs/Outputs

    	  sendl = sendr = leftIn;
    	  verb.Process(sendl, sendr, &wetl, &wetr);
    	  float reverb_out = (wetl + wetr) / 2;


        // Process Delay
        float delay_out = delay1.Process(leftIn);

        //leftOut = leftIn * (1.0 - Mix) + wetl * Mix;
        leftOut = leftIn * dryLevelAdjust + reverbMix * reverb_out + delayMix * delay_out;

        /////////////////////////////////////////////////////////////////////////
        // END DSP ////////////////////////////////////////////////////////////
        ///////////////////////////////////////////////////////////////////////// 

        /*
        * Convert floats to 32-bit output samples
        */

        // Ensure output samples are within [-1.0,+1.0] range
        if (leftOut < -1.0f) {
            leftOut = -1.0f;
        } else if (leftOut > 1.0f) {
            leftOut =  1.0f;
        }

        if (rightOut < -1.0f) {
            rightOut = -1.0f;
        } else if (rightOut > 1.0f) {
            rightOut =  1.0f;
        }

        // Scale to 24-bit signed integer and set output buffer
        outBufPtr[sampleIndex]	   = (int32_t) (leftOut  * 0x7FFFFF);
        outBufPtr[sampleIndex + 1] = (int32_t) (rightOut * 0x7FFFFF);


    }

    controlSettingsLocked = 0;

}


/* ADC Callbacks */
void HAL_SAI_RxHalfCpltCallback(SAI_HandleTypeDef *hsai) {

	inBufPtr  = &(adcBuf[0]);
	outBufPtr = &(dacBuf[0]);

	Process_HalfBuffer();

}

void HAL_SAI_RxCpltCallback(SAI_HandleTypeDef *hsai) {

	inBufPtr  = &(adcBuf[AUDIO_BUFFER_SIZE/2]);
	outBufPtr = &(dacBuf[AUDIO_BUFFER_SIZE/2]);

	Process_HalfBuffer();

}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc) {

	if (hadc->Instance == ADC1) {

		controlSettingsFlag = 1;

	}

}



/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MPU Configuration--------------------------------------------------------*/
  MPU_Config();

  /* Enable the CPU Cache */

  /* Enable I-Cache---------------------------------------------------------*/
  SCB_EnableICache();

  /* Enable D-Cache---------------------------------------------------------*/
  SCB_EnableDCache();

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* Configure the peripherals common clocks */
  PeriphCommonClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_SAI1_Init();
  MX_I2C1_Init();
  MX_ADC1_Init();
  MX_TIM3_Init();
  /* USER CODE BEGIN 2 */

  // Potentiometer controls
  controlSetting[0] = 0.5f;
  controlSetting[1] = 0.5f;
  controlSetting[2] = 0.5f;
  controlSetting[3] = 0.5f;
  controlSetting[4] = 0.5f;
  controlSetting[5] = 0.5f;
  controlSetting[6] = 0.5f;

  delayLine.Init();
  delay1.del = &delayLine;
  delay1.delayTarget = 2400; // in samples
  delay1.feedback = 0.0;
  delay1.active = true;

  // Effects
  verb.Init(SAMPLING_FREQUENCY_HZ);

  HAL_StatusTypeDef halStatus;

  /* Initialise Control ADCs */

  halStatus = HAL_ADCEx_Calibration_Start(&hadc1, ADC_CALIB_OFFSET, ADC_SINGLE_ENDED);

  /* Start control setting ADCs and sampling timer */
  halStatus = HAL_ADC_Start_DMA(&hadc1, (uint32_t *) controlSettingBuf, CONTROL_SETTING_COUNT);
  halStatus = HAL_TIM_Base_Start(&htim3); // TODO Make sure timer reference is correct


  /* Initialise SAIs (to start clocks, so that codec PLL locks) */
  halStatus = HAL_SAI_Init(&hsai_BlockA1);
  halStatus = HAL_SAI_Init(&hsai_BlockB1);

  // Set bypass as default when powering on
  HAL_GPIO_WritePin(GPIOD, GPIO_PIN_0, GPIO_PIN_RESET);


  //HAL_GPIO_WritePin(GPIOA, GPIO_PIN_11, GPIO_PIN_SET); // Turn on right led
  //HAL_GPIO_WritePin(GPIOA, GPIO_PIN_9, GPIO_PIN_SET); // Turn on  left led

  /*
   * Initialise codec
   */
  uint8_t codecInitStatus = CS4270_Init();

  /*
   * Start SAI DAC transmission
   */
  halStatus = HAL_SAI_Transmit_DMA(&hsai_BlockA1, (uint8_t *) dacBuf, AUDIO_BUFFER_SIZE);

  /*
   * Start SAI ADC reception
   */
  halStatus = HAL_SAI_Receive_DMA(&hsai_BlockB1,  (uint8_t *) adcBuf, AUDIO_BUFFER_SIZE);

  // Placeholders for using PWM for LEDs, currently just using GPIO, need to recheck values if using TIM code below
  //HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2);
  //HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_3);
  //HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_4);



  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
        ////////////////////////////////////////////////////////////////////////////////////////////////
        // Begin Knob Controls and Expression //////////////////////////////////////////////////////////
        ////////////////////////////////////////////////////////////////////////////////////////////////

	if ( controlSettingsFlag && !controlSettingsLocked ) {

		for (uint8_t controlSettingNum = 0; controlSettingNum < CONTROL_SETTING_COUNT; controlSettingNum++) {

			// Convert raw ADC readings to float (0 ... 1), and low-pass filter  TODO Removed for now, test if it helps later
			controlSetting[controlSettingNum] = UINT16_TO_FLOAT * controlSettingBuf[controlSettingNum];

			// UPDATE detecting change in parameters, if knob turned to slowly, wont detect change. compare to last time the controls were changed, not the previous adc reading
			// If large enough control setting change set control parameters
			float time_temp= 0.0;
			if ( (controlSetting[controlSettingNum] > (1.0f + CONTROL_SETTING_CHANGE_THRESHOLD) * prevControlSetting[controlSettingNum]) || (controlSetting[controlSettingNum] < (1.0f - CONTROL_SETTING_CHANGE_THRESHOLD) * prevControlSetting[controlSettingNum]) ) {

				// Shift samples  TODO Moved this here, see if it works better
				prevControlSetting[controlSettingNum] = controlSetting[controlSettingNum];

				switch (controlSettingNum) {

					case 0: {
						reverbMix = controlSetting[0];

						break;
                    }

					case 1: {
                    	verbTime = controlSetting[1];
                        time_temp = (float) verbTime;
                    	verb.SetFeedback(time_temp);

						break;
                    }

					case 2: {
                    	verbFreq = controlSetting[2];
                    	verb.SetLpFreq(verbFreq * verbFreq * 16000.0 + 500.0); // 500 to 16500 Hz low pass filter on reverb

						break;
                    }

					case 3: {
						delayMix = controlSetting[3];

						break;
                    }

					case 4: {
						float delayTime = controlSetting[4];
						delay1.delayTarget = delayTime * 96000;

						break;
					}

					case 5: {
						float delayFeedback = controlSetting[5];
						delay1.feedback = delayFeedback;


					    break;


					}

					case 6: {


						break;
                    }

				}

			}

		}

		controlSettingsFlag = 0;
	}

        ////////////////////////////////////////////////////////////////////////////////////////////////
        // End Knob Controls and Expression ////////////////////////////////////////////////////////////
        ////////////////////////////////////////////////////////////////////////////////////////////////


        //////////////////////////////////////////////////////////////////////////////////////////////////
        // Begin Footswitches ////////////////////////////////////////////////////////////////////////////
        //////////////////////////////////////////////////////////////////////////////////////////////////

	// TODO Limit the frequency of pin reads here, as is done when reading the analog pins

	// Bypass Footswitch Action //////////////////////////////////////////////////////////////////////
	uint32_t bypass_footswitch = HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_11);
	//uint32_t aux_footswitch = HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_7);

	// GPIO_PIN_SET means not pressed
	// GPIO_PIN_RESET means pressed

	// Check if footswitch is pressed, the debouce wait is done, and the footswitch was previously not pressed (not held)

	if (bypass_footswitch == GPIO_PIN_RESET && bypass_wait_timer == bypass_threshold && bypass_reset == 0) {
		bypass_wait_timer = 0; // Start bypass wait timer for debouce
		bypass_reset = 1;      // 1 means footswitch is still held

	}

	if (bypass_wait_timer == bypass_threshold && bypass_footswitch == GPIO_PIN_SET && bypass_reset == 1) { // If the footswitch is let go

    // Mute audio for quiet bypass and start timer for unmuting
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_2, GPIO_PIN_SET);
    quiet_bypass_reset = 1;
    quiet_bypass_wait_timer = 0;

    bypass_reset = 0;
	  bypass_wait_timer = 0; // Start bypass wait timer for debouce
    bypass = (bypass == false) ? true : false;

    if (bypass) {
      HAL_GPIO_WritePin(GPIOD, GPIO_PIN_0, GPIO_PIN_RESET);  // Bypass audio (true bypass relay)
      HAL_GPIO_WritePin(GPIOA, GPIO_PIN_11, GPIO_PIN_RESET); // Turn off led
        	
    } else {
      HAL_GPIO_WritePin(GPIOD, GPIO_PIN_0, GPIO_PIN_SET);  // Route audio through effect
      HAL_GPIO_WritePin(GPIOA, GPIO_PIN_11, GPIO_PIN_SET); // Turn on led

    }

	}

  // If bypass muting was triggered and timer has finished, unmute audio and reset timer
  if (quiet_bypass_wait_timer == quiet_bypass_threshold && quiet_bypass_reset == 1) {
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_2, GPIO_PIN_RESET);
    quiet_bypass_reset = 0;
  }



	// Aux Footswitch Action (Hold to engage, let go to disengage) //////////////////////////////////////
	    uint32_t aux_footswitch = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_8);
    /*
	if (aux_footswitch == GPIO_PIN_RESET) {
        aux = true;
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_9, GPIO_PIN_SET); // Turn on left led
        HAL_GPIO_WritePin(GPIOD, GPIO_PIN_2, GPIO_PIN_SET); // Mute audio (testing mute)

	} else if (aux_footswitch == GPIO_PIN_SET) {
        aux = false;
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_9, GPIO_PIN_RESET); // Turn off left led
        HAL_GPIO_WritePin(GPIOD, GPIO_PIN_2, GPIO_PIN_RESET); // Unmute audio (testing mute)
	}
    */
	  // GPIO_PIN_SET means NOT pressed (1) (Opposite of what I usually think)
	  // GPIO_PIN_RESET means pressed (0) (because the pin reads low, pressing footswitch connects pin to GND)

        // Initial check for when aux footswitch is pressed
	  if (aux_footswitch == GPIO_PIN_RESET && aux_wait_timer == aux_threshold && aux == false && aux_reset == 0) {
	    aux_wait_timer = 0; // Start aux wait timer for debouce
      aux_reset = 1;
	  }

        // Once debounce is complete and aux footswitch is still pressed, turn on Aux
	  if (aux_footswitch == GPIO_PIN_RESET && aux_wait_timer == aux_threshold && aux == false && aux_reset == 1) { // If the footswitch is held, turn on led, do any other aux action
      aux = true;
      aux_reset = 0;
      HAL_GPIO_WritePin(GPIOA, GPIO_PIN_9, GPIO_PIN_SET); // Turn on left led
      HAL_GPIO_WritePin(GPIOD, GPIO_PIN_2, GPIO_PIN_SET); // Mute audio (testing mute)
    }

        // Initial check for once aux is let go
  	if (aux_footswitch == GPIO_PIN_SET && aux_wait_timer == aux_threshold && aux == true && aux_reset == 0) {
	    aux_wait_timer = 0; // Start aux wait timer for debouce
      aux_reset = 1; 
	  }

        // Once debounce is complete and aux footswitch is let go, turn off Aux
	  if (aux_footswitch == GPIO_PIN_SET && aux_wait_timer == aux_threshold && aux == true && aux_reset == 1) {
      aux = false;
      aux_reset = 0; 
      HAL_GPIO_WritePin(GPIOA, GPIO_PIN_9, GPIO_PIN_RESET); // Turn off left led
      HAL_GPIO_WritePin(GPIOD, GPIO_PIN_2, GPIO_PIN_RESET); // Unmute audio (testing mute)
	  }

    ////////////////////////////////////////////////////////////////////////////////////////////////
    // End Footswitches ////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////////////////

    //////////////////////////////////////////////////////////////////////////////////////////////////
    // Begin Toggle Switches /////////////////////////////////////////////////////////////////////////
    //////////////////////////////////////////////////////////////////////////////////////////////////

    // Read the two toggle switch positions (Three-way toggles, using two GPIO inputs each)  /////////

    uint32_t right_toggle_up = HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_13); // Use your configured Port & Pin
    uint32_t right_toggle_down = HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_11); // Use your configured Port & Pin


    uint32_t left_toggle_up = HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_9); // Use your configured Port & Pin
    uint32_t left_toggle_down = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_3); // Use your configured Port & Pin


    if (left_toggle_up == 0) {  // 0 means pressed, or toggle position is up
    	//HAL_GPIO_WritePin(GPIOD, GPIO_PIN_0, GPIO_PIN_RESET);

    } else if (left_toggle_down == 0) { // 0 means pressed, or toggle position is down
    	//HAL_GPIO_WritePin(GPIOD, GPIO_PIN_0, GPIO_PIN_SET);

    } else {

    }


    if (right_toggle_up == 0) {
    	//HAL_GPIO_WritePin(GPIOD, GPIO_PIN_0, GPIO_PIN_RESET);

    } else if (right_toggle_down == 0) {
    	//HAL_GPIO_WritePin(GPIOD, GPIO_PIN_0, GPIO_PIN_SET);

    } else {

    }

    //////////////////////////////////////////////////////////////////////////////////////////////////
    // End Toggle Switches ///////////////////////////////////////////////////////////////////////////
    //////////////////////////////////////////////////////////////////////////////////////////////////

  } // end while
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

  /*AXI clock gating */
  RCC->CKGAENR = 0xE003FFFF;

  /** Supply configuration update enable
  */
  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 35;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_3;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_6) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief Peripherals Common Clock Configuration
  * @retval None
  */
void PeriphCommonClock_Config(void)
{
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

  /** Initializes the peripherals clock
  */
  PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_ADC|RCC_PERIPHCLK_SAI1;
  PeriphClkInitStruct.PLL2.PLL2M = 10;
  PeriphClkInitStruct.PLL2.PLL2N = 192;
  PeriphClkInitStruct.PLL2.PLL2P = 25;
  PeriphClkInitStruct.PLL2.PLL2Q = 2;
  PeriphClkInitStruct.PLL2.PLL2R = 2;
  PeriphClkInitStruct.PLL2.PLL2RGE = RCC_PLL2VCIRANGE_0;
  PeriphClkInitStruct.PLL2.PLL2VCOSEL = RCC_PLL2VCOWIDE;
  PeriphClkInitStruct.PLL2.PLL2FRACN = 0;
  PeriphClkInitStruct.Sai1ClockSelection = RCC_SAI1CLKSOURCE_PLL2;
  PeriphClkInitStruct.AdcClockSelection = RCC_ADCCLKSOURCE_PLL2;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_MultiModeTypeDef multimode = {0};
  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Common config
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_ASYNC_DIV1;
  hadc1.Init.Resolution = ADC_RESOLUTION_16B;
  hadc1.Init.ScanConvMode = ADC_SCAN_ENABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SEQ_CONV;
  hadc1.Init.LowPowerAutoWait = DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.NbrOfConversion = 7;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_EXTERNALTRIG_T3_TRGO;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_RISING;
  hadc1.Init.ConversionDataManagement = ADC_CONVERSIONDATA_DMA_CIRCULAR;
  hadc1.Init.Overrun = ADC_OVR_DATA_PRESERVED;
  hadc1.Init.LeftBitShift = ADC_LEFTBITSHIFT_NONE;
  hadc1.Init.OversamplingMode = DISABLE;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure the ADC multi-mode
  */
  multimode.Mode = ADC_MODE_INDEPENDENT;
  if (HAL_ADCEx_MultiModeConfigChannel(&hadc1, &multimode) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_5;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_32CYCLES_5;
  sConfig.SingleDiff = ADC_SINGLE_ENDED;
  sConfig.OffsetNumber = ADC_OFFSET_NONE;
  sConfig.Offset = 0;
  sConfig.OffsetSignedSaturation = DISABLE;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_9;
  sConfig.Rank = ADC_REGULAR_RANK_2;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_8;
  sConfig.Rank = ADC_REGULAR_RANK_3;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_4;
  sConfig.Rank = ADC_REGULAR_RANK_4;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_7;
  sConfig.Rank = ADC_REGULAR_RANK_5;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_3;
  sConfig.Rank = ADC_REGULAR_RANK_6;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_19;
  sConfig.Rank = ADC_REGULAR_RANK_7;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.Timing = 0x20B0CCFF;
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

  /** Configure Analogue filter
  */
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Digital filter
  */
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief SAI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SAI1_Init(void)
{

  /* USER CODE BEGIN SAI1_Init 0 */

  /* USER CODE END SAI1_Init 0 */

  /* USER CODE BEGIN SAI1_Init 1 */

  /* USER CODE END SAI1_Init 1 */
  hsai_BlockA1.Instance = SAI1_Block_A;
  hsai_BlockA1.Init.AudioMode = SAI_MODEMASTER_TX;
  hsai_BlockA1.Init.Synchro = SAI_ASYNCHRONOUS;
  hsai_BlockA1.Init.OutputDrive = SAI_OUTPUTDRIVE_DISABLE;
  hsai_BlockA1.Init.NoDivider = SAI_MASTERDIVIDER_ENABLE;
  hsai_BlockA1.Init.FIFOThreshold = SAI_FIFOTHRESHOLD_EMPTY;
  hsai_BlockA1.Init.AudioFrequency = SAI_AUDIO_FREQUENCY_48K;
  hsai_BlockA1.Init.SynchroExt = SAI_SYNCEXT_DISABLE;
  hsai_BlockA1.Init.MonoStereoMode = SAI_STEREOMODE;
  hsai_BlockA1.Init.CompandingMode = SAI_NOCOMPANDING;
  hsai_BlockA1.Init.TriState = SAI_OUTPUT_NOTRELEASED;
  if (HAL_SAI_InitProtocol(&hsai_BlockA1, SAI_I2S_STANDARD, SAI_PROTOCOL_DATASIZE_24BIT, 2) != HAL_OK)
  {
    Error_Handler();
  }
  hsai_BlockB1.Instance = SAI1_Block_B;
  hsai_BlockB1.Init.AudioMode = SAI_MODESLAVE_RX;
  hsai_BlockB1.Init.Synchro = SAI_SYNCHRONOUS;
  hsai_BlockB1.Init.OutputDrive = SAI_OUTPUTDRIVE_DISABLE;
  hsai_BlockB1.Init.FIFOThreshold = SAI_FIFOTHRESHOLD_EMPTY;
  hsai_BlockB1.Init.SynchroExt = SAI_SYNCEXT_DISABLE;
  hsai_BlockB1.Init.MonoStereoMode = SAI_STEREOMODE;
  hsai_BlockB1.Init.CompandingMode = SAI_NOCOMPANDING;
  hsai_BlockB1.Init.TriState = SAI_OUTPUT_NOTRELEASED;
  if (HAL_SAI_InitProtocol(&hsai_BlockB1, SAI_I2S_STANDARD, SAI_PROTOCOL_DATASIZE_24BIT, 2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SAI1_Init 2 */

  /* USER CODE END SAI1_Init 2 */

}

/**
  * @brief TIM3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM3_Init(void)
{

  /* USER CODE BEGIN TIM3_Init 0 */

  /* USER CODE END TIM3_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM3_Init 1 */

  /* USER CODE END TIM3_Init 1 */
  htim3.Instance = TIM3;
  htim3.Init.Prescaler = 16000 - 1;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = 100-1;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim3, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_UPDATE;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM3_Init 2 */

  /* USER CODE END TIM3_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Stream0_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Stream0_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Stream0_IRQn);
  /* DMA1_Stream1_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Stream1_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Stream1_IRQn);
  /* DMA1_Stream2_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Stream2_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Stream2_IRQn);

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
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_9|GPIO_PIN_11, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOD, GPIO_Out_Bypass_Pin|GPIO_Out_Mute_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIO_Output_CODEC_RESET_GPIO_Port, GPIO_Output_CODEC_RESET_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pins : GPIO_INP_SW1_DOWN_Pin GPIO_input_Footswitch2_or_ledR_Pin */
  GPIO_InitStruct.Pin = GPIO_INP_SW1_DOWN_Pin|GPIO_input_Footswitch2_or_ledR_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : GPIO_INP_SW1_UP_Pin GPIO_INP_SW2_DOWN_Pin GPIO_INP_SW2_UP_Pin */
  GPIO_InitStruct.Pin = GPIO_INP_SW1_UP_Pin|GPIO_INP_SW2_DOWN_Pin|GPIO_INP_SW2_UP_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  /*Configure GPIO pins : PA9 PA11 */
  GPIO_InitStruct.Pin = GPIO_PIN_9|GPIO_PIN_11;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : GPIO_In_Footsw_Pin */
  GPIO_InitStruct.Pin = GPIO_In_Footsw_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIO_In_Footsw_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : GPIO_Out_Bypass_Pin GPIO_Out_Mute_Pin */
  GPIO_InitStruct.Pin = GPIO_Out_Bypass_Pin|GPIO_Out_Mute_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  /*Configure GPIO pin : GPIO_Output_CODEC_RESET_Pin */
  GPIO_InitStruct.Pin = GPIO_Output_CODEC_RESET_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIO_Output_CODEC_RESET_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

 /* MPU Configuration */

void MPU_Config(void)
{
  MPU_Region_InitTypeDef MPU_InitStruct = {0};

  /* Disables the MPU */
  HAL_MPU_Disable();

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Enable = MPU_REGION_ENABLE;
  MPU_InitStruct.Number = MPU_REGION_NUMBER0;
  MPU_InitStruct.BaseAddress = 0x30000000;
  MPU_InitStruct.Size = MPU_REGION_SIZE_128KB;
  MPU_InitStruct.SubRegionDisable = 0x0;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL0;
  MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);
  /* Enables the MPU */
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);

}

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
