#include <WiFi.h>
#include <ThingSpeak.h>
#include <DHT.h>

//================ DHT11 =================
#define DHTPIN 4
#define DHTTYPE DHT11

//================ RCWL-1601 (Ultrasonic) =============
#define TRIG_PIN 27
#define ECHO_PIN 26

//================ LDR ===================
#define LDR_PIN 34

//================ WIFI ==================
const char* ssid = "(nama wifi)";
const char* password = "(password)";

//============= THINGSPEAK ===============
unsigned long channelNumber = 3416013;
const char* writeAPIKey = "HEYB9KLNEWX40F2F";

WiFiClient client;
DHT dht(DHTPIN, DHTTYPE);

// ============= VARIABEL GLOBAL =============
float suhu = -999.0;
float hum = -999.0;
float jarakSaatIni = 0.0;      // Jarak hasil pembacaan sensor saat ini (selalu nilai aktual)
int deteksiSekarang = 0;        // Status deteksi saat ini (1 = ada orang, 0 = tidak ada)
int ldrSkala = 0;

// ============= VARIABEL LATCH (TERKUNCI) UNTUK DI-UPLOAD =============
int statusLatch = 0;            // Status yang akan dikirim (1 = pernah ada orang)
float jarakLatch = 0.0;        // Jarak saat orang terdeteksi (akan dikirim jika statusLatch=1)

// ============= TIMER =============
unsigned long lastReadTime = 0;
const long readInterval = 1000;      // Baca sensor tiap 1 detik

unsigned long lastUploadTime = 0;
const long uploadInterval = 15000;   // Upload tiap 15 detik

void setup() {
  Serial.begin(115200);
  dht.begin();
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  WiFi.begin(ssid, password);
  Serial.print("Menghubungkan WiFi");
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi Connected");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi Gagal Konek");
  }
  ThingSpeak.begin(client);
}

void loop() {

  // ================== 1. CEK KONEKSI WIFI ==================
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi putus, mencoba reconnect...");
    WiFi.begin(ssid, password);
    delay(5000);
  }

  // ================== 2. BACA SENSOR SETIAP 1 DETIK ==================
  if (millis() - lastReadTime >= readInterval) {
    lastReadTime = millis();

    // ----- BACA DHT11 -----
    float tempSuhu = dht.readTemperature();
    float tempHum = dht.readHumidity();
    if (isnan(tempSuhu) || isnan(tempHum)) {
      suhu = -999.0;
      hum = -999.0;
    } else {
      suhu = tempSuhu;
      hum = tempHum;
    }

    // ----- BACA ULTRASONIC (RCWL-1601) -----
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);

    long duration = pulseIn(ECHO_PIN, HIGH, 30000); // timeout 30ms
    
    if (duration == 0) {
      // Tidak ada pantulan dalam 30ms -> jarak sangat jauh (misal > 5 meter)
      // Kita set ke nilai maksimum yang masuk akal, misal 500 cm
      jarakSaatIni = 500.0;
      deteksiSekarang = 0; // Tidak ada objek dalam jangkauan dekat
    } else {
      jarakSaatIni = duration * 0.0343 / 2.0;
      // Deteksi objek jika jarak antara 0-20 cm
      if (jarakSaatIni > 0 && jarakSaatIni <= 20) {
        deteksiSekarang = 1; // ADA ORANG/OBJEK!
      } else {
        deteksiSekarang = 0; // Tidak ada objek dekat
      }
    }

    // ========== LOGIKA PENGUNCI (LATCH) UNTUK STATUS & JARAK ==========
    // Jika sensor mendeteksi objek (deteksiSekarang = 1), 
    // maka kita "kunci" statusLatch = 1 dan jarakLatch = jarakSaatIni
    if (deteksiSekarang == 1) {
      statusLatch = 1;
      jarakLatch = jarakSaatIni;  // Simpan jarak saat orang terdeteksi
      Serial.print(">>> [LATCH] Ada orang! Jarak terkunci: ");
      Serial.print(jarakLatch);
      Serial.println(" cm");
    }
    // Jika deteksiSekarang == 0, statusLatch dan jarakLatch TIDAK berubah.
    // Mereka tetap menyimpan nilai terakhir saat ada orang.

    // ----- BACA LDR -----
    int nilaiLDR = analogRead(LDR_PIN);
    ldrSkala = constrain(map(nilaiLDR, 0, 4095, 0, 10), 0, 10);

    // ----- SERIAL MONITOR (untuk debugging) -----
    Serial.println("==============================");
    Serial.print("Jarak Saat Ini (aktual) : ");
    Serial.print(jarakSaatIni);
    Serial.println(" cm");
    Serial.print("Status Saat Ini         : ");
    Serial.println(deteksiSekarang);
    Serial.print(">>> [LATCH] Status yang akan di-upload: ");
    Serial.println(statusLatch);
    if (statusLatch == 1) {
      Serial.print(">>> [LATCH] Jarak yang akan di-upload: ");
      Serial.print(jarakLatch);
      Serial.println(" cm (nilai saat orang terdeteksi)");
    } else {
      Serial.print(">>> [LATCH] Jarak yang akan di-upload: ");
      Serial.print(jarakSaatIni);
      Serial.println(" cm (jarak ruangan kosong)");
    }
    Serial.print("Suhu: ");
    Serial.print(suhu);
    Serial.println(" C");
  }

  // ================== 3. UPLOAD KE THINGSPEAK SETIAP 15 DETIK ==================
  if (millis() - lastUploadTime >= uploadInterval) {
    lastUploadTime = millis();

    if (WiFi.status() == WL_CONNECTED) {
      
      // Tentukan nilai jarak yang akan dikirim
      float jarakKirim;
      if (statusLatch == 1) {
        // Jika pernah ada orang, kirim jarak saat orang terdeteksi
        jarakKirim = jarakLatch;
      } else {
        // Jika tidak pernah ada orang, kirim jarak ruangan kosong (aktual)
        jarakKirim = jarakSaatIni;
      }

      // Kirim data ke ThingSpeak
      ThingSpeak.setField(1, suhu);
      ThingSpeak.setField(2, hum);
      ThingSpeak.setField(3, jarakKirim);      // <-- Kirim jarak sesuai logika di atas
      ThingSpeak.setField(4, statusLatch);     // <-- Kirim status (1 jika pernah ada orang)
      ThingSpeak.setField(5, ldrSkala);

      int status = ThingSpeak.writeFields(channelNumber, writeAPIKey);

      if (status == 200) {
        Serial.println(">>> [SUKSES] Upload ke ThingSpeak Berhasil!");

        // ========== RESET LATCH SETELAH UPLOAD BERHASIL ==========
        // Reset hanya jika saat ini TIDAK ada orang di depan sensor
        if (deteksiSekarang == 0) {
          statusLatch = 0;
          jarakLatch = 0.0;  // Reset ke 0 (tidak digunakan)
          Serial.println(">>> [RESET] Latch di-reset (status=0) karena ruangan kosong.");
        } else {
          // Jika masih ada orang, biarkan latch tetap 1 dan perbarui jarakLatch dengan jarak terbaru
          jarakLatch = jarakSaatIni;
          Serial.println(">>> [RESET] Latch TIDAK di-reset karena masih ada orang. Jarak diperbarui.");
        }
      } else {
        Serial.print(">>> [GAGAL] Upload error code: ");
        Serial.println(status);
        // Jika gagal, JANGAN reset latch! Biarkan tetap 1 agar dicoba lagi nanti.
      }
    } else {
      Serial.println(">>> [LEWAT] WiFi tidak konek, upload dibatalkan.");
    }
  }
}