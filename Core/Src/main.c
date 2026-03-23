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
#include "usb_device.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

#include "defines.h"
#include "stdarg.h"
#include "usb_device.h"
#include "usbd_cdc_if.h"
#include "stm32f4xx_hal.h" // Example for F4
#include "eeprom_emul_uint32_t.h"
#include "stepper.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

typedef enum {
    KEY_NONE,
    KEY_SEQUENCE,
    KEY_UP,
    KEY_DOWN,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_ESC,
    KEY_ENTER,
    KEY_BACKSPACE,
    KEY_CLEAR,
    KEY_F1,
    KEY_F2,
    KEY_F3,
    KEY_F4,
} KeyCode;

typedef enum {
    SEQ_IDLE,
    SEQ_ESC,
    SEQ_BRACKET,
    SEQ_O,
} SeqState;

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

#define dotTime       60
#define interTime     dotTime
#define dashTime      (3*dotTime)
#define spaceTime     (7*dotTime)

void dot();
void dash();
uint8_t morse(const char *format, ... );
uint16_t TxWrite(const uint8_t *data, uint16_t len);
static void TxDrain(void);

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
TIM_HandleTypeDef htim2;

/* USER CODE BEGIN PV */

//Externs
extern PCD_HandleTypeDef hpcd_USB_OTG_FS;

extern volatile int32_t  posSteps;
extern volatile uint8_t  posHomed;

/* EEPROM init status — set once by Stepper_LoadParams() at boot.
 * 0 = OK (magic present)   1 = blank (no data)   2 = orphaned (data, no magic) */
int eepromStatus = -1;

volatile uint8_t diagMode = 0;
volatile uint8_t buttonsEn = 0;   /* 0 = ignore button EXTI events — starts OFF, enabled after boot OK */
volatile uint8_t endstopsEn = 1;  /* 0 = ignore endstop EXTI events */
/* esBlocked removed — use ES_L_ACTIVE() / ES_R_ACTIVE() directly (defines.h) */
float rangeUsableMm = 0.0f;       /* usable travel range in mm, set by 'range' command, 0 = not measured */
static uint8_t cdcMoveActive = 0; /* set by CDC move commands, cleared on motor stop — suppresses jog/step prompt */
volatile uint32_t buzzTick = 0;
volatile uint8_t  buzzActive = 0;
volatile uint8_t  buzzRequest = 0;  /* set in ISR, handled in main loop */

/* Input snapshots — written by EXTI ISR, read by main loop and ProcessLine */
volatile uint32_t snapA = 0;  /* last GPIOA IDR snapshot */
volatile uint32_t snapB = 0;  /* last GPIOB IDR snapshot */

/* Non-blocking morse state machine */
typedef enum { MRS_IDLE, MRS_TONE, MRS_INTER, MRS_LETTER, MRS_LETTERGAP } MorseState;
static MorseState mrsState = MRS_IDLE;
static const char *mrsText = NULL;
static uint8_t mrsCharIdx = 0;
static uint8_t mrsSymIdx  = 0;
static uint32_t mrsTick   = 0;
extern USBD_HandleTypeDef hUsbDeviceFS;

/* ---- TX ring buffer --------------------------------------------------- */
#define TX_RING_SIZE  1024          /* must be power of 2 */
#define TX_RING_MASK  (TX_RING_SIZE - 1)
static uint8_t  txRing[TX_RING_SIZE];
static volatile uint16_t txHead = 0;   /* _write pushes here (main context) */
static volatile uint16_t txTail = 0;   /* TxDrain reads from here           */
static uint8_t  txChunk[64];           /* USB FS max packet = 64 bytes      */
static volatile uint8_t txBusy = 0;    /* 1 = CDC_Transmit_FS in flight     */

//
uint8_t rxRing[RX_RING_SIZE];
volatile uint16_t rxHead = 0;
volatile uint16_t rxTail = 0;
//
extern uint8_t CDC_IsConnected;

// Locals

params_t motorParams;

static uint8_t  lineBuf[128];
static uint16_t lineLen = 0;

//
uint32_t semaphore = 0;    // state machine flags

// morse code letters and digits
// __attribute__((section(".rodata"))) const char* morseCode[] = {
const char* morseCode[] = {
    "-----",            // 0
    ".----",            // 1
    "..---",            // 2
    "...--",            // 3
    "....-",            // 4
    ".....",            // 5
    "-....",            // 6
    "--...",            // 7
    "---..",            // 8
    "----.",            // 9
    ".-",               // A
    "-...",             // B
    "-.-.",             // C
    "-..",              // D
    ".",                // E
    "..-.",             // F
    "--.",              // G
    "....",             // H
    "..",               // I
    ".---",             // J
    "-.-",              // K
    ".-..",             // L
    "--",               // M
    "-.",               // N
    "---",              // O
    ".--.",             // P
    "--.-",             // Q
    ".-.",              // R
    "...",              // S
    "-",                // T
    "..-",              // U
    "...-",             // V
    ".--",              // W
    "-..-",             // X
    "-.--",             // Y
    "--..",             // Z
};   //morse code from A to Z

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_TIM2_Init(void);
/* USER CODE BEGIN PFP */
static void MX_TIM9_Init(void);  /* debounce timer — survives CubeMX regen here */
static volatile uint32_t tim9Ms; /* forward decl — defined+init'd near debounce code below */
void SafeState_And_Blink(void);
void ProcessEvents(void);
void RunHome(void);
void RunHomeEx(uint8_t fromButtons);
void RunCombo(void);
void RunRange(void);
void PrintPrompt(void);
void MorseStart(const char *text);
void MorseUpdate(void);
uint8_t MorseIsBusy(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

void PrintPrompt(void)
{
    if (posHomed) {
        float mm = (float)posSteps / (float)motorParams.spmm.u;
        printf("%7.2f > ", mm);
    } else {
        printf("XXXX.XX > ");
    }
}

//
KeyCode ParseKey(uint8_t byte)
{
    static SeqState state = SEQ_IDLE;

    switch (state)
    {
    case SEQ_IDLE:
        if      (byte == 0x1B) { state = SEQ_ESC;               return KEY_SEQUENCE; }
        else if (byte == '\r' || byte == '\n' || byte == 0x8D)  return KEY_ENTER;
        else if (byte == 0x7F || byte == 0x08 )         return KEY_BACKSPACE;
        else if (byte == 0x0C) {
            return KEY_CLEAR;  // Ctrl+L
        }
        break;

    case SEQ_ESC:
        if (byte == '[') { state = SEQ_BRACKET; return KEY_SEQUENCE; }
        else if (byte == 'O') { state = SEQ_O;       return KEY_SEQUENCE; }
        state = SEQ_IDLE;
        return KEY_SEQUENCE;  /* swallow unknown ESC+byte */

    case SEQ_O:
        state = SEQ_IDLE;
        if (byte == 'M') return KEY_ENTER;   // keypad Enter
        if (byte == 'P') return KEY_F1;      // bonus: F1-F4 also use ESC O
        if (byte == 'Q') return KEY_F2;
        if (byte == 'R') return KEY_F3;
        if (byte == 'S') return KEY_F4;
        break;

    case SEQ_BRACKET:
        /* CSI sequences: ESC [ (params) (final byte 0x40-0x7E)
           Eat intermediate/parameter bytes (0x20-0x3F) silently,
           only act on the final byte, ignore unknown finals. */
        if (byte >= 0x20 && byte <= 0x3F) return KEY_SEQUENCE;  /* param/intermediate — stay in state */
        state = SEQ_IDLE;
        if      (byte == 'A') return KEY_UP;
        else if (byte == 'B') return KEY_DOWN;
        else if (byte == 'C') return KEY_RIGHT;
        else if (byte == 'D') return KEY_LEFT;
        return KEY_SEQUENCE;  /* unknown final — swallow it */
    }

    return KEY_NONE;
}

// my memset
void volatile_memset(volatile void *s, int c, size_t n) {
    volatile unsigned char *p = (volatile unsigned char *)s;
    while (n-- > 0) {
        *p++ = (unsigned char)c;
    }
}

//
void dumpVars()
{
    // readVariables();
    // printf("----------%08d-----\r\n", debugonly++);
    printf("Dump of NVARS in EEPROM\r\n");
    printf("-----------------------\r\n");
    printf("mmpsmax........: %7.3f\r\n", motorParams.mmpsmax.f );
    printf("mmpsmin........: %7.3f\r\n", motorParams.mmpsmin.f );
    printf("dvdtacc........: %7.3f\r\n", motorParams.dvdtacc.f );
    printf("dvdtdecc.......: %7.3f\r\n", motorParams.dvdtdecc.f );
    printf("jogmm..........: %7.3f\r\n", motorParams.jogmm.f );
    printf("stepmm.........: %7.3f\r\n", motorParams.stepmm.f );
    printf("spmm...........: %7ld\r\n",  motorParams.spmm.u );
    printf("-----------------------\r\n");
    printf("semaphore....:  %ld\r\n", semaphore);
    printf("SEM_EL.......:  %d\r\n", SEM_EL);
    printf("SEM_ER.......:  %d\r\n", SEM_ER);
    printf("SEM_JOGL.....:  %d\r\n", SEM_JOGL);
    printf("SEM_JOGR.....:  %d\r\n", SEM_JOGR);
    printf("SEM_JOGSTEPL.:  %d\r\n", SEM_JOGSTEPL);
    printf("SEM_JOGSTEPR.:  %d\r\n", SEM_JOGSTEPR);
    printf("-----------------------\r\n");
    printf("DB_JOGL......:  %d\r\n", DB_JOGL);
    printf("DB_JOGR......:  %d\r\n", DB_JOGR);
    printf("DB_STEPL.....:  %d\r\n", DB_STEPL);
    printf("DB_STEPR.....:  %d\r\n", DB_STEPR);
    printf("DB_EL........:  %d\r\n", DB_EL);
    printf("DB_ER........:  %d\r\n", DB_ER);
    printf("DB_EE........:  %d\r\n", DB_EE);
    printf("-----------------------\r\n");
}

// morse dot function
void dot()
{
    HAL_GPIO_WritePin(LED_USER_GPIO_Port, LED_USER_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(BUZZ_GPIO_Port, BUZZ_Pin, GPIO_PIN_RESET);

    HAL_Delay(dotTime);

    HAL_GPIO_WritePin(LED_USER_GPIO_Port, LED_USER_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(BUZZ_GPIO_Port, BUZZ_Pin, GPIO_PIN_SET);

    HAL_Delay(interTime);
}

// morse dash function
void dash()
{
    HAL_GPIO_WritePin(LED_USER_GPIO_Port, LED_USER_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(BUZZ_GPIO_Port, BUZZ_Pin, GPIO_PIN_RESET);

    HAL_Delay(dashTime);

    HAL_GPIO_WritePin(BUZZ_GPIO_Port, BUZZ_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(LED_USER_GPIO_Port, LED_USER_Pin, GPIO_PIN_SET);

    HAL_Delay(interTime);
}

/* Non-blocking morse — call MorseStart() once, MorseUpdate() from main loop */
void MorseStart(const char *text)
{
    mrsText = text;
    mrsCharIdx = 0;
    mrsSymIdx  = 0;
    mrsState   = MRS_LETTER;  /* will pick first char on next update */
    mrsTick    = tim9Ms;
}

uint8_t MorseIsBusy(void) { return mrsState != MRS_IDLE; }

void MorseUpdate(void)
{
    if (mrsState == MRS_IDLE) return;
    uint32_t now = tim9Ms;
    uint32_t elapsed = now - mrsTick;

    switch (mrsState)
    {
    case MRS_LETTER: {
        /* Pick next character */
        char ch = mrsText[mrsCharIdx];
        if (ch == '\0') {
            /* Done */
            HAL_GPIO_WritePin(BUZZ_GPIO_Port, BUZZ_Pin, GPIO_PIN_SET);
            HAL_GPIO_WritePin(LED_USER_GPIO_Port, LED_USER_Pin, GPIO_PIN_SET);
            mrsState = MRS_IDLE;
            return;
        }
        mrsSymIdx = 0;
        /* Get morse pattern for this character */
        const char *pattern = NULL;
        if (ch >= 'A' && ch <= 'Z')      pattern = morseCode[ch - 'A' + 10];
        else if (ch >= 'a' && ch <= 'z')  pattern = morseCode[ch - 'a' + 10];
        else if (ch >= '0' && ch <= '9')  pattern = morseCode[ch - '0'];
        if (!pattern || pattern[0] == '\0') {
            /* Unknown char or space — just pause */
            mrsTick = now;
            mrsCharIdx++;
            return;
        }
        /* Start first tone */
        HAL_GPIO_WritePin(BUZZ_GPIO_Port, BUZZ_Pin, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(LED_USER_GPIO_Port, LED_USER_Pin, GPIO_PIN_RESET);
        mrsTick = now;
        mrsState = MRS_TONE;
        break;
    }
    case MRS_TONE: {
        /* Currently sounding — wait for dot or dash duration */
        const char *pattern = NULL;
        char ch = mrsText[mrsCharIdx];
        if (ch >= 'A' && ch <= 'Z')      pattern = morseCode[ch - 'A' + 10];
        else if (ch >= 'a' && ch <= 'z')  pattern = morseCode[ch - 'a' + 10];
        else if (ch >= '0' && ch <= '9')  pattern = morseCode[ch - '0'];
        uint32_t dur = (pattern[mrsSymIdx] == '-') ? dashTime : dotTime;
        if (elapsed >= dur) {
            /* Tone done — silence */
            HAL_GPIO_WritePin(BUZZ_GPIO_Port, BUZZ_Pin, GPIO_PIN_SET);
            HAL_GPIO_WritePin(LED_USER_GPIO_Port, LED_USER_Pin, GPIO_PIN_SET);
            mrsSymIdx++;
            mrsTick = now;
            if (pattern[mrsSymIdx] == '\0') {
                /* Letter done — inter-letter gap (3 dot times) */
                mrsCharIdx++;
                mrsTick = now;
                mrsState = MRS_LETTERGAP;
            } else {
                mrsState = MRS_INTER;
            }
        }
        break;
    }
    case MRS_INTER:
        /* Inter-symbol gap */
        if (elapsed >= interTime) {
            /* Start next tone */
            HAL_GPIO_WritePin(BUZZ_GPIO_Port, BUZZ_Pin, GPIO_PIN_RESET);
            HAL_GPIO_WritePin(LED_USER_GPIO_Port, LED_USER_Pin, GPIO_PIN_RESET);
            mrsTick = now;
            mrsState = MRS_TONE;
        }
        break;
    case MRS_LETTERGAP: {
        /* Inter-letter gap: 3 dot times; word gap (space): 7 dot times */
        uint32_t gap = 3 * dotTime;
        if (mrsText[mrsCharIdx] == ' ') gap = 7 * dotTime;
        if (elapsed >= gap) {
            if (mrsText[mrsCharIdx] == ' ') mrsCharIdx++;  /* skip the space */
            mrsState = MRS_LETTER;
        }
        break;
    }
    case MRS_IDLE:
    default:
        break;
    }
}

/* Push bytes into TX ring buffer, kick USB if idle.
 * Blocks only when ring is full (with 100ms timeout). */
int _write(int file, char *ptr, int len)
{
    UNUSED(file);

    int sent = 0;
    uint32_t timeout = tim9Ms;

    while (sent < len) {
        uint16_t next = (txHead + 1) & TX_RING_MASK;
        if (next == txTail) {
            /* ring full — kick drain and spin until space or timeout */
            TxDrain();
            if (tim9Ms - timeout > 100) return sent;
            continue;
        }
        txRing[txHead] = (uint8_t)ptr[sent];
        txHead = next;
        sent++;
        timeout = tim9Ms;  /* reset timeout on progress */
    }

    TxDrain();  /* kick USB if idle */
    return sent;
}

// Morse code transmitter
uint8_t morse(const char *format, ... )
{
    uint8_t result = 0;

    unsigned char lettInMorse[8];

    uint8_t cnt2 = 0;

    uint8_t symbol;
    // uint8_t tmpsymbol;           //needed for CW printout in console

    while ( (symbol = format[cnt2]) > 0)
    {
        // end of string
        if ( symbol == 0x00 ) {
            break;
        }

        // check for alhpa
        if ( ( symbol > 90 ) | ( symbol < 65 ) ) {
          // check for digits
          if ( ( symbol > 57 ) | ( symbol < 48 ) ) {
            if ( symbol != 32 ) {
              BKPT;
              break;
            }
          }
        }

        // convert to pseudoascii
        if ( ( symbol <= 57 ) & ( symbol >= 48 ) ) {
          symbol -= 48;
        }

        // convert to pseudoascii
        if ( ( symbol <= 90 ) & ( symbol >= 65 ) ) {
          symbol -= 55;
        }
        // space handling
        if (symbol == 32) {
            HAL_Delay(spaceTime);
            cnt2++;
            continue;
        }

        uint8_t index = 0;        //indexLett in letter

        while (1) {
            lettInMorse[index] = morseCode[symbol][index];
            if (lettInMorse[index] == 0) {        // end of string
                break;
            }
            if (lettInMorse[index] == '.') {      //dot
                dot();
            }
            if (lettInMorse[index] == '-') {      //dash
                dash();
            }
            index++;
        }
        HAL_Delay(spaceTime);
        cnt2++;
    }
    return result;
}

//
// USB Physical disconnection — safety net: clear CDC_IsConnected if host
// disconnects without sending SET_CONTROL_LINE_STATE first.
void My_PCD_DisconnectCallback(PCD_HandleTypeDef *hpcd)
{
    CDC_IsConnected = 0;
    USBD_LL_DevDisconnected((USBD_HandleTypeDef *)hpcd->pData);
}

/* Copy up to 64 bytes from ring into txChunk and fire CDC_Transmit_FS.
 * Called from _write (main) and DataInCallback (ISR) — txBusy serializes. */
static void TxDrain(void)
{
    if (txBusy) return;
    uint16_t head = txHead;
    uint16_t tail = txTail;
    if (head == tail) return;  /* nothing to send */

    /* copy contiguous chunk from ring (up to 64 bytes = USB FS max packet) */
    uint16_t n = 0;
    while (tail != head && n < sizeof(txChunk)) {
        txChunk[n++] = txRing[tail];
        tail = (tail + 1) & TX_RING_MASK;
    }

    txBusy = 1;
    txTail = tail;  /* advance tail BEFORE transmit — prevents ISR re-send race */
    if (CDC_Transmit_FS(txChunk, n) != USBD_OK) {
        txBusy = 0;     /* transmit failed — data lost (USB link broken anyway) */
    }
}


/* Read one byte from RX buffer. Call RxAvailable() first. */
uint8_t RxAvailable(void)
{
    return rxHead != rxTail;
}

uint8_t RxRead(void)
{
    uint8_t byte = rxRing[rxTail];
    rxTail = (rxTail + 1) & RX_RING_MASK;
    return byte;
}

/* Queue bytes into TX ring. Returns number of bytes actually queued.
 * Non-blocking — drops bytes if ring is full. */
uint16_t TxWrite(const uint8_t *data, uint16_t len)
{
    uint16_t sent = 0;
    while (sent < len) {
        uint16_t next = (txHead + 1) & TX_RING_MASK;
        if (next == txTail) break;  /* ring full */
        txRing[txHead] = data[sent++];
        txHead = next;
    }
    TxDrain();
    return sent;
}

// DataIn is from DEVICE to HOST
void MyPCD_DataInStageCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
{
    USBD_LL_DataInStage((USBD_HandleTypeDef *)hpcd->pData, epnum,
                        hpcd->IN_ep[epnum].xfer_buff);

    if (epnum == 0x01)
    {
        txBusy = 0;
        TxDrain();    /* send next chunk from ring if available */
    }
}

// Debug some USB options
void debugStruc()
{
    USB_CfgTypeDef *usb = &hpcd_USB_OTG_FS.Init;
    printf("#########################################\r\n");
    printf("sizeof float is %d bytes\r\n", sizeof(float));
    printf("-----------------------------------------\r\n");
    printf("hpcd_USB_OTG_FS.Init.dev_endpoints:%d\r\n", usb->dev_endpoints);
    printf("hpcd_USB_OTG_FS.Init.Host_channels:%d\r\n", usb->Host_channels);
    printf("hpcd_USB_OTG_FS.Init.dma_enable:%d\r\n", usb->dma_enable);
    printf("hpcd_USB_OTG_FS.Init.speed:%d\r\n", usb->speed);
    printf("hpcd_USB_OTG_FS.Init.ep0_mps:%d\r\n", usb->ep0_mps);
    printf("hpcd_USB_OTG_FS.Init.phy_itface:%d\r\n", usb->phy_itface);
    printf("hpcd_USB_OTG_FS.Init.Sof_enable:%d\r\n", usb->Sof_enable);
    printf("hpcd_USB_OTG_FS.Init.low_power_enable:%d\r\n", usb->low_power_enable);
    printf("hpcd_USB_OTG_FS.Init.lpm_enable:%d\r\n", usb->lpm_enable);
    printf("hpcd_USB_OTG_FS.Init.battery_charging_enable:%d\r\n", usb->battery_charging_enable);
    printf("hpcd_USB_OTG_FS.Init.vbus_sensing_enable:%d\r\n", usb->vbus_sensing_enable);
    printf("hpcd_USB_OTG_FS.Init.use_dedicated_ep1:%d\r\n", usb->use_dedicated_ep1);
    printf("hpcd_USB_OTG_FS.Init.use_external_vbus:%d\r\n", usb->use_external_vbus);
}

void ProcessLine(void)
{
    char  cmd[16];
    char  param[16];
    float fval;
    int   ival;

    if (sscanf((char *)lineBuf, "set %s %f", param, &fval) == 2)
    {
        Stepper_SetParam(param, fval);
    }
    else if (sscanf((char *)lineBuf, "move %f", &fval) == 1)
    {
        if      (!(snapA & ES_L_Pin) && fval < 0) { buzzRequest = 1; printf("blocked: ES_L\r\n"); }
        else if (!(snapA & ES_R_Pin) && fval > 0) { buzzRequest = 1; printf("blocked: ES_R\r\n"); }
        else { cdcMoveActive = 1; Stepper_Move(fval); }
    }
    else if (sscanf((char *)lineBuf, "movel %f", &fval) == 1)
    {
        if (!(snapA & ES_L_Pin)) { buzzRequest = 1; printf("blocked: ES_L\r\n"); }
        else { cdcMoveActive = 1; Stepper_Move(-fval); }
    }
    else if (sscanf((char *)lineBuf, "mover %f", &fval) == 1)
    {
        if (!(snapA & ES_R_Pin)) { buzzRequest = 1; printf("blocked: ES_R\r\n"); }
        else { cdcMoveActive = 1; Stepper_Move(fval); }
    }
    else if (sscanf((char *)lineBuf, "steps %d", &ival) == 1)
    {
        if      (!(snapA & ES_L_Pin) && ival < 0) { buzzRequest = 1; printf("blocked: ES_L\r\n"); }
        else if (!(snapA & ES_R_Pin) && ival > 0) { buzzRequest = 1; printf("blocked: ES_R\r\n"); }
        else { cdcMoveActive = 1; Stepper_MoveSteps(ival); }
    }
    else if (sscanf((char *)lineBuf, "moveto %f", &fval) == 1)
    {
        if (!posHomed) {
            printf("\r\nmoveto: not homed\r\n");
        } else if (rangeUsableMm <= 0.0f) {
            printf("\r\nmoveto: range not measured — run 'range' first\r\n");
        } else if (fval < 0.0f || fval > rangeUsableMm) {
            buzzRequest = 1;
            printf("\r\nmoveto: %.2f out of range [0.00 .. %.2f]\r\n", fval, rangeUsableMm);
        } else {
            float currentMm = (float)posSteps / (float)motorParams.spmm.u;
            cdcMoveActive = 1;
            Stepper_Move(fval - currentMm);
        }
    }
    else if (sscanf((char *)lineBuf, "%s", cmd) == 1)
    {
        if      (strcmp(cmd, "stop")   == 0) Stepper_Stop();
        else if (strcmp(cmd, "params") == 0) Stepper_DumpParams();
        else if (strcmp(cmd, "save")       == 0) { if (Stepper_SaveParams()) buzzRequest = 1; }
        else if (strcmp(cmd, "initeeprom") == 0) { Stepper_InitDefaults(); buzzRequest = 1; }
        else if (strcmp(cmd, "dump")   == 0) dumpVars();
        else if (strcmp(cmd, "cls")    == 0) { printf("\033[2J\033[H"); PrintPrompt(); }
        else if (strcmp(cmd, "uptime") == 0) printf("uptime: %lu ms\r\n", HAL_GetTick());
        else if (strcmp(cmd, "reset")  == 0) { printf("\033[2J\033[H"); HAL_Delay(50); NVIC_SystemReset(); }
        else if (strcmp(cmd, "diag_inputs") == 0 || strcmp(cmd, "di") == 0) {
            diagMode = !diagMode;
            printf("diag_inputs %s\r\n", diagMode ? "ON" : "OFF");
        }
        else if (strcmp(cmd, "combo")  == 0) RunCombo();
        else if (strcmp(cmd, "home")   == 0) RunHome();
        else if (strcmp(cmd, "range")  == 0) RunRange();
        else if (strncmp((char *)lineBuf, "morse ", 6) == 0) {
            static char morseBuf[64];
            strncpy(morseBuf, (char *)lineBuf + 6, sizeof(morseBuf) - 1);
            morseBuf[sizeof(morseBuf) - 1] = '\0';
            MorseStart(morseBuf);
            printf("morse: %s\r\n", morseBuf);
        }
        else if (strcmp((char *)lineBuf, "buttons on")  == 0) { buttonsEn = 1; printf("buttons ON\r\n"); }
        else if (strcmp((char *)lineBuf, "buttons off") == 0) { buttonsEn = 0; printf("buttons OFF\r\n"); }
        else if (strcmp((char *)lineBuf, "endstops on")  == 0) { endstopsEn = 1; printf("endstops ON\r\n"); }
        else if (strcmp((char *)lineBuf, "endstops off") == 0) { endstopsEn = 0; printf("endstops OFF\r\n"); }
        else if (strcmp(cmd, "diag_outputs") == 0 || strcmp(cmd, "do") == 0) {
            printf("password: ");
        }
        else if (strcmp(cmd, "motorola") == 0) {
            printf("diag_outputs: 16000 steps R/L @ 1kHz (reset to stop)\r\n");
            TIM_OC_InitTypeDef sConfig = {0};
            sConfig.OCMode     = TIM_OCMODE_PWM1;
            sConfig.OCPolarity = TIM_OCPOLARITY_HIGH;
            sConfig.OCFastMode = TIM_OCFAST_DISABLE;
            sConfig.Pulse      = 48000;
            HAL_TIM_PWM_ConfigChannel(&htim2, &sConfig, TIM_CHANNEL_3);
            __HAL_TIM_SET_AUTORELOAD(&htim2, 96000 - 1);  /* 1kHz */
            __HAL_TIM_SET_COUNTER(&htim2, 0);
            while (1) {
                HAL_GPIO_WritePin(DIR_GPIO_Port, DIR_Pin, GPIO_PIN_SET);
                HAL_Delay(1);
                HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_3);
                HAL_Delay(16000);  /* 16000 pulses @ 1kHz = 16s */
                HAL_TIM_PWM_Stop(&htim2, TIM_CHANNEL_3);
                HAL_Delay(500);

                HAL_GPIO_WritePin(DIR_GPIO_Port, DIR_Pin, GPIO_PIN_RESET);
                HAL_Delay(1);
                HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_3);
                HAL_Delay(16000);
                HAL_TIM_PWM_Stop(&htim2, TIM_CHANNEL_3);
                HAL_Delay(500);
            }
        }
        else if (strcmp(cmd, "help")   == 0)
            printf("commands:\r\n"
                      "  move <mm>          move by mm\r\n"
                      "  movel <mm>         move left by mm\r\n"
                      "  mover <mm>         move right by mm\r\n"
                      "  steps <n>          move by steps\r\n"
                      "  set mmpsmax  <f>   max velocity mm/s\r\n"
                      "  set mmpsmin  <f>   min velocity mm/s\r\n"
                      "  set dvdtacc  <f>   accel mm/s2\r\n"
                      "  set dvdtdecc <f>   decel mm/s2\r\n"
                      "  set jogmm    <f>   jog distance mm\r\n"
                      "  set stepmm   <f>   step distance mm\r\n"
                      "  set spmm     <n>   steps per mm\r\n"
                      "  moveto <mm>        move to absolute position (needs home+range)\r\n"
                      "  range              measure travel range, return to 0\r\n"
                      "  params, save, dump, stop, combo, cls, uptime, reset\r\n"
                      "  diag_inputs (di)   toggle button/endstop diag mode\r\n"
                      "  diag_outputs (do)  PULSE+DIR test loop (reset to stop)\r\n"
                      "buttons:\r\n"
                      "  JOGL/R short       Jog(jogmm) with ramps\r\n"
                      "  JOGL/R hold>300ms  RunContinuous(mmpsmax) until release/endstop\r\n"
                      "  STEPL/R            Move(stepmm) with ramps\r\n"
                      "  ES_L/R             emergency stop (immediate decel)\r\n");
        else
            printf(" unknown: %s\r\n", cmd);
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

  // fire up buzzer for diagnostics. before main we are silencing it
  HAL_GPIO_WritePin(BUZZ_GPIO_Port, BUZZ_Pin, GPIO_PIN_RESET);

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
  MX_USB_DEVICE_Init();
  MX_TIM2_Init();
  /* USER CODE BEGIN 2 */
  MX_TIM9_Init();  /* debounce timer — must remain here after CubeMX regen */

  /* ── Firmware CRC self-check — first thing, before any peripherals ──────
   * Block at 0x0803FF80: 31 x uint32 label + 1 x uint32 CRC (128 bytes).
   * CRC covers [0x08000000 .. 0x0803FF80) = 65504 words (program + 0xFF pad).
   * "." in morse = OK.  "SOS" + halt = flash corruption detected.
   * Label and CRC patched by scripts/crc_patch.py after every build.      */
  {
      extern const struct { char label[124]; uint32_t crc; } __fw_info;
      __HAL_RCC_CRC_CLK_ENABLE();
      CRC->CR = CRC_CR_RESET;
      const uint32_t *p   = (const uint32_t *)0x08000000U;
      const uint32_t *end = (const uint32_t *)&__fw_info;   /* 0x0803FF80 */
      while (p < end) { CRC->DR = *p++; }
      uint32_t computed = CRC->DR;
      if (computed == __fw_info.crc) {
          morse("E");          /* E = dit = single short beep = CRC OK */
      } else {
          while (1) { morse("SOS"); HAL_Delay(2000); }   /* halt */
      }
  }

  setvbuf(stdout, NULL, _IONBF, 0);  /* disable buffering entirely */

  /* Override disconnect only: clear CDC_IsConnected as safety net
   * (host may disconnect without SET_CONTROL_LINE_STATE).
   * All other PCD callbacks use usbd_conf.c defaults. */
  HAL_PCD_RegisterCallback(&hpcd_USB_OTG_FS, HAL_PCD_DISCONNECT_CB_ID, My_PCD_DisconnectCallback);
  HAL_PCD_RegisterDataInStageCallback(&hpcd_USB_OTG_FS, MyPCD_DataInStageCallback);

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */

  // silence buzzer
  HAL_GPIO_WritePin(BUZZ_GPIO_Port, BUZZ_Pin, GPIO_PIN_SET);

  if ( EEPROM_Init() != EEPROM_OK ) {
      BKPT;
  }

  // USB enumeration
  HAL_Delay(1200);

  /* clear RX ring buffer and line buffer */
  rxHead  = 0;
  rxTail  = 0;
  lineLen = 0;
  lineBuf[0] = '\0';

  eepromStatus = Stepper_LoadParams();
  Stepper_ValidateParams();
  Stepper_Init(&htim2);

  morse("V");

  //printf("\033[2J\033[H");  /* clear screen — disabled: boot log (CRC, EEPROM) must remain visible */
  printf("\r\n===============================================\r\n");
  printf("  stepper_sc  %s  %s\r\n", GIT_HASH, BUILD_DATE);
  printf("  STM32F411CEU6 @ 96 MHz\r\n");
  printf("  type 'help' for commands\r\n");
  printf("===============================================\r\n");

  Stepper_DumpParams();

  if (eepromStatus == 0) {
      printf("  EEPROM OK — params loaded from flash\r\n");
  } else if (eepromStatus == 2) {
      printf("  EEPROM has data but magic absent — type 'save' to confirm\r\n");
  } else {
      /* eepromStatus == 1: truly blank — block until user decides */
      printf("\r\n*** EEPROM blank — motor DISABLED until answered ***\r\n");
      printf("*** Init with defaults? [y/n]                    ***\r\n");
      char ans = 0;
      uint32_t dotTick = tim9Ms;
      while (1) {
          if (rxHead != rxTail) {
              ans = (char)rxRing[rxTail];
              rxTail = (rxTail + 1) & RX_RING_MASK;
              if (ans == 'y' || ans == 'Y' || ans == 'n' || ans == 'N') break;
          }
          if (tim9Ms - dotTick >= 1000) {
              printf("  waiting for [y/n]...\r\n");
              dotTick = tim9Ms;
          }
      }
      if (ans == 'y' || ans == 'Y') {
          Stepper_InitDefaults();
          buzzRequest = 1;
      } else {
          printf("skipped — defaults in RAM only, not saved\r\n");
      }
  }

  morse("G");

  PrintPrompt();

  /* Flush any bytes minicom sent during boot (init strings, ESC queries) */
  rxHead = 0;
  rxTail = 0;
  lineLen = 0;
  lineBuf[0] = '\0';

  MorseStart("Z");

  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

    /* Heartbeat LED — toggle every 500ms */
    {
        static uint32_t ledTick = 0;
        uint32_t now = tim9Ms;
        if (now - ledTick >= 500) {
            ledTick = now;
            HAL_GPIO_TogglePin(LED_USER_GPIO_Port, LED_USER_Pin);
        }
    }

    /* Print prompt when motor stops (jog, step, CDC move) */
    {
        static uint8_t wasBusy = 0;
        uint8_t busy = Stepper_IsBusy();
        if (wasBusy && !busy) {
            if (cdcMoveActive) {
                printf("\r\n\r\n");
                PrintPrompt();
                cdcMoveActive = 0;
            } else if (motorParams.debug.u & 1) {
                PrintPrompt();
            }
        }
        wasBusy = busy;
    }

    /* Non-blocking morse */
    MorseUpdate();

    /* Boot sequence: Z → 3s delay → check inputs → OK → enable buttons
       If any button/endstop is stuck LOW at boot, play CQ CQ CQ DE LZ1CCM
       in a loop until all inputs are released. */
    {
        static uint8_t bootPhase = 0;  /* 0=wait Z, 1=delay, 2=check, 3=wait CQ, 4=pause, 5=play OK, 6=done */
        static uint32_t bootTick = 0;
        switch (bootPhase) {
        case 0: /* wait for Z to finish */
            if (!MorseIsBusy()) { bootTick = tim9Ms; bootPhase = 1; }
            break;
        case 1: /* 3s delay */
            if (tim9Ms - bootTick >= 3000) bootPhase = 2;
            break;
        case 2: { /* check all inputs — active LOW = stuck */
            uint8_t stuck = 0;
            /* ES_L skipped — motor may be parked on home switch */
            if (!HAL_GPIO_ReadPin(ES_R_GPIO_Port, ES_R_Pin))         { stuck = 1; printf("STUCK: ES_R\r\n"); }
            if (!HAL_GPIO_ReadPin(BUTT_JOGL_GPIO_Port, BUTT_JOGL_Pin)) { stuck = 1; printf("STUCK: JOGL\r\n"); }
            if (!HAL_GPIO_ReadPin(BUTT_JOGR_GPIO_Port, BUTT_JOGR_Pin)) { stuck = 1; printf("STUCK: JOGR\r\n"); }
            if (!HAL_GPIO_ReadPin(BUTT_STEPL_GPIO_Port, BUTT_STEPL_Pin)) { stuck = 1; printf("STUCK: STEPL\r\n"); }
            if (!HAL_GPIO_ReadPin(BUTT_STEPR_GPIO_Port, BUTT_STEPR_Pin)) { stuck = 1; printf("STUCK: STEPR\r\n"); }
            if (stuck) {
                MorseStart("CQ CQ CQ DE LZ1CCM");
                bootPhase = 3;
            } else {
                MorseStart("OK");
                bootPhase = 5;
            }
            break;
        }
        case 3: /* wait CQ to finish */
            if (!MorseIsBusy()) { bootTick = tim9Ms; bootPhase = 4; }
            break;
        case 4: /* pause 2s then re-check */
            if (tim9Ms - bootTick >= 2000) bootPhase = 2;
            break;
        case 5: /* wait OK to finish */
            if (!MorseIsBusy()) { buttonsEn = 1; printf("\r\nbuttons enabled\r\n"); PrintPrompt(); bootPhase = 6; }
            break;
        default: break;
        }
    }

    /* Buzzer: handle request from ISR (only if morse not active) */
    if (buzzRequest && !MorseIsBusy()) {
        HAL_GPIO_WritePin(BUZZ_GPIO_Port, BUZZ_Pin, GPIO_PIN_RESET);
        buzzTick = tim9Ms;
        buzzActive = 1;
        buzzRequest = 0;
    }

    /* Buzzer off after 50ms */
    if (buzzActive && (tim9Ms - buzzTick >= 50)) {
        HAL_GPIO_WritePin(BUZZ_GPIO_Port, BUZZ_Pin, GPIO_PIN_SET);
        buzzActive = 0;
    }

    /* Process button/endstop events from ISR */
    ProcessEvents();

    /* Drain RX buffer */
    while (RxAvailable())
    {
        uint8_t b = RxRead();

        KeyCode key = ParseKey(b);

        switch (key)
        {
        case KEY_UP:
            /* handle up    */
            printf("KEY_UP pressed.\r\n");
            break;
        case KEY_DOWN:
            /* handle down  */
            printf("KEY_DOWN pressed.\r\n");
            break;
        case KEY_LEFT:
            /* handle left  */
            printf("KEY_LEFT pressed.\r\n");
            break;
        case KEY_RIGHT:
            /* handle right */
            printf("KEY_RIGHT pressed.\r\n");
            break;
        case KEY_ENTER:
            printf("\r\n");
            if (lineLen > 0)
            {
                lineBuf[lineLen] = '\0';
                ProcessLine();
                lineLen = 0;
            }
            PrintPrompt();
            break;
        case KEY_BACKSPACE:
            if (lineLen > 0)
            {
                lineLen--;
                printf("\b \b");
            }
            break;
        case KEY_CLEAR:
            printf("\033[2J\033[H> %.*s", lineLen, lineBuf);
            break;
        case KEY_NONE:
            /* KEY_SEQUENCE is ignored, only true printable bytes reach here */
            if (b >= 0x20 && b < 0x7F && lineLen < sizeof(lineBuf) - 1)
            {
                lineBuf[lineLen++] = b;
                printf("%c", b);
            }
            break;
        case KEY_F1:  printf("F1\r\n");  break;
        case KEY_F2:  printf("F2\r\n");  break;
        case KEY_F3:  printf("F3\r\n");  break;
        case KEY_F4:  printf("F4\r\n");  break;

        default: break;
        }
    }

    // morse("C");
    // debugStruc();
    // dumpVars();
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

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 25;
  RCC_OscInitStruct.PLL.PLLN = 192;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
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

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_3) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief TIM2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM2_Init(void)
{

  /* USER CODE BEGIN TIM2_Init 0 */

  /* USER CODE END TIM2_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM2_Init 1 */

  /* USER CODE END TIM2_Init 1 */
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 0;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 9599;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_PWM_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 4800;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM2_Init 2 */

  /* USER CODE END TIM2_Init 2 */
  HAL_TIM_MspPostInit(&htim2);

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
  HAL_GPIO_WritePin(LED_USER_GPIO_Port, LED_USER_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, DIR_Pin|BUZZ_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : LED_USER_Pin */
  GPIO_InitStruct.Pin = LED_USER_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED_USER_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : ES_L_Pin ES_R_Pin BUTT_JOGL_Pin BUTT_JOGR_Pin */
  GPIO_InitStruct.Pin = ES_L_Pin|ES_R_Pin|BUTT_JOGL_Pin|BUTT_JOGR_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : BUTT_STEPL_Pin BUTT_STEPR_Pin */
  GPIO_InitStruct.Pin = BUTT_STEPL_Pin|BUTT_STEPR_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pins : DIR_Pin BUZZ_Pin */
  GPIO_InitStruct.Pin = DIR_Pin|BUZZ_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI0_IRQn, 2, 0);
  HAL_NVIC_EnableIRQ(EXTI0_IRQn);

  HAL_NVIC_SetPriority(EXTI1_IRQn, 2, 0);
  HAL_NVIC_EnableIRQ(EXTI1_IRQn);

  HAL_NVIC_SetPriority(EXTI3_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI3_IRQn);

  HAL_NVIC_SetPriority(EXTI4_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI4_IRQn);

  HAL_NVIC_SetPriority(EXTI9_5_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);

/* USER CODE BEGIN MX_GPIO_Init_2 */
/* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* TIM9 — debounce confirmation timer, 1 ms free-running.
 * No HAL handle needed: ISR accesses TIM9 registers directly.
 * TIM9 is on APB2 (96 MHz). PSC=95 → 1 MHz tick. ARR=999 → 1 ms period.
 * IRQ vector: TIM1_BRK_TIM9_IRQn (shared with TIM1 break, unused here).
 * Function body here (USER CODE 4) so CubeMX regeneration does not erase it. */
static void MX_TIM9_Init(void)
{
    __HAL_RCC_TIM9_CLK_ENABLE();
    TIM9->PSC  = 95;             /* 96 MHz / 96 = 1 MHz */
    TIM9->ARR  = 999;            /* 1 MHz / 1000 = 1 ms period */
    TIM9->CNT  = 0;
    TIM9->EGR  = TIM_EGR_UG;    /* load PSC/ARR immediately */
    TIM9->SR   = 0;              /* clear UIF set by EGR_UG */
    TIM9->DIER = TIM_DIER_UIE;  /* update interrupt enable */
    HAL_NVIC_SetPriority(TIM1_BRK_TIM9_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(TIM1_BRK_TIM9_IRQn);
    TIM9->CR1  = TIM_CR1_CEN;   /* start */
}

/* ---- Button/endstop event flags (set in ISR, processed in main) ------- */
#define DEBOUNCE_MS       30   /* press debounce */
#define DEBOUNCE_REL_MS   50   /* release lockout (switches bounce more on release) */

#define EVT_ES_L      (1U << 0)
#define EVT_ES_R      (1U << 1)
#define EVT_JOGL_DN   (1U << 2)
#define EVT_JOGL_UP   (1U << 3)
#define EVT_JOGR_DN   (1U << 4)
#define EVT_JOGR_UP   (1U << 5)
#define EVT_STEPL     (1U << 6)
#define EVT_STEPR     (1U << 7)
#define EVT_HOME      (1U << 8)  /* home combo: ES_L hit + JOGL held + JOGR pressed */
/* Debounce-in-progress flags — EXTI fired but inside debounce window; main loop ignores */
#define EVT_DB_JOGL   (1U << 9)
#define EVT_DB_JOGR   (1U << 10)
#define EVT_DB_STEPL  (1U << 11)
#define EVT_DB_STEPR  (1U << 12)

#define JOG_HOLD_MS   300

static volatile uint32_t evtFlags = 0;
static volatile uint32_t lastTick_jogL  = 0;
static volatile uint32_t lastTick_jogR  = 0;
static volatile uint32_t lastTick_stepL = 0;
static volatile uint32_t lastTick_stepR = 0;
static volatile uint32_t lastTick_esL   = 0;
static volatile uint32_t lastTick_esR   = 0;

/* Jog state machine */
typedef enum { JOG_IDLE, JOG_PRESSED, JOG_STEP, JOG_CONT } JogState;
static volatile JogState jogStateL = JOG_IDLE;
static volatile JogState jogStateR = JOG_IDLE;
static volatile uint32_t jogPressTickL = 0;
static volatile uint32_t jogPressTickR = 0;

/* snapA/snapB declared at top of file — written here, read everywhere */

/* TIM9 millisecond counter — own tick source, independent of SysTick/HAL_GetTick.
 * Incremented in TIM1_BRK_TIM9_IRQHandler every 1 ms.
 * Used as 'now' in HAL_GPIO_EXTI_Callback so both EXTI and TIM9 share one time base. */
static volatile uint32_t tim9Ms = 0;

/* ============================================================
 * Button debounce — two-phase design:
 *
 *   Phase 1 — EXTI ISR (hardware edge, may be bouncing):
 *     EVT_DB_x == 0  →  first edge → arm: lastTick = now, set EVT_DB_x, return
 *     EVT_DB_x == 1  →  bounce within window → ignore (TIM9 is already watching)
 *
 *   Phase 2 — TIM1_BRK_TIM9_IRQHandler (TIM9 free-running, 1 ms period):
 *     When EVT_DB_x is set and debounce window has expired:
 *       → update snapA/snapB, call HAL_GPIO_EXTI_Callback(pin) directly
 *       → callback re-enters with EVT_DB_x == 1 and now−lastTick >= DEBOUNCE
 *       → reads settled pin level → fires real EVT_JOGL_DN/UP etc. → clears EVT_DB_x
 *
 * EVT_DB_x acts as the state machine discriminator:
 *   0 = idle      1+fresh edge = arming     1+time expired = settled → fire
 *
 * "HAL_GPIO_EXTI_Callback is just a function at an address — TIM9 ISR can
 *  branch to it exactly as the NVIC does.  LL thinking, not HAL thinking." — smooker
 * ============================================================ */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    /* Snapshot first — one consistent read per port; now from TIM9 (not SysTick) */
    snapA = GPIOA->IDR;
    snapB = GPIOB->IDR;
    uint32_t now = tim9Ms;

    switch (GPIO_Pin)
    {
    /* ---- Endstops: lockout debounce, instant Stepper_Stop ---- */
    case ES_L_Pin:
        if (!endstopsEn) break;
        if (!diagMode) {
            if (now - lastTick_esL >= DEBOUNCE_MS) {
                lastTick_esL = now;
                Stepper_Stop();
                buzzRequest = 1;
                evtFlags |= EVT_ES_L;
            }
        } else if (now - lastTick_esL >= DEBOUNCE_MS) {
            lastTick_esL = now;
            evtFlags |= EVT_ES_L;
        }
        break;

    case ES_R_Pin:
        if (!endstopsEn) break;
        if (!diagMode) {
            if (now - lastTick_esR >= DEBOUNCE_MS) {
                lastTick_esR = now;
                Stepper_Stop();
                buzzRequest = 1;
                evtFlags |= EVT_ES_R;
            }
        } else if (now - lastTick_esR >= DEBOUNCE_MS) {
            lastTick_esR = now;
            evtFlags |= EVT_ES_R;
        }
        break;

    /* ---- Jog buttons: two-phase debounce (see comment above) ---- */
    case BUTT_JOGL_Pin:
        if (!buttonsEn) break;
        if (evtFlags & EVT_DB_JOGL) {
            /* Phase 2 (SysTick re-call) or bounce: check if window expired */
            if (now - lastTick_jogL >= DEBOUNCE_REL_MS) {
                evtFlags &= ~EVT_DB_JOGL;
                lastTick_jogL = now;
                if (!(snapA & BUTT_JOGL_Pin)) {     /* LOW  = pressed */
                    if (snapA & ES_L_Pin) {          /* ES_L not active */
                        jogPressTickL = now;
                        evtFlags |= EVT_JOGL_DN;
                    }
                } else {                             /* HIGH = released */
                    if (jogStateL == JOG_CONT) Stepper_Stop();
                    evtFlags |= EVT_JOGL_UP;
                }
            }
            /* else: still within window — SysTick will call again */
        } else {
            /* Phase 1: first edge → arm debounce window */
            lastTick_jogL = now;
            evtFlags |= EVT_DB_JOGL;
        }
        break;

    case BUTT_JOGR_Pin:
        if (!buttonsEn) break;
        if (evtFlags & EVT_DB_JOGR) {
            if (now - lastTick_jogR >= DEBOUNCE_REL_MS) {
                evtFlags &= ~EVT_DB_JOGR;
                lastTick_jogR = now;
                if (!(snapA & BUTT_JOGR_Pin)) {     /* LOW  = pressed */
                    /* home combo: ES_L active + JOGL held + JOGR pressed */
                    if (!(snapA & ES_L_Pin) && !(snapA & BUTT_JOGL_Pin)) {
                        evtFlags |= EVT_HOME;
                    } else if (snapA & ES_R_Pin) {  /* ES_R not active */
                        jogPressTickR = now;
                        evtFlags |= EVT_JOGR_DN;
                    }
                } else {                             /* HIGH = released */
                    if (jogStateR == JOG_CONT) Stepper_Stop();
                    evtFlags |= EVT_JOGR_UP;
                }
            }
        } else {
            lastTick_jogR = now;
            evtFlags |= EVT_DB_JOGR;
        }
        break;

    /* ---- Step buttons: two-phase debounce, press only.
     * Arm on press edge (LOW) only — release edge ignored.
     * Lockout guard in arm phase: only arm if outside DEBOUNCE_MS window
     * since last confirmation — prevents release bounces from re-arming and
     * causing a second step+beep on button release.
     * TIM9 confirms after DEBOUNCE_MS; pin may be HIGH by then (quick tap) —
     * irrelevant, the press was recorded at arm time. ---- */
    case BUTT_STEPL_Pin:
        if (!buttonsEn) break;
        if (evtFlags & EVT_DB_STEPL) {
            if (now - lastTick_stepL >= DEBOUNCE_MS) {
                evtFlags &= ~EVT_DB_STEPL;
                lastTick_stepL = now;
                if (snapA & ES_L_Pin)        /* ES_L not active */
                    evtFlags |= EVT_STEPL;
            }
        } else {
            if (!(snapB & BUTT_STEPL_Pin) &&          /* LOW = press */
                (now - lastTick_stepL >= DEBOUNCE_MS)) { /* outside lockout */
                lastTick_stepL = now;
                evtFlags |= EVT_DB_STEPL;
            }
        }
        break;

    case BUTT_STEPR_Pin:
        if (!buttonsEn) break;
        if (evtFlags & EVT_DB_STEPR) {
            if (now - lastTick_stepR >= DEBOUNCE_MS) {
                evtFlags &= ~EVT_DB_STEPR;
                lastTick_stepR = now;
                if (snapA & ES_R_Pin)        /* ES_R not active */
                    evtFlags |= EVT_STEPR;
            }
        } else {
            if (!(snapB & BUTT_STEPR_Pin) &&          /* LOW = press */
                (now - lastTick_stepR >= DEBOUNCE_MS)) { /* outside lockout */
                lastTick_stepR = now;
                evtFlags |= EVT_DB_STEPR;
            }
        }
        break;

    default:
        break;
    }
}

/* TIM9 fires HAL_GPIO_EXTI_Callback directly — it is just a function at an address.
 * "HAL_GPIO_EXTI_Callback is not sacred: NVIC jumps to it, TIM9 ISR can too." — LL thinking.
 * Defined here (not in stm32f4xx_it.c) so static variables remain accessible.
 * TIM9: free-running, 96MHz/96/1000 = 1 ms period. PSC=95, ARR=999. */
void TIM1_BRK_TIM9_IRQHandler(void)
{
    if (!(TIM9->SR & TIM_SR_UIF)) return;
    TIM9->SR = ~TIM_SR_UIF;   /* clear update interrupt flag */

    tim9Ms++;                  /* advance our own 1 ms tick — no SysTick involved */
    uint32_t now = tim9Ms;

    if ((evtFlags & EVT_DB_JOGL)  && (now - lastTick_jogL  >= DEBOUNCE_REL_MS)) {
        snapA = GPIOA->IDR; snapB = GPIOB->IDR;
        HAL_GPIO_EXTI_Callback(BUTT_JOGL_Pin);
    }
    if ((evtFlags & EVT_DB_JOGR)  && (now - lastTick_jogR  >= DEBOUNCE_REL_MS)) {
        snapA = GPIOA->IDR; snapB = GPIOB->IDR;
        HAL_GPIO_EXTI_Callback(BUTT_JOGR_Pin);
    }
    if ((evtFlags & EVT_DB_STEPL) && (now - lastTick_stepL >= DEBOUNCE_MS)) {
        snapA = GPIOA->IDR; snapB = GPIOB->IDR;
        HAL_GPIO_EXTI_Callback(BUTT_STEPL_Pin);
    }
    if ((evtFlags & EVT_DB_STEPR) && (now - lastTick_stepR >= DEBOUNCE_MS)) {
        snapA = GPIOA->IDR; snapB = GPIOB->IDR;
        HAL_GPIO_EXTI_Callback(BUTT_STEPR_Pin);
    }
}

/* Homing: approach ES_L at homespd CCW, backoff at mmpsmin CW until release, +1mm */
/* Returns 1 if both JOGL and JOGR are still held (for combo abort check).
   homeFromButtons=0 means CDC command — no abort check needed. */
static int HomeButtonsHeld(void) {
    return !(snapA & BUTT_JOGL_Pin) && !(snapA & BUTT_JOGR_Pin);
}

void RunHome(void)
{
    RunHomeEx(0);
}

void RunHomeEx(uint8_t fromButtons)
{
    float savedMax = motorParams.mmpsmax.f;
    float savedMin = motorParams.mmpsmin.f;
    uint8_t dbg = motorParams.debug.u & 1;
    uint8_t homed = 0;

    /* Debounced abort: buttons must be released for HOME_RELEASE_MS to abort.
       Tolerates non-simultaneous finger release. */
#define HOME_RELEASE_MS 300
    uint32_t homePressLost = 0;
#define HOME_ABORT_CHECK(stop_motor) \
    if (fromButtons) { \
        if (!HomeButtonsHeld()) { \
            if (!homePressLost) homePressLost = tim9Ms | 1; \
            else if (tim9Ms - homePressLost >= HOME_RELEASE_MS) { \
                if (stop_motor) { Stepper_Stop(); while (Stepper_IsBusy()) { HAL_Delay(5); } } \
                goto home_restore; \
            } \
        } else { homePressLost = 0; } \
    }

    endstopsEn = 0;  /* ignore EXTI — we poll manually to avoid EMI false triggers */
    buttonsEn = 0;
    uint32_t parkSteps = motorParams.homeoff.u;

    /* Check if already on ES_L */
    if (!(snapA & ES_L_Pin)) {
        if (dbg) printf("home: already on ES_L, backoff\r\n");
    } else {
        /* Phase 1: approach ES_L at homespd (CCW), debounced ES poll */
        if (dbg) printf("home: approach ES_L @ %.1f mm/s CCW\r\n", motorParams.homespd.f);
        motorParams.mmpsmax.f = motorParams.homespd.f;
        Stepper_Move(-9999.0f);
        uint8_t lowCount = 0;
        while (lowCount < 10 && Stepper_IsBusy()) {
            HOME_ABORT_CHECK(1)
            if (!(snapA & ES_L_Pin))
                lowCount++;
            else
                lowCount = 0;
            HAL_Delay(5);
        }
        Stepper_Stop();
        while (Stepper_IsBusy()) { HAL_Delay(5); }
        if (dbg) printf("home: ES_L confirmed, settling...\r\n");

        /* Phase 2: settle — 500ms, abort-checked every 10ms */
        uint32_t settleEnd = tim9Ms + 500;
        while (tim9Ms < settleEnd) {
            HOME_ABORT_CHECK(0)
            HAL_Delay(10);
        }
    }

    /* Phase 3: backoff at homespd/10 (CW) until ES_L releases (debounced) */
    float backoffSpd = motorParams.homespd.f / 10.0f;
    if (backoffSpd < 0.1f) backoffSpd = 0.1f;
    if (dbg) printf("home: backoff @ %.2f mm/s CW\r\n", backoffSpd);
    motorParams.mmpsmax.f = backoffSpd;
    motorParams.mmpsmin.f = backoffSpd;
    Stepper_Move(9999.0f);
    uint8_t highCount = 0;
    while (highCount < 10 && Stepper_IsBusy()) {
        HOME_ABORT_CHECK(1)
        if (snapA & ES_L_Pin)
            highCount++;
        else
            highCount = 0;
        HAL_Delay(5);
    }
    Stepper_Stop();
    while (Stepper_IsBusy()) { HAL_Delay(5); }
    if (dbg) printf("home: ES_L released, +%lu steps CW\r\n", parkSteps);

    /* Phase 4: park — move homeoff steps CW, abort-checked during move */
    HAL_Delay(200);
    Stepper_MoveSteps((int32_t)parkSteps);
    while (Stepper_IsBusy()) {
        HOME_ABORT_CHECK(1)
        HAL_GPIO_TogglePin(LED_USER_GPIO_Port, LED_USER_Pin);
        HAL_Delay(50);
    }

    /* Home achieved */
    posSteps = 0;
    posHomed = 1;
    homed = 1;

    /* Phase 5: 1s grace period then beep — machine is active */
    if (fromButtons) {
        HAL_Delay(1000);
        HAL_GPIO_WritePin(BUZZ_GPIO_Port, BUZZ_Pin, GPIO_PIN_RESET);
        HAL_Delay(150);
        HAL_GPIO_WritePin(BUZZ_GPIO_Port, BUZZ_Pin, GPIO_PIN_SET);
    }

home_restore:
    motorParams.mmpsmax.f = savedMax;
    motorParams.mmpsmin.f = savedMin;
    endstopsEn = 1;
    buttonsEn = 1;
    
    if (!homed && fromButtons) printf("home: ABORT\r\n");
    printf("\r\n\r\n");
    PrintPrompt();

#undef HOME_RELEASE_MS
#undef HOME_ABORT_CHECK
}

/* Measure travel range: drive to ES_R, print raw+usable mm, return to 0 */
void RunRange(void)
{
    if (!posHomed) { printf("range: not homed\r\n"); PrintPrompt(); return; }

    printf("range: driving to ES_R...\r\n");
    
    Stepper_Move(9999.0f);
    while (Stepper_IsBusy()) {
        HAL_GPIO_TogglePin(LED_USER_GPIO_Port, LED_USER_Pin);
        HAL_Delay(50);
    }

    int32_t rawSteps = posSteps;
    float rawMm      = (float)rawSteps / (float)motorParams.spmm.u;
    float usableMm   = (float)(rawSteps - (int32_t)motorParams.homeoff.u) / (float)motorParams.spmm.u;
    if (usableMm < 0.0f) usableMm = 0.0f;
    rangeUsableMm = usableMm;

    printf("range: %.2f mm raw, %.2f mm usable (homeoff=%lu steps)\r\n",
           rawMm, usableMm, motorParams.homeoff.u);

    /* Return to home position — endstops off to avoid ES_L interference near home */
    HAL_Delay(300);
    
    endstopsEn = 0;
    Stepper_MoveSteps(-rawSteps);
    while (Stepper_IsBusy()) {
        HAL_GPIO_TogglePin(LED_USER_GPIO_Port, LED_USER_Pin);
        HAL_Delay(50);
    }
    endstopsEn = 1;
    

    printf("\r\n\r\n");
    PrintPrompt();
}

/* Combo test: 4 moves with wait between each */
void RunCombo(void)
{
    printf("combo: triangle L\r\n");
    Stepper_Move(-1.0f);
    while (Stepper_IsBusy()) { HAL_GPIO_TogglePin(LED_USER_GPIO_Port, LED_USER_Pin); HAL_Delay(100); }
    HAL_Delay(500);

    printf("combo: triangle R\r\n");
    Stepper_Move(1.0f);
    while (Stepper_IsBusy()) { HAL_GPIO_TogglePin(LED_USER_GPIO_Port, LED_USER_Pin); HAL_Delay(100); }
    HAL_Delay(500);

    printf("combo: trapezoid L\r\n");
    Stepper_Move(-5.0f);
    while (Stepper_IsBusy()) { HAL_GPIO_TogglePin(LED_USER_GPIO_Port, LED_USER_Pin); HAL_Delay(100); }
    HAL_Delay(500);

    printf("combo: trapezoid R\r\n");
    Stepper_Move(5.0f);
    while (Stepper_IsBusy()) { HAL_GPIO_TogglePin(LED_USER_GPIO_Port, LED_USER_Pin); HAL_Delay(100); }

    printf("\r\n\r\ncombo done\r\n\r\n");
    PrintPrompt();
}

/* Called from main loop — safe to printf here */
void ProcessEvents(void)
{
    uint32_t now = tim9Ms;  /* same time base as jogPressTick* set in EXTI callback */
    __disable_irq();
    uint32_t flags = evtFlags & ~(EVT_DB_JOGL|EVT_DB_JOGR|EVT_DB_STEPL|EVT_DB_STEPR);
    evtFlags &= ~flags;  /* clear only real events — EVT_DB_* stay for TIM9 to consume */
    __enable_irq();

    /* ---- Endstops ---- */
    if (flags & EVT_ES_L) {
        if (motorParams.debug.u & 1) { printf("ES_L hit\r\n"); }
        jogStateL = JOG_IDLE;
        jogStateR = JOG_IDLE;
    }
    if (flags & EVT_ES_R) {
        if (motorParams.debug.u & 1) { printf("ES_R hit\r\n"); }
        jogStateL = JOG_IDLE;
        jogStateR = JOG_IDLE;
    }

    /* ---- Jog Left (CCW) ---- */
    if (flags & EVT_JOGL_DN) {
        if (diagMode) { printf("JOGL_DN\r\n"); PrintPrompt(); }
        else if (!(snapA & ES_L_Pin)) { /* ES_L active LOW = don't go left */ }
        else {
            jogStateL = JOG_PRESSED;
            buzzRequest = 1;
            Stepper_Jog(-1.0f);   /* immediate short jog */
        }
    }
    if (flags & EVT_JOGL_UP) {
        if (diagMode) { printf("JOGL_UP\r\n"); PrintPrompt(); }
        if (jogStateL == JOG_CONT) Stepper_Stop();  /* safety net: ISR may have missed the race */
        jogStateL = JOG_IDLE;
    }

    /* ---- Jog Right (CW) ---- */
    if (flags & EVT_JOGR_DN) {
        if (diagMode) { printf("JOGR_DN\r\n"); PrintPrompt(); }
        else if (!(snapA & ES_R_Pin)) { /* ES_R active LOW = don't go right */ }
        else {
            jogStateR = JOG_PRESSED;
            buzzRequest = 1;
            Stepper_Jog(1.0f);    /* immediate short jog */
        }
    }
    if (flags & EVT_JOGR_UP) {
        if (diagMode) { printf("JOGR_UP\r\n"); PrintPrompt(); }
        if (jogStateR == JOG_CONT) Stepper_Stop();  /* safety net: ISR may have missed the race */
        jogStateR = JOG_IDLE;
    }

    /* ---- Jog hold detection (polling, no event needed) ---- */
    if (!diagMode) {
        if (jogStateL == JOG_PRESSED && !Stepper_IsBusy()
            && (now - jogPressTickL >= JOG_HOLD_MS)) {
            if (!(snapA & ES_L_Pin)) { jogStateL = JOG_IDLE; }
            else { jogStateL = JOG_CONT; Stepper_RunContinuous(-1); }
        }
        if (jogStateR == JOG_PRESSED && !Stepper_IsBusy()
            && (now - jogPressTickR >= JOG_HOLD_MS)) {
            if (!(snapA & ES_R_Pin)) { jogStateR = JOG_IDLE; }
            else { jogStateR = JOG_CONT; Stepper_RunContinuous(1); }
        }
    }

    /* ---- Step buttons ---- */
    if (flags & EVT_STEPL) {
        if (diagMode) { printf("BUTT_STEPL snapB=%08lx\r\n", snapB); PrintPrompt(); }
        else if (!(snapA & ES_L_Pin)) { /* ES_L active = blocked */ }
        else { buzzRequest = 1; Stepper_Move(-motorParams.stepmm.f); }
    }
    if (flags & EVT_STEPR) {
        if (diagMode) { printf("BUTT_STEPR snapB=%08lx\r\n", snapB); PrintPrompt(); }
        else if (!(snapA & ES_R_Pin)) { /* ES_R active = blocked */ }
        else { buzzRequest = 1; Stepper_Move(motorParams.stepmm.f); }
    }

    /* ---- Home combo (ES_L hit + JOGL held + JOGR pressed) ---- */
    if (flags & EVT_HOME) {
        if (!diagMode) RunHomeEx(1);
    }
}

/**
  * @brief  Safe state: Hi-Z all functional pins, fast-blink LED as crash indicator.
  *         Called from Error_Handler and all Cortex-M fault handlers.
  *         Must not use HAL_Delay (SysTick IRQ may be dead).
  */
void SafeState_And_Blink(void)
{
    __disable_irq();

    /* Hi-Z all functional pins: input mode (MODER=00), no pull (PUPDR=00).
     * PA3=ES_L  PA4=ES_R  PA6=JOGL  PA7=JOGR
     * PB0=STEPL PB1=STEPR PB10=PULSE PB14=DIR PB15=BUZZ */
    GPIOA->MODER &= ~((3UL<<(3*2))|(3UL<<(4*2))|(3UL<<(6*2))|(3UL<<(7*2)));
    GPIOA->PUPDR &= ~((3UL<<(3*2))|(3UL<<(4*2))|(3UL<<(6*2))|(3UL<<(7*2)));

    GPIOB->MODER &= ~((3UL<<(0*2))|(3UL<<(1*2))|(3UL<<(10*2))|(3UL<<(14*2))|(3UL<<(15*2)));
    GPIOB->PUPDR &= ~((3UL<<(0*2))|(3UL<<(1*2))|(3UL<<(10*2))|(3UL<<(14*2))|(3UL<<(15*2)));

    /* PC13=LED_USER: force output (MODER=01) */
    GPIOC->MODER = (GPIOC->MODER & ~(3UL<<(13*2))) | (1UL<<(13*2));

    /* Fast blink ~16Hz — direct register, no HAL_Delay */
    while (1) {
        GPIOC->ODR ^= (1UL << 13);
        for (volatile uint32_t i = 0; i < 300000UL; i++) {}
    }
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  SafeState_And_Blink();
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
