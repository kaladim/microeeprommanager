/*!
 * \brief  Main entry point of the demo application.
 * \mcu    PIC16F1825: https://www.microchip.com/en-us/product/PIC16F1825
 * \compiler XC8: https://www.microchip.com/en-us/tools-resources/develop/mplab-xc-compilers/xc8
 */
#include <xc.h>
#include "MEEM.h"

// Configuration bits
#pragma config FOSC = INTOSC
#pragma config WDTE = OFF
#pragma config MCLRE = OFF
#pragma config PWRTE = ON
#pragma config PLLEN = OFF

extern void AppA_Init(void);
extern void AppA_Task_10ms(void);
extern void AppA_OnShutdown(void);
extern bool AppA_NeedsToShutdownTheSystem(void);
extern void AppB_Init(void);
extern void AppB_Task_10ms(void);
extern void AppB_OnShutdown(void);
extern void AppC_Init(void);
extern void AppC_Task_10ms(void);
extern void AppC_OnShutdown(void);
extern void AppD_Init(void);
extern void AppD_Task_10ms(void);
extern void AppD_OnShutdown(void);

void McuInit(void)
{
    // Set the CPU clock to 16 MHz, internal HF oscillator
    OSCCONbits.IRCF = 0b1111;

    // Configure TMR2 to overflow every ~10ms:
    // TMR2 clock input = Fosc/4 = 4 MHz
    T2CONbits.T2CKPS = 0b10;    // Prescaler 1:16
    T2CONbits.T2OUTPS = 0b1111; // Postscaler 1:16
    PR2 = 156;                  // Period register
    T2CONbits.TMR2ON = 1;       // Start the timer
}

#define Have_10ms_SystemTick() (PIR1bits.TMR2IF != 0)
#define Clear_10ms_SystemTick() PIR1bits.TMR2IF = 0

void main(void)
{
    // --- Init phase ---
    McuInit();

    // mEEM should init before any application that uses EEPROM data:
    MEEM_Init();
    // ...
    AppA_Init();
    AppB_Init();
    AppC_Init();
    AppD_Init();
    // --- End of the init phase ---

    // Don't forget to:
    MEEM_Resume();

    // The usual super loop:
    while (true)
    {
        if (Have_10ms_SystemTick())
        {
            Clear_10ms_SystemTick();

            AppA_Task_10ms();
            // Application A decides to shutdown the system here:
            if (AppA_NeedsToShutdownTheSystem())
            {
                break;
            }

            AppB_Task_10ms();
            AppC_Task_10ms();
            AppD_Task_10ms();
        }

        // The optimal call period for MEEM_Task() is ~5..6ms: this is the typical time for page/byte write of most EEPROMs.
        // But, more frequent calls doesn't hurt too:
        MEEM_PeriodicTask();
    }

    // --- System shutdown phase ---
    // Applications first:
    AppA_OnShutdown();
    AppB_OnShutdown();
    AppC_OnShutdown();
    AppD_OnShutdown();

    // ...then the mEEM:
    MEEM_Suspend();
    while (MEEM_IsBusy())
    {
        MEEM_PeriodicTask();
    }

    while (true)
    {
        // Wait for power off
    }
}