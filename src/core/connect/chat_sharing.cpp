#if !defined(LITE_VERSION)
#include "chat_sharing.h"
#include "core/display.h"
#include "core/mykeyboard.h"

ChatSharing::ChatSharing() {}

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

    Message message      = createMessage(text);
    esp_err_t response    = esp_now_send(broadcastAddress, (uint8_t *)&message, sizeof(message));

    if (response != ESP_OK) {
        Serial.printf("Chat send response: %s\n", esp_err_to_name(response));
        displayError("Gagal kirim pesan");
        delay(500);
        screenDirty = true; // repaint chat screen after the error banner
        return;
    }

    pushLine(text, true);
}

// Only redraws when something actually changed (new message in/out), so
// the screen doesn't flicker while idling in the input loop.
void ChatSharing::render() {
    if (!screenDirty) return;
    screenDirty = false;

    drawMainBorderWithTitle("ESP-NOW Chat");

    uint8_t start = historyCount > MAX_LINES ? historyCount - MAX_LINES : 0;
    for (uint8_t i = start; i < historyCount; i++) {
        uint8_t idx = (historyHead + HISTORY_SIZE - historyCount + i) % HISTORY_SIZE;
        ChatLine &line = history[idx];

        uint32_t secAgo = (millis() - line.timestampMs) / 1000;
        String prefix   = line.self ? "> " : "< ";

        padprintln(prefix + "[" + String(secAgo) + "s] " + line.text);
    }

    padprintln("");
    padprintln("SEL: type msg | ESC: exit");
}

void ChatSharing::run() {
    drawMainBorderWithTitle("ESP-NOW Chat");
    padprintln("");
    padprintln("Starting radio...");

    // beginEspnow() is inherited (protected) from EspConnection: it sets
    // WiFi to STA mode, calls esp_now_init(), registers the broadcast peer
    // and installs the send/recv callbacks. Its destructor (~EspConnection)
    // tears all of that down again automatically when this object goes out
    // of scope, so cleanup is guaranteed even on early return/ESC.
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
