#ifndef __ESP_CHAT_SHARING_H__
#define __ESP_CHAT_SHARING_H__
#if !defined(LITE_VERSION)
#include "esp_connection.h"

// Broadcast chat over ESP-NOW, built on top of the shared EspConnection
// infrastructure (same base class used by FileSharing). Every Bruce device
// with the chat screen open on the same WiFi channel sees every message -
// no pairing/target selection step.
class ChatSharing : public EspConnection {
public:
    ChatSharing();

    // Entry point, call as ChatSharing().run();
    void run();

private:
    static constexpr uint8_t HISTORY_SIZE = 20; // fixed-size ring buffer, no heap growth over time

    struct ChatLine {
        String   text;
        uint32_t timestampMs = 0;
        bool     self        = false;
    };

    ChatLine history[HISTORY_SIZE];
    uint8_t  historyHead  = 0; // next write slot
    uint8_t  historyCount = 0;
    bool     screenDirty  = true;

    void pushLine(const String &text, bool self);
    void processIncoming();
    void sendChatMessage(const String &text);
    void render();

    // Locks WiFi to a single fixed channel so both sides are guaranteed to
    // match, disconnecting any leftover STA connection first (ESP-IDF
    // refuses a manual channel change while actually associated to an AP).
    // Returns false (and shows an error) if the channel could not be set.
    bool lockWifiChannel();

    // "12s" / "3m" / "1h" instead of a raw, ever-growing second count.
    static String formatAgo(uint32_t timestampMs);

    // How many wrapped screen-lines this line will take at the given
    // indent, using the exact same width math padprintln() uses
    // internally - keeps our line-budget estimate accurate so nothing
    // gets cut off at the bottom of the screen.
    static int wrappedLineCount(const String &text, int16_t padx);
};

#endif
#endif
