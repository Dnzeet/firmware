#include "core/powerSave.h"
#include "core/utils.h"
#include <Button.h>
#include <globals.h>
#include <interface.h>

// [ Variabel Button Flags ]
volatile bool nxtPress = false;
volatile bool prvPress = false;
volatile bool ecPress  = false;
volatile bool slPress  = false;
volatile bool holdNext = false;
volatile bool holdPrev = false;
volatile bool powerOffReq = false;

// [ Variabel State Power Off ]
bool powerOffCountdown = false;
unsigned long powerOffStart = 0;
int lastCount = -1;

Button *btn1 = nullptr;
Button *btn2 = nullptr;

// [ Callback Handler Tombol 1 - Down ]
static void onButtonSingleClickCb1(void *b, void *d) { slPress = true; }
static void onButtonHoldCb1(void *b, void *d)        { holdNext = true; }
static void onButtonDoubleClickCb1(void *b, void *d) { nxtPress = true; }

// [ Callback Handler Tombol 2 - Up ]
static void onButtonSingleClickCb2(void *b, void *d) { ecPress = true; }
static void onButtonHoldCb2(void *b, void *d)        { holdPrev = true; }
static void onButtonDoubleClickCb2(void *b, void *d) { powerOffReq = true; }

// [ Inisialisasi Hardware & Button ]
void _setup_gpio() {
    pinMode(DW_BTN, INPUT_PULLUP);
    pinMode(UP_BTN, INPUT_PULLUP);

    if (btn1 == nullptr) {
        button_config_t bt1 = {
            .type = BUTTON_TYPE_GPIO,
            .long_press_time = 200,
            .short_press_time = 120,
            .gpio_button_config = { .gpio_num = DW_BTN, .active_level = 0 },
        };
        btn1 = new Button(bt1);
        btn1->attachSingleClickEventCb(&onButtonSingleClickCb1, NULL);
        btn1->attachLongPressStartEventCb(&onButtonHoldCb1, NULL);
        btn1->attachDoubleClickEventCb(&onButtonDoubleClickCb1, NULL);
    }

    if (btn2 == nullptr) {
        button_config_t bt2 = {
            .type = BUTTON_TYPE_GPIO,
            .long_press_time = 200,
            .short_press_time = 120,
            .gpio_button_config = { .gpio_num = UP_BTN, .active_level = 0 },
        };
        btn2 = new Button(bt2);
        btn2->attachSingleClickEventCb(&onButtonSingleClickCb2, NULL);
        btn2->attachLongPressStartEventCb(&onButtonHoldCb2, NULL);
        btn2->attachDoubleClickEventCb(&onButtonDoubleClickCb2, NULL);
    }

    pinMode(ADC_EN, OUTPUT);
    digitalWrite(ADC_EN, HIGH);
    bruceConfigPins.rfModule = CC1101_SPI_MODULE;
    bruceConfigPins.rfidModule = PN532_I2C_MODULE;
    bruceConfigPins.irRx = RXLED;
    bruceConfigPins.irTx = TXLED;
    Serial.begin(115200);
}

// [ Pengaturan Kecerahan Layar ]
void _setBrightness(uint8_t brightval) {
    if (brightval == 0) analogWrite(TFT_BL, 0);
    else {
        int bl = MINBRIGHT + round(((255 - MINBRIGHT) * brightval / 100));
        analogWrite(TFT_BL, bl);
    }
}

// [ Perintah Masuk Mode Deep Sleep ]
void powerOff() {
    tft.fillScreen(bruceConfig.bgColor);
    digitalWrite(TFT_BL, LOW);
    tft.writecommand(0x10);
    digitalWrite(ADC_EN, LOW);
    esp_sleep_enable_ext0_wakeup((gpio_num_t)UP_BTN, BTN_ACT);
    esp_deep_sleep_start();
}

// [ Manager Power Off - Panggil di Menu Utama saja ]
void PowerManager() {
    if (powerOffReq) {
        powerOffReq = false;
        powerOffCountdown = true;
        powerOffStart = millis();
        lastCount = -1;
    }

    if (powerOffCountdown) {
        if (nxtPress || prvPress || ecPress || slPress || holdNext || holdPrev) {
            powerOffCountdown = false;
            tft.fillRect(60, 12, 16 * LW, tft.fontHeight(1), bruceConfig.bgColor);
            nxtPress = prvPress = ecPress = slPress = false; 
            return;
        }

        int countDown = 3 - ((millis() - powerOffStart) / 1000);
        if (countDown != lastCount && countDown >= 0) {
            lastCount = countDown;
            tft.setCursor(60, 12);
            tft.setTextSize(1);
            tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
            tft.printf(" PWR OFF IN %d  ", countDown);
        }

        if (countDown < 0) {
            powerOffCountdown = false;
            powerOff();
        }
    }
}

// [ Handler Input Navigasi - Panggil di Semua Menu ]
void InputHandler() {
    static unsigned long tm = 0;
    static unsigned long holdTimer = 0;
    static bool btn_pressed = false;

    // Logika Auto-Scroll Hold
    if (holdNext || holdPrev) {
        if (millis() - holdTimer > 350) {
            holdTimer = millis();
            if (holdNext) nxtPress = true;
            if (holdPrev) prvPress = true;
        }
        if (digitalRead(DW_BTN) == HIGH) holdNext = false;
        if (digitalRead(UP_BTN) == HIGH) holdPrev = false;
    }

    if (nxtPress || prvPress || ecPress || slPress) btn_pressed = true;

    if (millis() - tm > 200 || LongPress) {
        if (btn_pressed) {
            btn_pressed = false;
            tm = millis();

            if (wakeUpScreen()) {
                nxtPress = prvPress = ecPress = slPress = powerOffReq = false;
                return; 
            }
            
            AnyKeyPress = true;
            SelPress  = slPress;
            EscPress  = ecPress;
            NextPress = nxtPress;
            PrevPress = prvPress;

            nxtPress = prvPress = ecPress = slPress = false;
        }
    }
}
