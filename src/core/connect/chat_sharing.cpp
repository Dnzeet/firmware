#if !defined(LITE_VERSION)
#include "chat_sharing.h"
#include "core/display.h"
#include "core/mykeyboard.h"
#include <WiFi.h>
#include <esp_wifi.h>

// setupPeer() (in EspConnection) registers peers with channel=0, meaning
// "whatever channel WiFi is on right now". Without ever connecting to an
// AP, that channel is NOT deterministic - it depends on each device's WiFi
// history/NVS state, so two devices can easily end up on different
// channels and never see each other. Pinning both devices to the same
// fixed channel before starting ESP-NOW makes delivery reliable.
static constexpr uint8_t CHAT_WIFI_CHANNEL = 1;

ChatSharing::ChatSharing() {}

bool ChatSharing::lockWifiChannel() {
    // Disconnect any leftover STA connection first: esp_wifi_set_channel()
    // is rejected while actually associated to an AP, which would leave us
    // silently stuck on whatever channel that AP was using.
    WiFi.disconnect(true, true);
    delay(100);
    WiFi.mode(WIFI_STA);
    delay(100);

    esp_err_t result = esp_wifi_set_channel(CHAT_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
    if (result != ESP_OK) {
        Serial.printf("esp_wifi_set_channel failed: %s\n", esp_err_to_name(result));
        displayError("Gagal set WiFi channel");
        delay(1500);
        return false;
    }
    return true;
}

// ---- ring buffer -----------------------------------------------------
// Fixed HISTORY_SIZE slots, oldest entry silently overwritten once full.
// Keeps memory bounded no matter how long the chat session runs, which
// matters on no-PSRAM boards (Cardputer/TTGO) where String churn is the
// usual cause of heap fragmentation crashes.
void ChatSharing::pushLine(const String &text, bool self) {
    ChatLine &line = history[historyHead];
    line.text        = text;
    line.timestampMs = millis();
    line.self        = self;

    historyHead = (historyHead + 1) % HISTORY_SIZE;
    if (historyCount < HISTORY_SIZE) historyCount++;
    screenDirty = true;
}

// Drains whatever EspConnection::onDataRecv queued up. Ping/pong control
// messages never reach recvQueue (the base class intercepts those), so
// everything here is a real chat line from another device.
void ChatSharing::processIncoming() {
    while (!recvQueue.empty()) {
        Message msg = recvQueue.front();
        recvQueue.erase(recvQueue.begin());

        if (msg.dataSize == 0) continue;

        String text;
        size_t len = min(msg.dataSize, (size_t)ESP_DATA_SIZE);
        text.reserve(len);
        for (size_t i = 0; i < len; i++) text += msg.data[i];

        pushLine(text, false);
    }
}

void ChatSharing::sendChatMessage(const String &text) {
    if (text.isEmpty()) return;

    Message message   = createMessage(text);
    esp_err_t response = esp_now_send(broadcastAddress, (uint8_t *)&message, sizeof(message));

    if (response != ESP_OK) {
        Serial.printf("Chat send response: %s\n", esp_err_to_name(response));
        displayError("Gagal kirim pesan");
        delay(500);
        screenDirty = true; // repaint chat screen after the error banner
        return;
    }

    pushLine(text, true);
}

String ChatSharing::formatAgo(uint32_t timestampMs) {
    uint32_t s = (millis() - timestampMs) / 1000;
    if (s < 60) return String(s) + "s";
    if (s < 3600) return String(s / 60) + "m";
    return String(s / 3600) + "h";
}

int ChatSharing::wrappedLineCount(const String &text, int16_t padx) {
    // Mirrors padprintln()'s own line-splitting math exactly, so our
    // "will this fit on screen" estimate matches what actually gets drawn.
    int maxCharsInLine = (tftWidth - (padx + 1) * BORDER_PAD_X) / (FP * LW);
    if (maxCharsInLine < 1) maxCharsInLine = 1;
    if (text.isEmpty()) return 1;
    return (text.length() + maxCharsInLine - 1) / maxCharsInLine;
}

// Only redraws when something actually changed (new message in/out), so
// the screen doesn't flicker while idling in the input loop. Renders as
// many of the newest messages as actually fit the screen - long/wrapped
// messages are accounted for so nothing gets cut off at the bottom.
void ChatSharing::render() {
    if (!screenDirty) return;
    screenDirty = false;

    drawMainBorderWithTitle("ESP-NOW Chat");

    int contentTop    = tft.getCursorY();
    int lineHeight     = FP * LH;
    int footerLines    = 1; // "SEL: type | ESC: exit" hint at the bottom
    int availableLines = (tftHeight - contentTop) / lineHeight - footerLines;
    if (availableLines < 1) availableLines = 1;

    // Walk backwards from the newest message, counting how many wrapped
    // screen-lines each one needs, until the budget runs out.
    int usedLines   = 0;
    uint8_t visible = 0;
    for (; visible < historyCount; visible++) {
        uint8_t idx        = (historyHead + HISTORY_SIZE - 1 - visible) % HISTORY_SIZE;
        ChatLine &line      = history[idx];
        int16_t padx        = line.self ? 6 : 1;
        String label        = (line.self ? "You " : "Peer ") + formatAgo(line.timestampMs) + ": " + line.text;
        int lines            = wrappedLineCount(label, padx);
        if (usedLines + lines > availableLines) break;
        usedLines += lines;
    }

    uint8_t start = historyCount - visible;
    for (uint8_t i = start; i < historyCount; i++) {
        uint8_t idx    = (historyHead + HISTORY_SIZE - historyCount + i) % HISTORY_SIZE;
        ChatLine &line = history[idx];
        int16_t padx   = line.self ? 6 : 1;
        String label   = (line.self ? "You " : "Peer ") + formatAgo(line.timestampMs) + ": " + line.text;
        padprintln(label, padx);
    }

    padprintln("SEL: type | ESC: exit", 1);
}

void ChatSharing::run() {
    drawMainBorderWithTitle("ESP-NOW Chat");
    padprintln("");
    padprintln("Setting up radio...");

    if (!lockWifiChannel()) return;

    // beginEspnow() is inherited (protected) from EspConnection: starts
    // esp_now, registers the broadcast peer, installs send/recv callbacks.
    // Its destructor (~EspConnection) tears the radio down again
    // automatically when this object goes out of scope, so cleanup is
    // guaranteed even on early return/ESC.
    if (!beginEspnow()) return;

    historyHead  = 0;
    historyCount = 0;
    screenDirty  = true;

    bool exitChat = false;
    while (!exitChat) {
        InputHandler();
        wakeUpScreen();

        processIncoming();
        render();

        if (check(EscPress)) {
            exitChat = true;
            continue;
        }

        if (check(SelPress)) {
            String msg = keyboard("", ESP_DATA_SIZE - 1, "Type your message:");
            if (msg != "\x1B" && msg.length() > 0) { // ESC from keyboard() returns "\x1B"
                sendChatMessage(msg);
            }
            screenDirty = true; // keyboard drew over the chat screen, force repaint
        }

        delay(20); // keep the loop responsive without pegging the CPU
    }
}
#endif
