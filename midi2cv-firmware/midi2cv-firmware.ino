/*
 * MIDI Interface
 *
 * MIDI clock to 16th note clock signal
 * MIDI clock to DYN SYNC 24ppqn signal
 *
 * Code based on:
 * - https://github.com/dhaillant/midi8d
 * - https://github.com/kassu/KassutronicsQuantizer
 *
 * Hardware based on:
 * - https://n-audio.net/designing-midi-in-and-midi-out-schematics/
 * - https://electricdruid.net/product/midi-inoutthru-pcb/
 * - https://github.com/dhaillant/midi8d
 * - https://github.com/kassu/kassutronics/tree/master/documentation/Quantizer
 * 
*/
// #define PITCH_CALIBRATION  // uncomment for calibration mode

// Macros to set and clear a single bit in any register
#ifndef cbi
#define cbi(sfr, bit) (_SFR_BYTE(sfr) &= ~_BV(bit))
#endif
#ifndef sbi
#define sbi(sfr, bit) (_SFR_BYTE(sfr) |= _BV(bit))
#endif

#define TRIGGER_LENGTH 1000  // clock and sync gate length in microseconds

#include <Arduino.h>
#include <MIDI.h>  // https://github.com/FortySevenEffects/arduino_midi_library.git

const uint8_t PIN_CLOCK = 8;
const uint8_t PIN_CLOCK2 = 7;

const uint8_t PIN_CC = 10;    // PWM pin
const uint8_t PIN_PITCH = 9;  // PWM pin
const uint8_t PIN_GATE = A5;

const uint8_t PIN_SERIAL_RX = 0;
const uint8_t PIN_SERIAL_TX = 1;


// currently unused pins for extension board
const uint8_t PIN_EXP1 = 3;
const uint8_t PIN_EXP3 = 5;
const uint8_t PIN_EXP5 = 11;
const uint8_t PIN_EXP7 = 6;

const uint8_t MIDI_CHANNEL = MIDI_CHANNEL_OMNI;  // Set midi channel here
const uint8_t CC_NUMBER = 27;                    // TODO: find a good default...


MIDI_CREATE_DEFAULT_INSTANCE();

bool isRunning = false;

class Trigger {
public:
  uint8_t step = 0;
  bool isTriggered = false;

  Trigger(const uint8_t pin)
    : PIN(pin) {}

  void update() {
    long now = micros();
    if (isTriggered) {  // pick up trigger signal
      triggerStart = now;
      isTriggered = false;
    }
    if (now - triggerStart < TRIGGER_LENGTH) {  // keep pin high for the predefined duration
      digitalWrite(PIN, HIGH);
    } else {
      digitalWrite(PIN, LOW);
    }
  }
private:
  const uint8_t PIN;
  long triggerStart = 0;
};

Trigger clock(PIN_CLOCK2);
Trigger sync(PIN_CLOCK);

void startupBlinkLED() {
  digitalWrite(PIN_GATE, LOW);
  digitalWrite(PIN_CLOCK2, LOW);
  delay(1000);
  digitalWrite(PIN_GATE, HIGH);
  delay(200);
  digitalWrite(PIN_GATE, LOW);
  digitalWrite(PIN_CLOCK2, HIGH);
  delay(200);
  digitalWrite(PIN_CLOCK2, LOW);
}
void setup() {
  // Serial.begin(115200);
  // Serial.begin(31250);

  setupPWM();

  pinMode(PIN_GATE, OUTPUT);
  pinMode(PIN_CLOCK, OUTPUT);
  pinMode(PIN_CLOCK2, OUTPUT);
  digitalWrite(PIN_GATE, HIGH);
  digitalWrite(PIN_CLOCK, HIGH);
  digitalWrite(PIN_CLOCK2, HIGH);

#ifndef PITCH_CALIBRATION
  MIDI.setHandleClock(handleClock);        // called on each clock pulse
  MIDI.setHandleStart(handleStart);        // called on START message
  MIDI.setHandleContinue(handleContinue);  // called on CONTINUE message
  MIDI.setHandleStop(handleStop);          // called on STOP message
  MIDI.setHandleNoteOn(handleNoteOn);
  MIDI.setHandleNoteOff(handleNoteOff);
  MIDI.setHandleControlChange(handleCC);

  MIDI.begin(MIDI_CHANNEL);  // filter MIDI channel
#endif

  delay(1000);
  // Serial.println("Serial ready...");
  startupBlinkLED();
}

void loop() {

#if defined(PITCH_CALIBRATION)
  writePitch(36);  // for unipolar output: 3V
  delay(2000);
  writePitch(84);  // for unipolar output: 7V
  delay(2000);
#else
  MIDI.read();
  clock.update();
  sync.update();
#endif
}

void handleStart() {
  // Serial.println("Start");

  isRunning = true;
  sync.step = 0;
  clock.step = 0;
}

void handleContinue() {
  // Serial.println("Continue");

  isRunning = true;
}

void handleStop() {
  // Serial.println("Stop");

  isRunning = false;
}

void handleClock() {
  // Serial.println("Tick");
  if (isRunning) {
    clock.isTriggered = (clock.step == 0);
    sync.isTriggered = true;

    sync.step++;
    sync.step = sync.step % 96;  // 24ppqn MIDI clock, directly suitable for DIN SYNC; fold at each bar to avoid overflow
    clock.step = sync.step % 6;  // 16th notes; as a division of the midi clock
  }
}

void handleNoteOn(midi::Channel channel, byte note, byte velocity) {
  // convert note V/Oct
  // one step in the PWM output is one note, so only shift could be adjusted
  // Pitch output needs hardware calibration
  // C-2 is midi note 0
  // C 3 is midi note 60
  // C 8 is midi note 120
  uint8_t noteIndex = note;
  writePitch(noteIndex);

  // gate on unless velocity is 0
  if (velocity == 0) {
    digitalWrite(PIN_GATE, LOW);
  } else {
    digitalWrite(PIN_GATE, HIGH);
  }
}

void handleNoteOff(midi::Channel channel, byte note, byte velocity) {
  // keep last pitch aka do nothing
  // gate off
  digitalWrite(PIN_GATE, LOW);
}

void handleCC(midi::Channel channel, byte cc_number, byte value) {
  if (cc_number == CC_NUMBER) {  // filter CC number
    writeCC(value);
  }
}

// This is taken from https://github.com/kassu/KassutronicsQuantizer/blob/master/KassutronicsQuantizer/Hardware.ino
void setupPWM() {
  // Setup timer1 for PWM on pin 9 and 10

  // The pins should be outputs
  pinMode(PIN_PITCH, OUTPUT);
  // pinMode(9, OUTPUT);
  pinMode(PIN_CC, OUTPUT);
  // pinMode(10, OUTPUT);

  // --- Clock prescaler ---
  // The PWM clock runs at the CPU rate (16 MHz) divided by a prescaler.
  // For settings 0x01 -- 0x05, the prescaler is 1, 8, 64, 256, 1024, respectively.
  // We use the fastest option:
  TCCR1B = TCCR1B & 0b11111000 | 0x01;

  // --- Waveform Generation Mode (WGM)
  /* WGM13:0 sets the PWM mode. Of the 16 modes, these are most interesting:
     5 - 7: Fast PWM 8-10 bit, respectively
     14: Fast PWM, frequency set by ICR1 register
     15: Fast PWM, frequency set by OCR1A register
 
     In mode 14 and 15, the PWM frequency can be chosen very flexibly: 
  
         fPWM = fCPU / (N * (TOP+1))
  
     where fIO is the CPU clock, N is the prescaler set above and TOP is the value in
     either ICR1 or OCR1A (mode 14 or 15, resp.).
     
     We use mode 14, because then we can freely choose the frequency, but have the OCR1A register
     available to control the PWM on pin 9.
    */
  // The mode setting is spread over the two timer registers,so we set it one bit at the time (14 = 0b1110 = set set set clear)
  sbi(TCCR1B, WGM13);
  sbi(TCCR1B, WGM12);
  sbi(TCCR1A, WGM11);
  cbi(TCCR1A, WGM10);

  // Set ICR1 register define frequency (see formula above) and resolution (valid output values are 0 to ICR1)
  //ICR1 = 0x01FF; // 9 bit
  ICR1 = 0x007F;  // 7 bit

  // Compare Output Mode - COM1A1, COM1A0, COM1B1, COM1B0 (first 4 bits of TCCR1A, respectively)
  // These set the mode for output A (=pin 9) and output B (=pin 10), respectively. In fast PWM mode, the modes are:
  //   0b00 - Normal port (disable PWM)
  //   0b01 - Only in WGM mode 14 or 15 (see above): Toggle output A on Compare Match. Output B is normal port (PWM disabled)
  //          I'm not sure what the point of this is. It seems you get always 50% duty cycle and half the normal fast PWM frequency,
  //          but then you can set the phase with register OCR1A (or analogWrite(9,phase) )
  //   0b10 - non-inverting PWM
  //   0b11 - inverting PWM
  // Arduinos analogWrite sets the first bit for the respective port each time you call analogWrite (so you can switch to
  // digitalWrite and back whenever you feel like it). Let's set them all explicitly in the beginning, so we can write the
  // registers directly.
  sbi(TCCR1A, COM1A1);
  cbi(TCCR1A, COM1A0);
  sbi(TCCR1A, COM1B1);
  cbi(TCCR1A, COM1B0);

  // Write 0 to both ports as initial value
  OCR1A = 0;
  OCR1B = 0;
}

inline void writePitch(uint8_t pitch) {
  OCR1A = pitch;
}

inline void writeCC(uint8_t cc) {
  OCR1B = cc;
}
