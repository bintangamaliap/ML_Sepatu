#include <DHT.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>

// ======================================================
// WIFI
// ======================================================

const char* ssid     = "KOSANBUTIA_LT2";
const char* password = "01234321";

// ======================================================
// TELEGRAM
// ======================================================

const char* botToken = "8660151353:AAHRVk80ZVeM847MOVezLs5Pe7k0JiSk4-I";
String chatID = "1372317660";

WiFiClientSecure secured_client;
UniversalTelegramBot bot(botToken, secured_client);

// ======================================================
// ML SERVER
// ======================================================

const char* ML_SERVER = "https://web-production-d5583.up.railway.app/prediksi";

// ======================================================
// PREFERENCES
// ======================================================

Preferences preferences;

// ======================================================
// DHT11
// ======================================================

#define DHTPIN   13
#define DHTTYPE  DHT11
DHT dht(DHTPIN, DHTTYPE);

// ======================================================
// LCD
// ======================================================

LiquidCrystal_I2C lcd(0x27, 16, 2);

// ======================================================
// RELAY
// ======================================================

#define RELAY1_PIN  15
#define RELAY2_PIN  16
#define DRYER_PIN   17
#define LAMP_PIN    18

// ======================================================
// VARIABLE
// ======================================================

String jenisSepatu      = "-";
float  suhuTarget       = 35.0;
int    waktuTargetMenit = 60;
float  batasOFF         = 20.0;

bool pengeringAktif       = false;
bool relayMenyala         = false;
bool notifRecoveryPerlu   = false; // flag: perlu kirim notif recovery setelah boot

// ======================================================
// RECOVERY
// ======================================================

bool          recoveryMenunggu = false;
unsigned long elapsedRecovery  = 0;
unsigned long waktuMulai       = 0;

// ======================================================
// CATATAN DESAIN: TIDAK ADA FASE DETEKSI KEBASAHAN TERPISAH
// ======================================================
// Sempat dicoba pendekatan "fase deteksi 8 menit berbasis kenaikan
// kelembaban" -- dibongkar karena TIDAK DIPERLUKAN. Terbukti dari
// data pengujian riil: karena prosedur fisik pengguna adalah
// MEMASUKKAN sepatu basah ke chamber DULU, baru pilih jenis di
// Telegram, maka pembacaan sensor tunggal di awal mulaiPengering()
// (sebelum pemanas nyala) SUDAH merefleksikan kondisi kebasahan
// dengan baik -- terbukti dari korelasi kuat suhu_awal/kelembapan_awal
// dengan durasi kering aktual di data riil (lihat model_linear_v4).


// ======================================================
// DETEKSI KERING (HYBRID: SENSOR + SAFETY TIMER)
// ======================================================
// Alasan pendekatan hybrid:
// - waktuTargetMenit (dari ML) tetap dipakai sebagai SAFETY CUTOFF /
//   batas atas, supaya alat tetap punya jaminan berhenti walau sensor
//   gagal baca terus-menerus.
// - Tapi kalau kondisi sensor sudah menunjukkan kering SECARA STABIL
//   (bukan cuma sekali baca, karena DHT11 rawan glitch di suhu ~45C),
//   alat berhenti lebih awal dari prediksi ML.

bool          sedangStabilKering = false;
unsigned long waktuMulaiStabil   = 0;

// Ambang kelembaban yang dianggap "kering" — pakai <=, bukan ==0,
// supaya tidak terlalu ketat/rapuh terhadap noise sensor.
const float BATAS_KELEMBABAN_KERING = 5.0;   // %
const float TOLERANSI_SUHU_KERING   = 0.5;   // derajat C di bawah target

// Berapa lama kondisi kering harus bertahan berturut-turut sebelum
// dianggap valid (bukan glitch sesaat). Sesuaikan berdasarkan hasil
// pengujian kamu (jenis/ukuran/kebasahan) — mulai dari 5 menit dulu.
const unsigned long STABIL_MINIMAL_MS = 5UL * 60000UL;

// ======================================================
// DETEKSI "TIDAK ADA SEPATU" (HEURISTIK, BUKAN KEPASTIAN)
// ======================================================
// PENTING: ini BUKAN sensor keberadaan objek. Sistem ini tidak
// punya cara pasti membedakan "chamber kosong" dari "sepatu sudah
// kering total". Heuristik di bawah memakai asumsi fisik: sepatu
// basah yang dipanaskan akan MELEPAS uap air dulu (kelembaban naik)
// sebelum akhirnya kering (kelembaban turun). Chamber kosong tidak
// punya sumber uap air, jadi kelembaban tidak akan pernah naik sejak
// awal sesi.
//
// KETERBATASAN YANG PERLU KAMU TERIMA:
// - Kalau sepatu dimasukkan dalam kondisi HAMPIR kering (kadar air
//   sangat sedikit), kenaikan kelembaban mungkin terlalu kecil untuk
//   terdeteksi DHT11 (akurasi sensor ini +-5% RH) -> heuristik ini
//   bisa salah mengira "tidak ada sepatu" padahal ada.
// - Ambang DELTA_KELEMBABAN_MIN di bawah adalah TEBAKAN AWAL, belum
//   dikalibrasi dari data pengujian kamu. Wajib diuji: jalankan 1
//   sesi chamber kosong dan 1 sesi sepatu asli, catat kurva
//   kelembaban dari Serial Monitor, baru tentukan angka yang sesuai.
float         kelembabanAwalSesi   = 100.0;
float         kelembabanMaxSesi    = 0.0;
const float   DELTA_KELEMBABAN_MIN = 3.0;          // % kenaikan minimal yang dianggap "ada objek basah"
const unsigned long GRACE_PERIOD_MS = 15UL * 60000UL; // waktu tunggu sebelum peringatan "kemungkinan kosong"
bool          peringatanKosongTerkirim = false;

// ======================================================
// TIMING
// ======================================================

unsigned long lastTimeBotRan      = 0;
const unsigned long BOT_MTBS      = 3000;
unsigned long lastNotif           = 0;
const unsigned long intervalNotif = 1800000UL;
unsigned long lastSave            = 0;
unsigned long lastLCD             = 0;
bool          lcdSlide            = false;

// ======================================================
// KEYBOARD JSON
// ======================================================

String getKeyboard() {
  return "[[\"👟 Mesh\",\"🧵 Kanvas\"],[\"🥾 Kulit\",\"📊 Status\"]]";
}

// ======================================================
// MATIKAN RELAY
// ======================================================

void matikanRelay() {
  digitalWrite(RELAY1_PIN, HIGH);
  digitalWrite(RELAY2_PIN, HIGH);
  digitalWrite(DRYER_PIN,  HIGH);
  digitalWrite(LAMP_PIN,   HIGH);
  relayMenyala = false;
}

// ======================================================
// SAVE DATA
// ======================================================

void simpanData() {
  unsigned long waktuBerjalan = (millis() - waktuMulai) / 60000;

  preferences.begin("dryer", false);
  preferences.putBool("aktif",     pengeringAktif);
  preferences.putString("jenis",   jenisSepatu);
  preferences.putInt("target",     waktuTargetMenit);
  preferences.putULong("elapsed",  waktuBerjalan);
  preferences.putBool("notifSent", false);
  preferences.end();

  Serial.println("💾 DATA TERSIMPAN - elapsed: " + String(waktuBerjalan) + " menit");
}

// ======================================================
// LOAD DATA
// ======================================================

void loadData() {
  preferences.begin("dryer", true);
  String        savedJenis  = preferences.getString("jenis",    "-");
  bool          aktif       = preferences.getBool("aktif",      false);
  int           savedTarget = preferences.getInt("target",      0);
  unsigned long elapsed     = preferences.getULong("elapsed",   0);
  bool notifSent = false;
  preferences.end();

  bool dataValid = aktif && savedTarget > 0 && elapsed < (unsigned long)savedTarget;
  int sisaWaktu  = savedTarget - (int)elapsed;
  if (sisaWaktu < 5) dataValid = false;

  if (dataValid) {
    jenisSepatu      = savedJenis;
    waktuTargetMenit = savedTarget;
    recoveryMenunggu = true;
    elapsedRecovery  = elapsed;
    pengeringAktif   = false;
    relayMenyala     = false;

    Serial.println("📂 DATA LAMA DITEMUKAN - elapsed: " + String(elapsed)
                   + " / target: " + String(savedTarget)
                   + " / notifSent: " + String(notifSent));

    // Simpan flag perlu kirim notif — pengiriman dilakukan di setup()
    // setelah delay agar TLS Telegram sudah siap
    if (!notifSent) {
      notifRecoveryPerlu = true;
      Serial.println("⚠️ Perlu kirim notif recovery — akan dikirim di setup()");
    } else {
      Serial.println("Notifikasi recovery sudah pernah dikirim, skip.");
    }

  } else {

    jenisSepatu      = "-";
    waktuTargetMenit = 60;
    pengeringAktif   = false;
    relayMenyala     = false;
    recoveryMenunggu = false;

    Serial.println("🆕 SESI BARU - tidak ada data valid");
  }
}

// ======================================================
// MACHINE LEARNING
// ======================================================
int kirimKeML(int jenisKode, float suhuAwal, float kelembapanAwal) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi tidak terhubung");
    return -1;
  }

  WiFiClientSecure client;
  client.setInsecure();

HTTPClient http;

http.begin(client, ML_SERVER);

http.addHeader("Content-Type","application/json");

String jenisStr;

if(jenisKode==0)
    jenisStr="Canvas";
else if(jenisKode==1)
    jenisStr="Leather";
else
    jenisStr="Mesh";

String jsonData="{";

jsonData+="\"jenis_sepatu\":\""+jenisStr+"\",";

jsonData+="\"suhu_awal\":"+String(suhuAwal,2)+",";

jsonData+="\"kelembapan_awal\":"+String(kelembapanAwal,2);

jsonData+="}";

Serial.println(jsonData);

int httpCode=http.POST(jsonData);

if(httpCode>0){

    String response=http.getString();

    Serial.println(response);

}

// ======================================================
// MULAI PENGERING
// ======================================================
// Sensor dibaca LANGSUNG di sini, sebelum pemanas menyala -- ini
// SUDAH BENAR secara fisik karena prosedur pengujian kamu memasukkan
// sepatu basah ke chamber SEBELUM memilih jenis di Telegram. Jadi
// pembacaan ini merefleksikan kondisi sepatu basah yang sudah duduk
// di chamber tertutup, bukan kondisi ambien kosong.

void mulaiPengering(String jenis, int jenisKode, float suhuTargetBaru) {
  float suhuNow       = dht.readTemperature();
  float kelembabanNow = dht.readHumidity();

  if (isnan(suhuNow) || isnan(kelembabanNow)) {
    bot.sendMessage(chatID, "❌ Sensor error! Coba lagi.", "");
    return;
  }

  recoveryMenunggu = false;

  bot.sendMessage(chatID, "🔄 Menghitung prediksi waktu...", "");

  int prediksi = kirimKeML(jenisKode, suhuNow, kelembabanNow);

  if (prediksi <= 0) {
    prediksi = 180;
    bot.sendMessage(chatID,
      "⚠️ Server ML tidak tersedia.\n"
      "Menggunakan waktu default: 180 menit.", "");
  }

  jenisSepatu      = jenis;
  suhuTarget       = suhuTargetBaru;
  waktuTargetMenit = prediksi;
  pengeringAktif   = true;
  relayMenyala     = true;
  waktuMulai       = millis();
  lastSave         = millis();
  lastNotif        = millis();

  // Reset status deteksi kering-stabil untuk sesi baru
  sedangStabilKering = false;
  waktuMulaiStabil   = 0;

  // Reset heuristik deteksi chamber kosong untuk sesi baru
  kelembabanAwalSesi      = kelembabanNow;
  kelembabanMaxSesi       = kelembabanNow;
  peringatanKosongTerkirim = false;

  // Reset flag notifSent karena ini sesi baru
  preferences.begin("dryer", false);
  preferences.putBool("notifSent", false);
  preferences.end();

  digitalWrite(RELAY1_PIN, LOW);
  digitalWrite(RELAY2_PIN, LOW);
  digitalWrite(DRYER_PIN,  LOW);
  digitalWrite(LAMP_PIN,   LOW);

  // Simpan segera agar data tidak hilang jika ESP32 restart
  simpanData();

  bot.sendMessage(chatID,
    "✅ *PENGERING AKTIF!*\n"
    "━━━━━━━━━━━━━━━━━━\n"
    "👟 Jenis    : " + jenis + "\n"
    "🌡️ Target   : " + String(suhuTargetBaru, 1) + " °C\n"
    "🔮 Prediksi : " + String(prediksi) + " menit\n"
    "━━━━━━━━━━━━━━━━━━\n"
    "🔥 Pengering dimulai!",
    "");
}

// ======================================================
// STATUS TELEGRAM
// ======================================================

void kirimStatusTelegram() {
  float suhu       = dht.readTemperature();
  float kelembaban = dht.readHumidity();

  unsigned long waktuBerjalan = pengeringAktif
                                ? (millis() - waktuMulai) / 60000 : 0;

  int sisaWaktu = (int)waktuTargetMenit - (int)waktuBerjalan;
  if (sisaWaktu < 0) sisaWaktu = 0;

  String statusEmoji, statusText;
  if (recoveryMenunggu) {
    statusEmoji = "⏸️";
    statusText  = "Menunggu /lanjut";
  } else if (pengeringAktif && relayMenyala) {
    statusEmoji = "🔥";
    statusText  = "Mengeringkan";
  } else if (pengeringAktif && !relayMenyala) {
    statusEmoji = "🌡️";
    statusText  = "Menunggu suhu turun";
  } else {
    statusEmoji = "💤";
    statusText  = "Standby";
  }

  String pesan =
    "📊 *STATUS SMART SHOE DRYER*\n"
    "━━━━━━━━━━━━━━━━━━\n"
    "👟 Jenis       : " + jenisSepatu + "\n"
    "🌡️ Suhu        : " + (isnan(suhu)       ? "❌ Error" : String(suhu, 1) + " °C") + "\n"
    "💧 Kelembaban  : " + (isnan(kelembaban) ? "❌ Error" : String(kelembaban, 1) + " %") + "\n"
    "🎯 Target Suhu : " + String(suhuTarget, 1) + " °C\n"
    "⏱️ Berjalan    : " + String(waktuBerjalan) + " menit\n"
    "⏳ Sisa Waktu  : " + String(sisaWaktu) + " menit\n"
    "━━━━━━━━━━━━━━━━━━\n"
    + statusEmoji + " Status : " + statusText;

  bot.sendMessage(chatID, pesan, "");
}

// ======================================================
// TELEGRAM HANDLER
// ======================================================

void handleTelegram() {
  int numNewMessages = bot.getUpdates(bot.last_message_received + 1);

  while (numNewMessages) {
    for (int i = 0; i < numNewMessages; i++) {
      String text = bot.messages[i].text;
      Serial.println("📩 Pesan: " + text);

      // /lanjut - konfirmasi recovery
      if (text == "/lanjut") {
        if (recoveryMenunggu) {
          if      (jenisSepatu == "Mesh")   suhuTarget = 45;
          else if (jenisSepatu == "Kanvas") suhuTarget = 45;
          else if (jenisSepatu == "Kulit")  suhuTarget = 45;

          pengeringAktif   = true;
          relayMenyala     = true;
          recoveryMenunggu = false;
          waktuMulai       = millis() - (elapsedRecovery * 60000UL);
          lastSave         = millis();
          lastNotif        = millis();

          sedangStabilKering = false;
          waktuMulaiStabil   = 0;

          preferences.begin("dryer", false);
          preferences.putBool("notifSent", false);
          preferences.end();

          digitalWrite(RELAY1_PIN, LOW);
          digitalWrite(RELAY2_PIN, LOW);
          digitalWrite(DRYER_PIN,  LOW);
          digitalWrite(LAMP_PIN,   LOW);

          int sisaMenit = waktuTargetMenit - (int)elapsedRecovery;
          if (sisaMenit < 0) sisaMenit = 0;

          bot.sendMessage(chatID,
            "✅ *Recovery Dikonfirmasi!*\n"
            "━━━━━━━━━━━━━━━━━━\n"
            "👟 Jenis  : " + jenisSepatu + "\n"
            "⏱️ Sudah  : " + String(elapsedRecovery) + " menit\n"
            "⏳ Sisa   : " + String(sisaMenit) + " menit\n"
            "━━━━━━━━━━━━━━━━━━\n"
            "🔥 Pengering dilanjutkan!",
            "");
        } else {
          bot.sendMessage(chatID,
            "ℹ️ Tidak ada sesi recovery yang menunggu.", "");
        }
      }

      // Pilih jenis sepatu -> langsung mulai fase deteksi kebasahan
      // OTOMATIS (tidak ada lagi input manual tingkat kebasahan)
      else if (text == "👟 Mesh"   || text == "Mesh")   { mulaiPengering("Mesh",   2, 45.0); }
      else if (text == "🧵 Kanvas" || text == "Kanvas") { mulaiPengering("Kanvas", 0, 45.0); }
      else if (text == "🥾 Kulit"  || text == "Kulit")  { mulaiPengering("Kulit",  1, 45.0); }

      // Status
      else if (text == "📊 Status" || text == "Status" || text == "/status") {
        kirimStatusTelegram();
      }

      // Stop
      else if (text == "/stop") {
        if (pengeringAktif || recoveryMenunggu) {
          pengeringAktif   = false;
          recoveryMenunggu = false;
          sedangStabilKering = false;
          matikanRelay();
          preferences.begin("dryer", false);
          preferences.clear();
          preferences.end();
          bot.sendMessage(chatID,
            "🛑 Pengering dihentikan secara manual.", "");
        } else {
          bot.sendMessage(chatID,
            "ℹ️ Pengering tidak sedang aktif.", "");
        }
      }

      // Start / help
      else if (text == "/start" || text == "/help") {
        bot.sendMessageWithReplyKeyboard(
          chatID,
          "👋 *Selamat datang di SMART SHOE DRYER!*\n"
          "━━━━━━━━━━━━━━━━━━\n"
          "Silakan pilih jenis sepatu:\n\n"
          "👟 *Mesh*   — Sepatu kain/rajut\n"
          "🧵 *Kanvas* — Sepatu kanvas\n"
          "🥾 *Kulit*  — Sepatu kulit\n"
          "📊 *Status* — Lihat status pengering\n"
          "━━━━━━━━━━━━━━━━━━\n"
          "📌 Ketik /stop untuk menghentikan\n"
          "📌 Ketik /lanjut untuk recovery sesi",
          "", getKeyboard(), true);
      }

      else {
        bot.sendMessage(chatID,
          "❓ Perintah tidak dikenali.\nKetik /start untuk memulai.", "");
      }
    }

    numNewMessages = bot.getUpdates(bot.last_message_received + 1);
  }
}

// ======================================================
// SETUP
// ======================================================

void setup() {
  Serial.begin(115200);

  dht.begin();
  pinMode(DHTPIN, INPUT);
  delay(2000);

  pinMode(RELAY1_PIN, OUTPUT);
  pinMode(RELAY2_PIN, OUTPUT);
  pinMode(DRYER_PIN,  OUTPUT);
  pinMode(LAMP_PIN,   OUTPUT);
  matikanRelay();

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0); lcd.print("Connecting");
  lcd.setCursor(0, 1); lcd.print("WiFi...");

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n✅ WiFi Connected: " + WiFi.localIP().toString());

  lastSave  = millis();
  lastNotif = millis();

  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Pilih Sepatu");
  lcd.setCursor(0, 1); lcd.print("Via Telegram");
  secured_client.setInsecure();
  delay(4000);

  Serial.println("Sebelum loadData");
  loadData();
  Serial.println("Setelah loadData");

  Serial.println("recoveryMenunggu  : " + String(recoveryMenunggu));
  Serial.println("notifRecoveryPerlu: " + String(notifRecoveryPerlu));

  // ===== DEBUG RECOVERY =====
  Serial.println("=== DEBUG RECOVERY ===");
  Serial.println("recoveryMenunggu : " + String(recoveryMenunggu));
  Serial.println("notifRecoveryPerlu: " + String(notifRecoveryPerlu));
  Serial.println("jenisSepatu      : " + jenisSepatu);
  Serial.println("waktuTargetMenit : " + String(waktuTargetMenit));
  Serial.println("elapsedRecovery  : " + String(elapsedRecovery));
  // ===== END DEBUG =====

  // Kirim notif recovery di sini — SETELAH delay, bukan di dalam loadData()
  if (recoveryMenunggu && notifRecoveryPerlu) {
    int sisaWaktu = waktuTargetMenit - (int)elapsedRecovery;
    if (sisaWaktu < 0) sisaWaktu = 0;

    bool ok = bot.sendMessage(chatID,
      "⚠️ *Sesi Sebelumnya Ditemukan!*\n"
      "━━━━━━━━━━━━━━━━━━\n"
      "👟 Jenis   : " + jenisSepatu + "\n"
      "⏱️ Sudah   : " + String(elapsedRecovery) + " menit\n"
      "🎯 Target  : " + String(waktuTargetMenit) + " menit\n"
      "⏳ Sisa    : " + String(sisaWaktu) + " menit\n"
      "━━━━━━━━━━━━━━━━━━\n"
      "Ketik /lanjut untuk melanjutkan\n"
      "atau pilih jenis sepatu baru untuk memulai ulang.",
      "");

    if (ok) {
      Serial.println("✅ Notif recovery terkirim");
      preferences.begin("dryer", false);
      preferences.putBool("notifSent", true);
      preferences.end();
      notifRecoveryPerlu = false;
    } else {
      Serial.println("❌ Notif recovery GAGAL — akan dicoba ulang di loop()");
    }
  }

  if (!recoveryMenunggu) {
    bot.sendMessageWithReplyKeyboard(
      chatID,
      "👋 *Selamat datang di SMART SHOE DRYER!*\n"
      "━━━━━━━━━━━━━━━━━━\n"
      "Silakan pilih jenis sepatu:\n\n"
      "👟 *Mesh*   — Sepatu kain/rajut\n"
      "🧵 *Kanvas* — Sepatu kanvas\n"
      "🥾 *Kulit*  — Sepatu kulit\n"
      "📊 *Status* — Lihat status pengering",
      "", getKeyboard(), true);
  }
}

// ======================================================
// LOOP
// ======================================================

void loop() {
  if (millis() - lastTimeBotRan > BOT_MTBS) {
    handleTelegram();
    lastTimeBotRan = millis();
  }

  // RETRY notif recovery jika sebelumnya gagal kirim
  if (recoveryMenunggu && notifRecoveryPerlu) {
    int sisaWaktu = waktuTargetMenit - (int)elapsedRecovery;
    if (sisaWaktu < 0) sisaWaktu = 0;

    bool ok = bot.sendMessage(chatID,
      "⚠️ *Sesi Sebelumnya Ditemukan!*\n"
      "━━━━━━━━━━━━━━━━━━\n"
      "👟 Jenis   : " + jenisSepatu + "\n"
      "⏱️ Sudah   : " + String(elapsedRecovery) + " menit\n"
      "🎯 Target  : " + String(waktuTargetMenit) + " menit\n"
      "⏳ Sisa    : " + String(sisaWaktu) + " menit\n"
      "━━━━━━━━━━━━━━━━━━\n"
      "Ketik /lanjut untuk melanjutkan\n"
      "atau pilih jenis sepatu baru untuk memulai ulang.",
      "");

    if (ok) {
      Serial.println("✅ Notif recovery terkirim (retry)");
      preferences.begin("dryer", false);
      preferences.putBool("notifSent", true);
      preferences.end();
      notifRecoveryPerlu = false;
    } else {
      Serial.println("❌ Notif recovery retry gagal — coba lagi...");
    }
  }

  float suhu       = dht.readTemperature();
  float kelembaban = dht.readHumidity();

  if (isnan(suhu) || isnan(kelembaban)) {
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("Sensor Error!");
    lcd.setCursor(0, 1); lcd.print("Cek DHT11");
    // Sensor gagal baca -> jangan anggap sebagai kondisi kering,
    // reset status stabil supaya tidak keliru trigger stop dari noise.
    sedangStabilKering = false;
    delay(2000);
    return;
  }

  unsigned long waktuBerjalan = pengeringAktif
                                ? (millis() - waktuMulai) / 60000 : 0;

  // AUTO SAVE setiap 1 menit
  if (pengeringAktif && millis() - lastSave >= 60000) {
    simpanData();
    lastSave = millis();
  }

  // AUTO NOTIF setiap 30 menit
  if (pengeringAktif && millis() - lastNotif >= intervalNotif) {
    kirimStatusTelegram();
    lastNotif = millis();
  }

  // Perbarui puncak kelembaban yang pernah terlihat sesi ini —
  // dipakai heuristik deteksi chamber kosong di bawah.
  if (pengeringAktif && kelembaban > kelembabanMaxSesi) {
    kelembabanMaxSesi = kelembaban;
  }

  // Sudah pernah terlihat kenaikan kelembaban yang cukup besar sejak
  // sesi mulai? Kalau TIDAK PERNAH naik sama sekali, ini indikasi
  // (bukan kepastian) tidak ada objek basah di dalam chamber.
  bool kenaikanKelembabanTerdeteksi =
      (kelembabanMaxSesi - kelembabanAwalSesi) >= DELTA_KELEMBABAN_MIN;

  unsigned long waktuBerjalanMs = pengeringAktif ? (millis() - waktuMulai) : 0;

  // Peringatan (bukan auto-stop) kalau sampai akhir masa tunggu belum
  // ada kenaikan kelembaban sama sekali — kemungkinan chamber kosong.
  if (pengeringAktif && !kenaikanKelembabanTerdeteksi
      && !peringatanKosongTerkirim
      && waktuBerjalanMs >= GRACE_PERIOD_MS) {
    bot.sendMessage(chatID,
      "⚠️ *Peringatan!*\n"
      "━━━━━━━━━━━━━━━━━━\n"
      "Sudah " + String(GRACE_PERIOD_MS / 60000UL) + " menit berjalan, "
      "tapi kelembaban tidak pernah naik.\n"
      "Kemungkinan chamber KOSONG (sepatu belum/tidak dimasukkan).\n"
      "━━━━━━━━━━━━━━━━━━\n"
      "Ketik /stop kalau memang kosong, atau abaikan pesan ini "
      "kalau sepatu memang sudah hampir kering saat dimasukkan.",
      "");
    peringatanKosongTerkirim = true;
  }

  // ======================================================
  // DETEKSI KERING LEBIH AWAL (HYBRID: SENSOR + SAFETY TIMER)
  // ======================================================
  bool kondisiKeringSaatIni = pengeringAktif
                            && kenaikanKelembabanTerdeteksi
                            && (suhu >= (suhuTarget - TOLERANSI_SUHU_KERING))
                            && (kelembaban <= BATAS_KELEMBABAN_KERING);

  if (kondisiKeringSaatIni) {
    if (!sedangStabilKering) {
      sedangStabilKering = true;
      waktuMulaiStabil   = millis();
      Serial.println("🕒 Kondisi kering terdeteksi, mulai hitung stabilitas...");
    } else if (millis() - waktuMulaiStabil >= STABIL_MINIMAL_MS) {
      // Kondisi kering sudah bertahan cukup lama -> anggap valid, stop.
      int prediksiSebelumnya = waktuTargetMenit;

      pengeringAktif      = false;
      sedangStabilKering  = false;
      matikanRelay();

      preferences.begin("dryer", false);
      preferences.clear();
      preferences.end();

      bot.sendMessage(chatID,
        "🎉 *Pengeringan Selesai (deteksi sensor)!*\n"
        "━━━━━━━━━━━━━━━━━━\n"
        "👟 Jenis    : " + jenisSepatu + "\n"
        "⏱️ Aktual   : " + String(waktuBerjalan) + " menit\n"
        "🔮 Prediksi : " + String(prediksiSebelumnya) + " menit\n"
        "━━━━━━━━━━━━━━━━━━\n"
        "Sepatu kering lebih cepat dari prediksi ML! 👏",
        "");

      bot.sendMessageWithReplyKeyboard(
        chatID,
        "Pilih jenis sepatu untuk sesi berikutnya:",
        "", getKeyboard(), true);

      // Hindari langsung mengevaluasi blok "WAKTU HABIS" di bawah
      // pada iterasi yang sama, karena pengeringAktif sudah false.
      delay(2000);
      return;
    }
  } else {
    if (sedangStabilKering) {
      Serial.println("↩️ Kondisi kering tidak konsisten, reset penghitung stabilitas.");
    }
    sedangStabilKering = false;
  }

  // WAKTU HABIS (safety cutoff dari prediksi ML — tetap dipertahankan
  // sebagai jaminan alat berhenti walau sensor tidak pernah mendeteksi
  // kondisi kering-stabil, misalnya karena sensor rusak/terlepas)
  if (pengeringAktif && waktuBerjalan >= (unsigned long)waktuTargetMenit) {
    pengeringAktif = false;
    sedangStabilKering = false;
    matikanRelay();
    preferences.begin("dryer", false);
    preferences.clear();
    preferences.end();
    bot.sendMessage(chatID,
      "🎉 *Pengeringan Selesai!*\n"
      "━━━━━━━━━━━━━━━━━━\n"
      "👟 Jenis : " + jenisSepatu + "\n"
      "✅ Total : " + String(waktuTargetMenit) + " menit\n"
      "━━━━━━━━━━━━━━━━━━\n"
      "Sepatu siap dipakai! 👏",
      "");

    // Kirim ulang keyboard setelah selesai
    bot.sendMessageWithReplyKeyboard(
      chatID,
      "Pilih jenis sepatu untuk sesi berikutnya:",
      "", getKeyboard(), true);
  }

  // RELAY CONTROL
  if (pengeringAktif) {
    if (suhu <= (suhuTarget - 1.0) && !relayMenyala) {
      digitalWrite(RELAY1_PIN, LOW);
      digitalWrite(RELAY2_PIN, LOW);
      digitalWrite(DRYER_PIN,  LOW);
      digitalWrite(LAMP_PIN,   LOW);
      relayMenyala = true;
    } else if (suhu >= (suhuTarget + 0.5) && relayMenyala) {
      matikanRelay();
    }
  }

  // LCD 2 SLIDE
  if (millis() - lastLCD > 3000) {
    lcdSlide = !lcdSlide;
    lastLCD  = millis();
    lcd.clear();
  }

  if (!lcdSlide) {
    lcd.setCursor(0, 0);
    lcd.print("Suhu: ");
    lcd.print(suhu, 1);
    lcd.print((char)223);
    lcd.print("C   ");
    lcd.setCursor(0, 1);
    lcd.print("Lembap:");
    lcd.print(kelembaban, 1);
    lcd.print("%   ");
  } else {
    lcd.setCursor(0, 0);
    if (recoveryMenunggu) {
      lcd.print("Recovery?       ");
      lcd.setCursor(0, 1);
      lcd.print("Ketik /lanjut   ");
    } else {
      String ln1 = "Sepatu " + jenisSepatu;
      while (ln1.length() < 16) ln1 += " ";
      lcd.print(ln1.substring(0, 16));
      lcd.setCursor(0, 1);
      String ln2;
      if      (pengeringAktif && relayMenyala)  ln2 = "Mengeringkan    ";
      else if (pengeringAktif && !relayMenyala) ln2 = "Menunggu        ";
      else                                       ln2 = "Standby         ";
      lcd.print(ln2);
    }
  }

  Serial.println("===== SENSOR =====");
  Serial.print("🌡️ Suhu: ");        Serial.println(suhu);
  Serial.print("💧 Kelembaban: ");  Serial.println(kelembaban);
  Serial.print("👟 Jenis: ");       Serial.println(jenisSepatu);
  Serial.print("🎯 Target Suhu: "); Serial.println(suhuTarget);
  Serial.print("⏱️ Target Menit: ");Serial.println(waktuTargetMenit);
  Serial.print("⏱️ Berjalan: ");    Serial.println(waktuBerjalan);
  Serial.print("🔌 Relay: ");       Serial.println(relayMenyala ? "ON" : "OFF");
  Serial.print("🌤️ Stabil kering: "); Serial.println(sedangStabilKering ? "YA" : "TIDAK");

  delay(2000);
}
