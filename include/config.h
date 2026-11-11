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
#define CHARGE_TIME_US     5000   // hold HIGH this long; must be >= 5*R*C to reach 3.3 V
#define NUM_SAMPLES        400    // ADC samples captured per discharge
#define SAMPLE_INTERVAL_US 0      // extra delay between samples (0 = as fast as possible)
#define CYCLE_PERIOD_MS    50     // pause between charge/discharge cycles

// ---- Serial ----------------------------------------------------------------
#define SERIAL_BAUD 921600

// ---- Sanity check ----------------------------------------------------------
static_assert(SENSE_PIN >= 1 && SENSE_PIN <= 10,
              "SENSE_PIN must be an ADC1 pin (GPIO1..GPIO10 on ESP32-S3)");
static_assert(SENSE_PIN != CHARGE_PIN, "SENSE_PIN and CHARGE_PIN must differ");
