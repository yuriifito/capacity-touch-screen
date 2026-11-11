#include <Arduino.h>
#include "config.h"

// One captured discharge curve.
static uint16_t raw[NUM_SAMPLES];   // ADC readings (0..4095)
static uint32_t t_us[NUM_SAMPLES];  // time since release, microseconds
static uint16_t raw_charged;        // ADC reading while CHARGE_PIN is still HIGH

static void chargeStrip() {
  digitalWrite(CHARGE_PIN, HIGH);
  delayMicroseconds(CHARGE_TIME_US);
}

static void releaseStrip() {
  // Drive LOW: C discharges through R into the pin.
  digitalWrite(CHARGE_PIN, LOW);
}

static void captureDischarge() {
  chargeStrip();
  raw_charged = analogRead(SENSE_PIN);  // diagnostic: should be ~3300 mV
  releaseStrip();
  const uint32_t t0 = micros();
  for (int i = 0; i < NUM_SAMPLES; i++) {
    raw[i] = analogRead(SENSE_PIN);
    t_us[i] = micros() - t0;
#if SAMPLE_INTERVAL_US > 0
    delayMicroseconds(SAMPLE_INTERVAL_US);
#endif
  }
}

// Text protocol, easy to read in any serial monitor:
//   B <count> <mV while charging>
//   <t_us>,<mV>
//   ...
//   E
static void sendBurst() {
  Serial.printf("B %d %lu\n", NUM_SAMPLES,
                (unsigned long)((uint32_t)raw_charged * 3300 / 4095));
  for (int i = 0; i < NUM_SAMPLES; i++) {
    // Linear approximation for 11 dB attenuation (~0..3.1 V usable).
    // Good enough for comparing discharge times.
    const uint32_t mv = (uint32_t)raw[i] * 3300 / 4095;
    Serial.printf("%lu,%lu\n", (unsigned long)t_us[i], (unsigned long)mv);
  }
  Serial.print("E\n");
}

void setup() {
  Serial.begin(SERIAL_BAUD);

  analogReadResolution(12);
  analogSetPinAttenuation(SENSE_PIN, ADC_11db);  // full 0..3.3 V range
  analogRead(SENSE_PIN);                         // attach pin to ADC1

  pinMode(CHARGE_PIN, OUTPUT);
  digitalWrite(CHARGE_PIN, LOW);
  delay(500);
  Serial.print("# capacitive strip test bench ready\n");
}

void loop() {
  captureDischarge();
  sendBurst();
  delay(CYCLE_PERIOD_MS);
}
