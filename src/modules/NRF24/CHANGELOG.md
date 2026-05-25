# NRF24 Module — Patch Changelog

**Date:** 2026-05-25  
**Branch:** Ttgo  
**Files Modified:** `nrf_spectrum.cpp`, `nrf_jammer.cpp`, `nrf_jammer_api.cpp`

---

## Bug Fixes

---

### [Bug #1] `nrf_spectrum.cpp` — Missing `nrf_setMode()` before `nrf_start()`

**Severity:** Medium  
**Symptom:** Spectrum analyzer gagal init diam-diam di hardware tertentu (board dengan shared SPI bus, UART bridge, dll). Tidak ada error message, layar hanya blank.

**Root Cause:**  
`nrf_spectrum()` memanggil `nrf_start(NRF_MODE_SPI)` secara hardcode tanpa memanggil `nrf_setMode()` terlebih dahulu. Pada board tertentu, SPI bus perlu dipilih secara eksplisit oleh caller sebelum `nrf_start()` dapat menginisialisasi radio dengan benar. Pattern ini tidak konsisten dengan `nrf_jammer()` yang selalu memanggil `nrf_setMode()` dulu.

**Fix:**  
Tambahkan `nrf_setMode()` sebelum `nrf_start()` di `nrf_spectrum()`, plus guard untuk `returnToMenu` dan `NRF_MODE_DISABLED`. Sekarang user akan mendapat menu pilihan SPI/UART/BOTH seperti di module lain.

```cpp
// BEFORE:
if (nrf_start(NRF_MODE_SPI)) { ... }

// AFTER:
NRF24_MODE specMode = nrf_setMode();
if (returnToMenu || specMode == NRF_MODE_DISABLED) return;
if (nrf_start(specMode)) { ... }
```

---

### [Bug #2] `nrf_spectrum.cpp` — Manual CE Pin Toggle Konflik dengan RF24 Library

**Severity:** Critical — **Root cause utama Spectrum tidak menampilkan sinyal**  
**Symptom:** Spectrum analyzer muncul tapi semua bar kosong/nol, tidak ada sinyal terdeteksi sama sekali meskipun ada perangkat 2.4GHz aktif di sekitar.

**Root Cause:**  
Di `scanChannels()`, CE pin (io0) di-toggle manual via `digitalWrite()`:
```cpp
digitalWrite(bruceConfigPins.NRF24_bus.io0, LOW);  // sebelum loop
// ... scan loop ...
digitalWrite(bruceConfigPins.NRF24_bus.io0, HIGH); // sesudah loop
```
RF24 library memiliki internal state tracking untuk CE pin. Ketika CE di-set LOW secara eksternal, library tidak tahu hal ini — sehingga ketika `startListening()` dipanggil di dalam loop, library menganggap CE sudah HIGH dan tidak benar-benar mengaktifkan radio receiver. Akibatnya `testRPD()` selalu mengembalikan 0.  

Ironisnya, komentar di `nrf_jammer.cpp` sendiri sudah memperingatkan hal ini:  
> *"Let RF24 library manage CE pin — manual digitalWrite conflicts with library's internal CE state tracking"*

**Fix:**  
Hapus kedua `digitalWrite(bruceConfigPins.NRF24_bus.io0, ...)` dari `scanChannels()`. RF24 library mengelola CE secara internal melalui `startListening()` dan `stopListening()`.

---

### [Bug #3] `nrf_jammer_api.cpp` — `stopBLEJammer()` Memanggil `nrf_setMode()` Saat Cleanup

**Severity:** High  
**Symptom:** Saat BLE jammer dihentikan, user tiba-tiba melihat menu interaktif "SPI Mode / SPI UART / SPI BOTH" yang tidak relevan. Jika user memilih mode berbeda dari saat start, `stopConstCarrier()` bisa tidak dipanggil (radio tetap transmit) atau dipanggil di bus yang salah.

**Root Cause:**  
`stopBLEJammer()` memanggil `nrf_setMode()` untuk menentukan mode sebelum memanggil `stopConstCarrier()`:
```cpp
void stopBLEJammer() {
    NRF24_MODE mode = nrf_setMode(); // <- menampilkan menu interaktif!
    if (CHECK_NRF_SPI(mode)) NRFradio.stopConstCarrier();
    ...
}
```
`nrf_setMode()` adalah fungsi UI yang menampilkan menu ke layar dan menunggu input user — bukan fungsi getter. Ini tidak boleh dipanggil di dalam fungsi cleanup.

**Fix:**  
Tambahkan static variable `activeNRFMode` yang menyimpan mode yang dipilih saat `isNRF24Available()` (initialization). `stopBLEJammer()` menggunakan variable ini untuk cleanup tanpa perlu menampilkan menu.

```cpp
// Tambahan:
static NRF24_MODE activeNRFMode = NRF_MODE_DISABLED;

// Di isNRF24Available():
activeNRFMode = mode; // simpan saat init

// Di stopBLEJammer():
if (CHECK_NRF_SPI(activeNRFMode)) NRFradio.stopConstCarrier(); // pakai yg tersimpan
```

---

### [Bug #4] `nrf_jammer.cpp` — `hopIndex` Tidak Sinkron dengan Channel CW Initialization

**Severity:** Medium  
**Symptom:** Pada mode jammer dengan channel list (BLE, BLE_ADV, WiFi, dst.), carrier CW tidak stabil / sempat bergetar di iterasi pertama jamming loop. Terlihat sebagai brief glitch saat jammer baru mulai.

**Root Cause:**  
Di `runJammer()`, channel untuk `initCW()` di-clamp ke minimum 50 (per requirement hardware nRF24 untuk CW mode):
```cpp
int channel = initChannels[0]; // mis. BLE_ADV ch[0] = 2
if (channel < 50) channel = 50; // di-override ke 50 → initCW(50)
```
Tapi `hopIndex` tetap `0`, sehingga iterasi pertama loop jamming langsung mengambil `initChannels[0]` (= 2), memanggil `cwChannel(2, 0)`. CW di-init di ch50 tapi langsung di-hop ke ch2 — menyebabkan PLL re-lock yang tidak smooth di iterasi pertama.

**Fix:**  
Cari index pertama dalam channel list yang memiliki nilai >= 50, set `hopIndex` ke index tersebut agar loop jamming mulai tepat di channel yang sama dengan `initCW()`. Jika tidak ada channel >= 50 dalam list (e.g. BLE_ADV hanya punya ch 2, 26, 80), fallback ke index 0.

```cpp
// Cari startHopIndex = index pertama dengan channel >= 50
int startHopIndex = 0;
for (size_t idx = 0; idx < initCount; idx++) {
    if (initChannels[idx] >= 50) { startHopIndex = idx; break; }
}
hopIndex = startHopIndex; // sync!
int channel = initChannels[startHopIndex]; // CW init di channel yg sama
initCW(channel);
```

---

## Files Changed

| File | Bugs Fixed | Lines Changed |
|------|-----------|---------------|
| `nrf_spectrum.cpp` | Bug #1, Bug #2 | ~15 lines |
| `nrf_jammer_api.cpp` | Bug #3 | ~8 lines |
| `nrf_jammer.cpp` | Bug #4 | ~18 lines |

## Files Unchanged

`nrf_common.h`, `nrf_common.cpp`, `nrf_jammer.h`, `nrf_jammer_api.h`,
`nrf_spectrum.h`, `nrf_mousejack.h`, `nrf_mousejack.cpp`


---

### [Bug #5] `nrf_common.cpp` — CE Pin Di-set LOW Manual Sebelum `begin()` → **Root Cause Jammer Tidak Transmit**

**Severity:** Critical — **Root cause utama modul NRF tidak anget / tidak transmit**  
**Symptom:** Jammer tampak berjalan normal (UI muncul, tidak ada error "NRF24 not found"), tapi modul tidak panas sama sekali karena tidak ada RF output. Mode CW (`startConstCarrier`) tidak bekerja.

**Root Cause:**  
Di `nrf_start()` dalam `nrf_common.cpp`, sebelum `NRFradio.begin()` dipanggil, CE pin di-set LOW secara manual:

```cpp
pinMode(bruceConfigPins.NRF24_bus.io0, OUTPUT);
digitalWrite(bruceConfigPins.NRF24_bus.io0, LOW); // ← masalah di sini
// ...
NRFradio.begin(NRFSPI, io0, cs); // library init, tapi CE sudah LOW di hardware
```

Ada dua lapisan masalah yang saling memperburuk:

**Layer 1 — Pre-begin CE manipulation:**  
`NRFradio.begin()` menginisialisasi CE pin secara internal dan menetapkan state awalnya. Ketika CE sudah di-set LOW via `digitalWrite` sebelum `begin()`, terjadi desync antara hardware (CE=LOW) dan library internal state — library tidak "tahu" bahwa pin sudah dimanipulasi sebelum ia sempat mengambil alih kontrol.

**Layer 2 — Konstruktor RF24 dengan pin -1:**  
`NRFradio` dikonstruksi dengan `RF24 NRFradio(NRF24_CE_PIN, NRF24_SS_PIN)` dimana keduanya adalah `-1` (lihat `nrf_common.h`). Pin sebenarnya baru dipass saat `begin()`. Pada beberapa versi RF24 library, internal `ce()` call pada `startConstCarrier()` menggunakan pin yang diregister saat konstruktor — jika `-1`, CE toggle tidak terjadi di hardware → tidak ada carrier.

Kombinasi keduanya menyebabkan `startConstCarrier()` tidak pernah benar-benar men-drive CE HIGH di hardware → tidak ada RF output → modul dingin.

**Kenapa firmware lama tidak bermasalah:**  
Firmware lama kemungkinan besar memanggil `begin()` dengan pin langsung (tanpa pre-begin `digitalWrite`), atau menggunakan versi RF24 library yang handle `-1` pin constructor secara berbeda.

**Fix:**  
Hapus `pinMode` dan `digitalWrite` CE (io0) dari `nrf_start()`. RF24 library menginisialisasi dan mengontrol CE pin secara penuh melalui `begin()` — tidak perlu dan tidak boleh di-set manual sebelumnya.

```cpp
// DIHAPUS dari nrf_start():
// pinMode(bruceConfigPins.NRF24_bus.io0, OUTPUT);
// digitalWrite(bruceConfigPins.NRF24_bus.io0, LOW);
```

CS pin (`cs`) tetap di-set HIGH secara manual sebelum `begin()` — ini benar dan tidak diubah, karena CS adalah active-low dan harus idle HIGH sebelum SPI bus aktif.

---

## Files Changed (Updated)

| File | Bugs Fixed | Keterangan |
|------|-----------|------------|
| `nrf_spectrum.cpp` | Bug #1, Bug #2 | Mode selection + CE toggle conflict |
| `nrf_jammer.cpp` | Bug #4 | hopIndex sync dengan CW init |
| `nrf_jammer_api.cpp` | Bug #3 | stopBLEJammer cleanup tanpa menu popup |
| `nrf_common.cpp` | **Bug #5** | **Root cause jammer tidak transmit** |
| `nrf_common.h` | Bug #5 | Dokumentasi pin -1 behavior |

## Files Unchanged

`nrf_jammer.h`, `nrf_jammer_api.h`, `nrf_spectrum.h`,
`nrf_mousejack.h`, `nrf_mousejack.cpp`

---

## Revisi setelah analisa old firmware (nrf24_old.zip)

Setelah membandingkan langsung dengan old firmware yang work, ditemukan bahwa analisis sebelumnya untuk Bug #2 dan #5 **sebagian keliru**. Berikut koreksinya:

---

### [Bug #5 — REVISED] Root Cause Jammer Tidak Transmit: Gap antara `nrf_start()` dan `initCW()`

**Analisis old firmware:**
Old firmware langsung memanggil `startConstCarrier()` **tepat setelah** `nrf_start()` tanpa ada kode apapun di antara keduanya:
```cpp
if (nrf_start()) {
    NRFradio.setPALevel(RF24_PA_MAX);
    NRFradio.startConstCarrier(RF24_PA_MAX, 50); // langsung!
```

**Masalah di new firmware:**
New firmware memanggil `nrf_start()` di `nrf_jammer()`, lalu memanggil `runJammer()` yang di dalamnya ada puluhan baris setup variable, hopIndex calculation, drawJammerStatus(), dll — **sebelum** `initCW()` dipanggil. Gap inilah yang menyebabkan CE stuck LOW terlalu lama → tidak ada RF output.

**Fix:**
Tambahkan `initCW()` langsung setelah `nrf_start()` di `nrf_jammer()`, sebelum `runJammer()` dipanggil. Ini mirrors behavior old firmware persis.

---

### [Bug #2 — REVISED] CE Toggle di Spectrum adalah Intentional, Bukan Bug

**Analisis old firmware:**
Old spectrum juga toggle CE manual — tapi pakai `NRF24_CE_PIN` yang merupakan pin asli dari board define:
```cpp
digitalWrite(NRF24_CE_PIN, LOW);  // NRF24_CE_PIN = pin asli, bukan -1
// ... scan ...
digitalWrite(NRF24_CE_PIN, HIGH);
```

**Masalah di new firmware:**
New spectrum mencoba hal yang sama dengan `bruceConfigPins.NRF24_bus.io0`, yang merupakan equivalent yang benar — **toggle setelah `begin()` valid** karena library sudah meregister pin tersebut. Bug sebenarnya bukan di adanya toggle, tapi di **urutan init** (lihat Bug #1).

**Fix:**
Kembalikan CE toggle di spectrum menggunakan `bruceConfigPins.NRF24_bus.io0` — ini sudah benar.

