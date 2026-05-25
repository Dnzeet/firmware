#include "rf_jammer.h"
#include "core/display.h"
#include "rf_utils.h"
#include <esp_random.h>   // FIX #4: hardware RNG

// ─────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────
static const uint32_t MAX_JAM_TIME_MS  = 20000;
static const uint32_t MAX_SEQUENCE     = 50;
static const uint32_t DURATION_CYCLES  = 3;

// FIX #1: explicit half-period for the full jammer carrier (µs).
// 50 µs HIGH + 50 µs LOW → ~10 kHz square wave that creates broadband noise.
static const uint32_t FULL_JAM_HALF_US = 50;

// ─────────────────────────────────────────────
// Construction / destruction
// ─────────────────────────────────────────────
RFJammer::RFJammer(bool full) : fullJammer(full) {
    setup();
}

RFJammer::~RFJammer() {
    deinitRfModule();
}

// ─────────────────────────────────────────────
// FIX #5 – helper: guarantee the TX pin is in OUTPUT mode before any
// digitalWrite() call.  The single-pin path in initRfModule() calls
// gsetRfTxPin() which may leave the pin direction unset on some boards.
// ─────────────────────────────────────────────
void RFJammer::ensure_tx_pin_output() {
    pinMode(nTransmitterPin, OUTPUT);
    digitalWrite(nTransmitterPin, LOW);
}

// ─────────────────────────────────────────────
// Setup
// ─────────────────────────────────────────────
void RFJammer::setup() {
    nTransmitterPin = bruceConfigPins.rfTx;
    if (!initRfModule("tx")) return;

    if (bruceConfigPins.rfModule == CC1101_SPI_MODULE) {
        nTransmitterPin = bruceConfigPins.CC1101_bus.io0;
    }

    // FIX #5: ensure OUTPUT direction before first digitalWrite
    ensure_tx_pin_output();

    sendRF       = true;
    returnToMenu = false;
    display_banner();

    if (fullJammer) run_full_jammer();
    else            run_itmt_jammer();
}

// ─────────────────────────────────────────────
// UI
// ─────────────────────────────────────────────
void RFJammer::display_banner() {
    drawMainBorderWithTitle("RF Jammer");
    printSubtitle(String(fullJammer ? "Full Jammer" : "Intermittent Jammer"));
    padprintln("Sending...");
    padprintln("");
    padprintln("");
    tft.setTextColor(getColorVariation(bruceConfig.priColor), bruceConfig.bgColor);
    padprintln("Press [ESC] for options.");
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
}

// ─────────────────────────────────────────────
// FIX #1 – Full Jammer
//
// OLD: used `micros() % 100 < 2` and `currentTime % 500 < 10` as
//      toggle conditions.  On a fast ESP32 loop micros() can jump by
//      more than 2 ticks per iteration, so those windows were almost
//      never entered and the pin stayed HIGH the whole time (DC output,
//      not a jamming signal).
//
// NEW: explicit symmetric square wave using delayMicroseconds().
//      50 µs HIGH / 50 µs LOW ≈ 10 kHz carrier — reliably toggles the
//      pin every iteration and produces actual broadband noise.
// ─────────────────────────────────────────────
void RFJammer::run_full_jammer() {
    uint32_t startTime     = millis();
    uint32_t lastCheckTime = startTime;

    while (sendRF) {
        // Symmetric square wave — guaranteed to toggle every iteration
        digitalWrite(nTransmitterPin, HIGH);
        delayMicroseconds(FULL_JAM_HALF_US);
        digitalWrite(nTransmitterPin, LOW);
        delayMicroseconds(FULL_JAM_HALF_US);

        // Check buttons / timeout at ~100 ms intervals to avoid overhead
        uint32_t now = millis();
        if (now - lastCheckTime >= 100) {
            lastCheckTime = now;
            if (check(EscPress) || (now - startTime >= MAX_JAM_TIME_MS)) {
                sendRF       = false;
                returnToMenu = true;
            }
        }
    }

    digitalWrite(nTransmitterPin, LOW);
}

// ─────────────────────────────────────────────
// FIX #3 – Intermittent Jammer
//
// OLD: sequenceValues[i] = 10 * (i + 1) → 10, 20, … 500 µs (linear).
//      Linear spacing means the jammer sweeps only a narrow band and
//      produces a predictable pattern that most receivers can ignore.
//
// NEW: pulse widths are randomised with esp_random() (hardware RNG) in
//      the range 10–499 µs so the sweep is unpredictable and covers a
//      much wider slice of the spectrum each pass.
// ─────────────────────────────────────────────
void RFJammer::run_itmt_jammer() {
    uint32_t startTime     = millis();
    uint32_t lastCheckTime = startTime;

    uint32_t sequenceValues[MAX_SEQUENCE];

    // FIX #3: randomised sequence instead of linear 10*i
    for (int i = 0; i < (int)MAX_SEQUENCE; i++) {
        sequenceValues[i] = 10 + (esp_random() % 490);   // 10–499 µs
    }

    while (sendRF) {
        for (int sequence = 0; sequence < (int)MAX_SEQUENCE && sendRF; sequence++) {
            uint32_t pulseWidth = sequenceValues[sequence];

            for (int dur = 0; dur < (int)DURATION_CYCLES && sendRF; dur++) {
                send_optimized_pulse(pulseWidth);

                uint32_t now = millis();
                if (now - lastCheckTime >= 50) {
                    lastCheckTime = now;
                    if (check(EscPress) || (now - startTime >= MAX_JAM_TIME_MS)) {
                        sendRF       = false;
                        returnToMenu = true;
                    }
                }
            }
        }

        // Randomise the sequence again for the next sweep
        if (sendRF) {
            for (int i = 0; i < (int)MAX_SEQUENCE; i++) {
                sequenceValues[i] = 10 + (esp_random() % 490);
            }
            send_random_pattern(100);
        }
    }

    digitalWrite(nTransmitterPin, LOW);
}

// ─────────────────────────────────────────────
// FIX #2 – send_optimized_pulse()
//
// OLD: `uint32_t lowPeriod = width + (width % 23);`
//      width % 23 yields 0–22 µs — a tiny, pseudo-random addition that
//      does not create a meaningful duty cycle and wastes µs in an inner
//      loop of `delayMicroseconds(10)` calls instead of one clean delay.
//
// NEW: lowPeriod == width → exact 50 % duty cycle.  The inner delay
//      loops are also collapsed into single delayMicroseconds() calls
//      to reduce overhead and make timing more deterministic.
// ─────────────────────────────────────────────
void RFJammer::send_optimized_pulse(uint32_t width) {
    // HIGH phase
    digitalWrite(nTransmitterPin, HIGH);
    delayMicroseconds(width);

    // LOW phase — FIX #2: symmetric 50 % duty cycle
    digitalWrite(nTransmitterPin, LOW);
    delayMicroseconds(width);   // was: width + (width % 23)
}

// ─────────────────────────────────────────────
// FIX #4 – send_random_pattern()
//
// OLD: `millis() % 46` and `micros() % 96` used as pulse/space widths.
//      millis() increments monotonically, so `millis() % 46` cycles
//      0→45→0→45… in a completely predictable, periodic pattern —
//      the opposite of "random".
//
// NEW: esp_random() (ESP32 hardware TRNG) for true randomness.
// ─────────────────────────────────────────────
void RFJammer::send_random_pattern(int numPulses) {
    uint32_t startTime = millis();

    for (int i = 0; i < numPulses && sendRF; i++) {
        // FIX #4: hardware RNG instead of millis()/micros() modulo
        uint32_t pulseWidth = 5 + (esp_random() % 46);   // 5–50 µs
        uint32_t spaceWidth = 5 + (esp_random() % 96);   // 5–100 µs

        digitalWrite(nTransmitterPin, HIGH);
        delayMicroseconds(pulseWidth);

        digitalWrite(nTransmitterPin, LOW);
        delayMicroseconds(spaceWidth);

        if (millis() - startTime > 100) break;
    }
}
