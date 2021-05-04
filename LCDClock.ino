/* Low-Power LCD Clock

   David Johnson-Davies - www.technoblogy.com - 4th May 2021
   AVR128DA48 @ 24 MHz (internal oscillator; BOD disabled)
   
   CC BY 4.0
   Licensed under a Creative Commons Attribution 4.0 International license: 
   http://creativecommons.org/licenses/by/4.0/
*/

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>

// Seven segment definitions

const int CharLen = 15;
uint8_t Char[CharLen] = {
//  abcdefg  Segments
  0b1111110, // 0
  0b0110000, // 1
  0b1101101, // 2
  0b1111001, // 3
  0b0110011, // 4
  0b1011011, // 5
  0b1011111, // 6
  0b1110000, // 7
  0b1111111, // 8
  0b1111011, // 9
  0b0000000, // 10  Space
  0b0000001, // 11  '-'
  0b1100011, // 12  Degree
  0b1001110, // 13  'C'
  0b0111110  // 14  'V'
};

const int Space = 10;
const int Minus = 11;
const int Degree = 12;
const int Celsius = 13;
const int Vee = 14;

// Ports **********************************************

PORT_t *Digit[4] = { &PORTD, &PORTC, &PORTA, &PORTB };

void PortSetup () {
  for (int p=0; p<4; p++) Digit[p]->DIR = 0xFF;       // All pins outputs
  PORTE.DIR = PIN0_bm | PIN1_bm;                      // COMs outputs, PE0 and PE1
  PORTF.DIR = PIN5_bm | PIN4_bm;                      // 1A, colon
}

// Real-Time Clock **********************************************

void RTCSetup () {
  uint8_t temp;
  // Initialize 32.768kHz Oscillator:

  // Disable oscillator:
  temp = CLKCTRL.XOSC32KCTRLA & ~CLKCTRL_ENABLE_bm;

  // Enable writing to protected register
  CPU_CCP = CCP_IOREG_gc;
  CLKCTRL.XOSC32KCTRLA = temp;

  while (CLKCTRL.MCLKSTATUS & CLKCTRL_XOSC32KS_bm);   // Wait until XOSC32KS is 0
  
  temp = CLKCTRL.XOSC32KCTRLA | CLKCTRL_LPMODE_bm;      // Use External Crystal & low power
  
  // Enable writing to protected register
  CPU_CCP = CCP_IOREG_gc;
  CLKCTRL.XOSC32KCTRLA = temp;
  
  temp = CLKCTRL.XOSC32KCTRLA | CLKCTRL_ENABLE_bm;    // Enable oscillator
  
  // Enable writing to protected register
  CPU_CCP = CCP_IOREG_gc;
  CLKCTRL.XOSC32KCTRLA = temp;
  
  // Initialize RTC
  while (RTC.STATUS > 0);                             // Wait until registers synchronized

  // 32.768kHz External Crystal Oscillator (XOSC32K)
  RTC.CLKSEL = RTC_CLKSEL_XOSC32K_gc;

  RTC.PITINTCTRL = RTC_PI_bm;                         // Periodic Interrupt: enabled
  
  // RTC Clock Cycles 512, enabled ie 64Hz interrupt
  RTC.PITCTRLA = RTC_PERIOD_CYC512_gc | RTC_PITEN_bm;
}

// ADC Setup **********************************************

void ADCSetup () {
  VREF.ADC0REF = VREF_REFSEL_2V048_gc;
  ADC0.CTRLC = ADC_PRESC_DIV64_gc;                    // 375kHz clock
  VREF.ACREF = VREF_REFSEL_VDD_gc;                    // For voltage measurement
  AC0.DACREF = 64;                                    // DACREF0 voltage VDD*64/256
  ADC0.MUXPOS = ADC_MUXPOS_DACREF0_gc;                // Initially voltage
}

// Display Temperature **********************************************

void DisplayTemp () {
  uint16_t offset = SIGROW.TEMPSENSE1;                // Read offset calibration
  uint16_t slope = SIGROW.TEMPSENSE0;                 // Read slope calibration
  ADC0.MUXPOS = ADC_MUXPOS_TEMPSENSE_gc; 
  ADC0.CTRLA = ADC_ENABLE_bm;                         // Single, 12-bit, left adjust
  ADC0.COMMAND = ADC_STCONV_bm;                       // Start conversion
  while (ADC0.COMMAND & ADC_STCONV_bm);               // Wait for completion
  uint16_t adc_reading = ADC0.RES;                    // ADC conversion result
  uint32_t kelvin = ((uint32_t)(offset - adc_reading) * slope + 0x0800) >> 12;
  int celsius = abs(kelvin - 273);
  ADC0.CTRLA = 0;                                     // Disable ADC

  // Display it
  if (kelvin < 273) Digit[0]->OUT = Char[Minus];
    else Digit[0]->OUT = Char[celsius/10];
  Digit[1]->OUT = Char[celsius%10];
  Digit[2]->OUT = Char[Degree];
  uint8_t units = Char[Celsius];
  Digit[3]->OUT = units;
  PORTF.OUT = (units>>1 & PIN5_bm);                   // No colon
}

// Display Voltage **********************************************

void DisplayVoltage () {
  ADC0.MUXPOS = ADC_MUXPOS_DACREF0_gc;                // Measure DACREF0
  ADC0.CTRLA = ADC_ENABLE_bm;                         // Single, 12-bit, left adjusted
  ADC0.COMMAND = ADC_STCONV_bm;                       // Start conversion
  while (ADC0.COMMAND & ADC_STCONV_bm);               // Wait for completion
  uint16_t adc_reading = ADC0.RES;                    // ADC conversion result
  uint16_t voltage = adc_reading/50;
  ADC0.CTRLA = 0;                                     // Disable ADC

  // Display it
  Digit[0]->OUT = Char[Space];
  Digit[1]->OUT = Char[voltage/10] | 0x80;            // Decimal point
  Digit[2]->OUT = Char[voltage%10];
  uint8_t units = Char[Vee];
  Digit[3]->OUT = units;
  PORTF.OUT = (units>>1 & PIN5_bm);                   // No colon
}

// Display Time **********************************************

void DisplayTime (unsigned long halfsecs) {
  uint8_t minutes = (halfsecs / 120) % 60;
  #ifdef TWELVEHOUR
  uint8_t hours = (halfsecs / 7200) % 12 + 1;
  #else
  uint8_t hours = (halfsecs / 7200) % 24;
  #endif
  Digit[0]->OUT = Char[hours/10];
  Digit[1]->OUT = Char[hours%10];
  Digit[2]->OUT = Char[minutes/10];
  uint8_t units = Char[minutes%10];
  Digit[3]->OUT = units;
  uint8_t colon = (halfsecs & 1)<<4;                  // Toggle colon at 1Hz   
  PORTF.OUT = (units>>1 & PIN5_bm) | colon;
}

// Interrupt Service Routine at 64Hz
ISR(RTC_PIT_vect) {
  static uint8_t cycles = 0;
  static unsigned long halfsecs;
  RTC.PITINTFLAGS = RTC_PI_bm;                        // Clear interrupt flag
  // Toggle segments
  for (int p=0; p<4; p++) Digit[p]->OUTTGL = 0xFF;    // Toggle all PORTA,B,C,D pins
  PORTE.OUTTGL = PIN0_bm | PIN1_bm;                   // Toggle COMs, PE0 and PE1
  PORTF.OUTTGL = PIN5_bm | PIN4_bm;                   // Toggle segment 1A, Colon

  cycles++;
  if (cycles < 32) return;
  cycles = 0;

  // Update time
  halfsecs = (halfsecs+1) % 172800;                   // 24 hours
  uint8_t ticks = halfsecs % 120;                     // Half-second ticks
                 
  if (MinsButton()) halfsecs = ((halfsecs/7200)*60 + (halfsecs/120 + 1)%60)*120;
  if (HoursButton()) halfsecs = halfsecs + 7200;

  if (MinsButton() || HoursButton() || ticks < 108) DisplayTime(halfsecs);
  else if (ticks == 108) DisplayVoltage();
  else if (ticks == 114) DisplayTemp();
}

// Buttons **********************************************

void ButtonSetup () {
  PORTE.PIN3CTRL = PORT_PULLUPEN_bm;                  // PE3 input pullup
  PORTE.PIN2CTRL = PORT_PULLUPEN_bm;                  // PE2 input pullup
}

boolean MinsButton () {
  return (PORTE.IN & PIN2_bm) == 0;                   // True if button pressed
}

boolean HoursButton () {
  return (PORTE.IN & PIN3_bm) == 0;                   // True if button pressed
}

// Sleep **********************************************

void SleepSetup () {
  SLPCTRL.CTRLA |= SLPCTRL_SMODE_PDOWN_gc;
  SLPCTRL.CTRLA |= SLPCTRL_SEN_bm;
}

// Setup **********************************************

void setup () {
  PortSetup();
  ButtonSetup();
  SleepSetup();
  RTCSetup();
  ADCSetup();
}

// Just stay asleep to save power unless woken by an interrupt
void loop () {
  sleep_cpu();
}
