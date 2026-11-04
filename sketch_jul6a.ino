#include <WiFi.h>
#include <ThingSpeak.h>
#include <DHT.h>
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRutils.h>

// ========== KONFIGURASI PIN ==========
#define DHTPIN 4
#define DHTTYPE DHT11
#define IR_RECV_PIN 15
#define LED_PIN 2

// ========== KONFIGURASI WiFi ==========
const char* ssid     = "(nama wifi)";
const char* password = "(password)";

// ========== KONFIGURASI THINGSPEAK ==========
unsigned long myChannelNumber = 3393202;
const char* myWriteAPIKey     = "6WBGF986N9DY28Z6";

// ========== INISIALISASI ==========
DHT dht(DHTPIN, DHTTYPE);
WiFiClient client;
IRrecv irrecv(IR_RECV_PIN);
decode_results results;

// ========== VARIABEL ==========
bool acState = false;       // Status AC (ON=true, OFF=false)
int usageCount = 0;         // Jumlah kali AC dinyalakan

// ========== KODE IR REMOTE AC ==========
#define IR_CODE_ON  0xB29F90
#define IR_CODE_OFF 0xB27BE0

void setup() {
  Serial.begin(115200);
  dht.begin();
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  irrecv.enableIRIn();
  Serial.println("IR Receiver siap");

  WiFi.begin(ssid, password);
  Serial.print("Menghubungkan WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi terhubung! IP: " + WiFi.localIP().toString());

  ThingSpeak.begin(client);
}

void loop() {
  // ----- BACA IR -----
  if (irrecv.decode(&results)) {
    unsigned long irCode = results.value;
    Serial.print("Kode IR: 0x");
    Serial.println(irCode, HEX);

    // LED menyala sebagai indikasi sinyal terbaca (300ms)
    digitalWrite(LED_PIN, HIGH);
    delay(300);
    digitalWrite(LED_PIN, LOW);

    // ----- DETEKSI TOMBOL AC -----
    if (irCode == IR_CODE_ON) {
      if (!acState) { // Hanya tambah counter jika sebelumnya OFF
        acState = true;
        usageCount++;
        Serial.println("✅ AC ON - Penggunaan ke-" + String(usageCount));
      } else {
        Serial.println("ℹ️ AC sudah ON, abaikan.");
      }
    } 
    else if (irCode == IR_CODE_OFF) {
      if (acState) {
        acState = false;
        Serial.println("❌ AC OFF");
      } else {
        Serial.println("ℹ️ AC sudah OFF, abaikan.");
      }
    }
    else {
      Serial.println("⚠️ Kode tidak dikenal (bukan AC)");
    }

    irrecv.resume();
    delay(100);
  }

  // ----- BACA DHT11 -----
  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature();

  if (isnan(humidity) || isnan(temperature)) {
    Serial.println("❌ Gagal membaca DHT11!");
    delay(5000);
    return;
  }

  // ----- KIRIM KE THINGSPEAK -----
  ThingSpeak.setField(1, temperature);
  ThingSpeak.setField(2, humidity);
  ThingSpeak.setField(3, acState ? 1 : 0);   // Status AC (1=ON, 0=OFF)
  ThingSpeak.setField(4, usageCount);        // Jumlah penggunaan

  int status = ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);

  // Tampilkan di Serial Monitor
  Serial.print("🌡️ Suhu: " + String(temperature) + "°C, ");
  Serial.print("💧 Kelembaban: " + String(humidity) + "%, ");
  Serial.print("📶 AC: " + String(acState ? "ON" : "OFF") + ", ");
  Serial.print("🔢 Penggunaan: " + String(usageCount) + "x");
  if (status == 200)
    Serial.println(" ✅ Data terkirim");
  else
    Serial.println(" ❌ Gagal kirim, error: " + String(status));

  delay(15000); // ThingSpeak minimum 15 detik
}
