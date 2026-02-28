#include "core/powerSave.h"
#include "core/utils.h"
#include <Button.h>
#include <globals.h>
#include <interface.h>

// ===== BUTTON FLAGS =====
volatile bool nxtPress = false;
volatile bool prvPress = false;
volatile bool ecPress = false;
volatile bool slPress = false;
volatile bool holdNext = false;
volatile bool holdPrev = false;

// ===== SCREEN STATE =====
volatile bool isScreenDimmed = false; 

// ===== POWER OFF FLAGS =====
volatile bool powerOffReq = false;
bool powerOffCountdown = false;
unsigned long powerOffStart = 0;
int lastCount = -1;

// dw btn dim toggle
void toggleScreen() {
    if (isScreenDimmed) {
        _setBrightness(bruceConfig.brightness); 
        isScreenDimmed = false;
    } else {
        _setBrightness(0); 
        isScreenDimmed = true;
    }
}

// ===== CALLBACKS BTN1 (DW_BTN) =====
static void onButtonSingleClickCb1(void *button_handle, void *usr_data) { slPress = true; }
static void onButtonHoldCb1(void *button_handle, void *usr_data) { holdNext = true; }
static void onButtonDoubleClickCb1(void *button_handle, void *usr_data) { toggleScreen(); }

// ===== CALLBACKS BTN2 (UP_BTN) =====
static void onButtonSingleClickCb2(void *button_handle, void *usr_data) { ecPress = true; }
static void onButtonHoldCb2(void *button_handle, void *usr_data) { holdPrev = true; }
static void onButtonDoubleClickCb2(void *button_handle, void *usr_data) { powerOffReq = true; }

Button *btn1 = nullptr;
Button *btn2 = nullptr;

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

void _setBrightness(uint8_t brightval) {
    if (brightval == 0) analogWrite(TFT_BL, 0);
    else {
        int bl = MINBRIGHT + round(((255 - MINBRIGHT) * brightval / 100));
        analogWrite(TFT_BL, bl);
    }
}

void InputHandler(void) {
    static unsigned long tm = 0;
    static bool btn_pressed = false;
    static unsigned long holdTimer = 0;

    // Auto scroll hold logic
    if (holdNext || holdPrev) {
        if (millis() - holdTimer > 350) {
            holdTimer = millis();
            if (holdNext) nxtPress = true;
            if (holdPrev) prvPress = true;
        }
        if (digitalRead(DW_BTN) == HIGH) holdNext = false;
        if (digitalRead(UP_BTN) == HIGH) holdPrev = false;
    }

    // Power off countdown logic
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
            // Fix: Pecah assignment volatile
            nxtPress = false; 
            prvPress = false; 
            ecPress = false; 
            slPress = false;
            return;
        }
        int elapsed = (millis() - powerOffStart) / 1000;
        int countDown = 3 - elapsed;
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
            return;
        }
    }

    // Main Input logic
    if (nxtPress || prvPress || ecPress || slPress) {
        if (isScreenDimmed) {
            _setBrightness(bruceConfig.brightness);
            isScreenDimmed = false;          
            // Fix: Pecah assignment volatile
            nxtPress = false; 
            prvPress = false; 
            ecPress = false; 
            slPress = false; 
            tm = millis();
            return;
        }
        btn_pressed = true;
    }

    if (millis() - tm > 200 || LongPress) {
        if (btn_pressed) {
            btn_pressed = false;
            tm = millis();

            if (wakeUpScreen()) {
                // Fix: Pecah assignment volatile
                nxtPress = false; 
                prvPress = false; 
                ecPress = false; 
                slPress = false;
                return; 
            }
            
            AnyKeyPress = true;
            SelPress  = slPress;
            EscPress  = ecPress;
            NextPress = nxtPress;
            PrevPress = prvPress;

            // Fix: Pecah assignment volatile
            nxtPress = false; 
            prvPress = false; 
            ecPress = false; 
            slPress = false;
        }
    }
}

void powerOff() {
    tft.fillScreen(bruceConfig.bgColor);
    digitalWrite(TFT_BL, LOW);
    tft.writecommand(0x10);
    digitalWrite(ADC_EN, LOW);
    esp_sleep_enable_ext0_wakeup((gpio_num_t)UP_BTN, BTN_ACT);
    esp_deep_sleep_start();
}
