#include <Arduino.h>

#define PWM_PIN 9


void setup() {
  // Initialize PWM on Pin 9 with the fastest frequency and 7-bit resolution
  initPWMOnPin(PWM_PIN);

}

void loop() {
  // You can control the PWM duty cycle with analogWrite (0 to 127 for 7-bit resolution)
  for (int i = 0; i < 128; i+=32) {
    analogWrite(PWM_PIN, i);  // 7-bit resolution: values from 0 to 127
    delay(10);                 // Small delay to visualize the change
  }
}

// Function to set Timer1 for fast PWM on a given pin with 7-bit resolution
void initPWMOnPin(int pin) {
  // Set the pin as an output (moved pinMode inside the function)
  pinMode(pin, OUTPUT);

  // Disable interrupts during timer configuration to avoid errors
  noInterrupts();
  
  // Set Timer1 to Fast PWM mode with a prescaler of 1 (highest frequency)
  TCCR1A = (1 << COM1A1) | (1 << WGM11) | (1 << WGM10);  // Fast PWM, non-inverted on OC1A (pin 9)
  TCCR1B = (1 << WGM13) | (1 << WGM12) | (1 << CS10);     // No prescaling, Fast PWM
  
  // Set the PWM resolution to 7-bits by setting the TOP value to 127
  OCR1A = 127;  // Set TOP value for 7-bit resolution

  // Re-enable interrupts
  interrupts();
}