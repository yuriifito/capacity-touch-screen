#pragma once
// ============================================================================
//  Test-bench configuration: ONE copper strip.
//  Edit the values below to match your wiring.
// ============================================================================
//
//  Wiring (one strip):
//
//     CHARGE_PIN ──[ R ]──┬── SENSE_PIN (ADC1)
//                         │
//                        ═╪═  C  (copper strip, or a test capacitor)
//                         │
//                        GND
//
//  Cycle: CHARGE_PIN = HIGH -> C charges to 3.3 V through R.
//         CHARGE_PIN = LOW  -> C discharges through the same R into the pin;
//         SENSE_PIN samples the voltage with ADC1 (exponential, tau = R*C).
//  A finger adds capacitance -> larger RC -> slower discharge.

// ---- Pins ------------------------------------------------------------------
// SENSE_PIN must be an ADC1 pin. On ESP32-S3: GPIO1 .. GPIO10.
// (ADC2 conflicts with Wi-Fi, so we stay on ADC1.)
#define SENSE_PIN   4
// Any free GPIO that can be an output.
#define CHARGE_PIN  5

// ---- Measurement -----------------------------------------------------------
// Pick these from tau = R*C (now R = 1 MOhm, C = 160 pF -> tau = 160 us).
// One ADC sample takes ~61 us.
#define CHARGE_TIME_US     1000   // >= 5*tau, so C reaches ~3.3 V
#define NUM_SAMPLES        20     // ~20 * 61 us = 1.2 ms ~ 7*tau: whole decay visible
#define SAMPLE_INTERVAL_US 0      // extra delay between samples (0 = as fast as possible)
#define CYCLE_PERIOD_MS    20     // pause between charge/discharge cycles

// ---- DC leak test ----------------------------------------------------------
// 1 = keep CHARGE_PIN HIGH all the time and print the steady node voltage
//     plus the estimated leak resistance (no discharge curves).
// 0 = normal charge/discharge measurement.
#define DC_LEAK_TEST   0
#define R_SERIES_OHMS  1000000UL  // the resistor between CHARGE_PIN and the node
#define V_HIGH_MV      3300       // CHARGE_PIN HIGH level (measure it with a multimeter)

// ---- Built-in touch sensor test -------------------------------------------
// 1 = use the ESP32-S3 hardware touch sensor: touchRead(SENSE_PIN) instead of
//     the RC discharge. The plate only needs to be on SENSE_PIN (GPIO1..14);
//     CHARGE_PIN is switched to high-Z so the resistor does not interfere.
//     On ESP32-S3 the value goes UP when a finger touches.
#define TOUCH_READ_TEST    1
#define TOUCH_PERIOD_MS    10     // one touchRead() every N ms

// ---- Serial ----------------------------------------------------------------
#define SERIAL_BAUD 921600

// ---- Sanity check ----------------------------------------------------------
static_assert(SENSE_PIN >= 1 && SENSE_PIN <= 10,
              "SENSE_PIN must be an ADC1 pin (GPIO1..GPIO10 on ESP32-S3)");
static_assert(!(DC_LEAK_TEST && TOUCH_READ_TEST),
              "enable only one of DC_LEAK_TEST / TOUCH_READ_TEST");
static_assert(SENSE_PIN != CHARGE_PIN, "SENSE_PIN and CHARGE_PIN must differ");
