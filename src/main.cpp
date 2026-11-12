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

// DC leak test: pin is HIGH forever, node settles at the divider voltage
//   V = V_HIGH * R_leak / (R + R_leak)  ->  R_leak = R * V / (V_HIGH - V)
static void dcLeakTest() {
  uint64_t sum = 0;
  int saturated = 0;
  const int n = 200;
  for (int i = 0; i < n; i++) {
    if (analogRead(SENSE_PIN) >= 4090) saturated++;
    sum += analogReadMilliVolts(SENSE_PIN);  // calibrated mV
    // Slow sampling on purpose: every conversion pulls a little charge from the
    // node, and through a megaohm resistor fast sampling would itself look like a leak.
    delay(1);
  }
  const double v = (double)sum / n;
  Serial.printf("# DC: node = %.0f mV", v);
  if (saturated > n / 2) {
    Serial.print("  ->  ADC at its top limit (node > ~3.1 V): leak too small to see"
                 " with this R; use a bigger R_SERIES\n");
  } else if (v < V_HIGH_MV - 30) {
    const double r_leak = (double)R_SERIES_OHMS * v / (V_HIGH_MV - v);
    Serial.printf("  ->  R_leak ~ %.2f MOhm\n", r_leak / 1e6);
  } else {
    Serial.print("  ->  no measurable leak (node ~ V_HIGH)\n");
  }
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
#if TOUCH_READ_TEST
  // "T <value>": one hardware touch measurement per line.
  pinMode(CHARGE_PIN, INPUT);  // high-Z: keep the RC resistor out of the way
  Serial.printf("T %lu\n", (unsigned long)touchRead(SENSE_PIN));
  delay(TOUCH_PERIOD_MS);
  return;
#endif
#if DC_LEAK_TEST
  digitalWrite(CHARGE_PIN, HIGH);
  dcLeakTest();
  delay(500);
  return;
#endif
  captureDischarge();
  sendBurst();
  delay(CYCLE_PERIOD_MS);
}
