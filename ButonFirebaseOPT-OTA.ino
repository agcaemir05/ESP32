#include <WiFi.h>
#include <PCF8574.h>
#include <Wire.h>
#include <FirebaseESP32.h>
#include <Update.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>  // HTTPClient kütüphanesi eklendi

// Wi-Fi bilgileri
const char* WIFI_SSID = "FiberHGW_TPBAFA_2.4GHz";
const char* WIFI_PASSWORD = "cVjVjaXC";

// Firebase bilgileri
#define FIREBASE_HOST "home-automation-b36b3-default-rtdb.firebaseio.com"
#define FIREBASE_AUTH "iCALp2UrPEjZjrS3GfN6VROQzGuqR41c4QB8eMG0"

// Firebase nesneleri
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// PCF8574 nesneleri (LED ve Buton için ayrı adresler)
PCF8574 pcf8574_led(0x38);
PCF8574 pcf8574_button(0x39);

// Buton debounce için değişkenler
bool lastButtonState[8] = {false};
unsigned long lastDebounceTime[8] = {0};
const int debounceDelay = 50;  // 50 ms debounce süresi

// OTA güncelleme için
String currentVersion = "1.0.0"; // Cihazdaki firmware sürümünüz (her OTA sonrası güncelleyin)
unsigned long lastUpdateCheck = 0;
const unsigned long updateInterval = 10000; // Test amaçlı 10 saniye; üretimde daha uzun bir süre önerilir

// Firebase üzerinden OTA güncelleme için JSON verisini kontrol eden fonksiyon
void checkFirmwareUpdate() {
  Serial.println("\n[OTA] Firmware güncellemesi kontrol ediliyor...");
  if (Firebase.getJSON(fbdo, "/firmware")) {
    FirebaseJson &json = fbdo.jsonObject();
    FirebaseJsonData jsonData;
    String newVersion = "";
    String firmwareURL = "";
    
    // "version" alanını oku
    if (json.get(jsonData, "version") && jsonData.type == "string") {
      newVersion = jsonData.stringValue;
    }
    // "url" alanını oku
    if (json.get(jsonData, "url") && jsonData.type == "string") {
      firmwareURL = jsonData.stringValue;
    }
    
    if (newVersion != "" && firmwareURL != "") {
      Serial.printf("[OTA] Firebase sürüm: %s, Mevcut sürüm: %s\n", newVersion.c_str(), currentVersion.c_str());
      // Sürüm numarası farklı ise güncelleme başlat
      if (newVersion != currentVersion) {
        Serial.println("[OTA] Yeni bir firmware bulundu. OTA güncellemesi başlatılıyor...");
        performOTA(firmwareURL);
      } else {
        Serial.println("[OTA] Cihaz zaten güncel.");
      }
    } else {
      Serial.println("[OTA] Gerekli OTA bilgileri eksik!");
    }
  } else {
    Serial.printf("[OTA] Firebase'den firmware verisi alınamadı! Hata: %s\n", fbdo.errorReason().c_str());
  }
}

// OTA güncellemesini gerçekleştiren fonksiyon
void performOTA(String firmwareURL) {
  WiFiClientSecure client;
  client.setInsecure(); // Test için; üretimde uygun sertifika doğrulaması yapın.
  
  HTTPClient http;  // HTTPClient sınıfı kullanılıyor
  Serial.printf("[OTA] Firmware indiriliyor: %s\n", firmwareURL.c_str());
  http.begin(client, firmwareURL);
  int httpCode = http.GET();
  
  if (httpCode == HTTP_CODE_OK) { // HTTP_CODE_OK sabiti HTTPClient kütüphanesi tarafından tanımlanır.
    int contentLength = http.getSize();
    if (contentLength <= 0) {
      Serial.println("[OTA] Hatalı içerik boyutu.");
      http.end();
      return;
    }
    
    if (!Update.begin(contentLength)) {
      Serial.println("[OTA] OTA güncellemesi için yeterli alan yok!");
      http.end();
      return;
    }
    
    Serial.println("[OTA] OTA güncellemesi başlatılıyor...");
    size_t written = Update.writeStream(http.getStream());
    if (written == contentLength) {
      Serial.println("[OTA] OTA güncellemesi başarılı!");
    } else {
      Serial.printf("[OTA] Yazılan byte sayısı hatalı: %d/%d\n", written, contentLength);
    }
    
    if (Update.end()) {
      if (Update.isFinished()) {
        Serial.println("[OTA] OTA güncellemesi tamamlandı. Cihaz yeniden başlatılıyor...");
        ESP.restart();
      } else {
        Serial.println("[OTA] OTA güncellemesi tamamlanamadı!");
      }
    } else {
      Serial.printf("[OTA] OTA Hatası, hata kodu: %d\n", Update.getError());
    }
  } else {
    Serial.printf("[OTA] HTTP GET isteği başarısız! Hata kodu: %d\n", httpCode);
  }
  http.end();
}

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);

  // Wi-Fi bağlantısı
  WiFi.setTxPower(WIFI_POWER_19_5dBm); // İsteğe bağlı: Wi-Fi sinyal gücünü artırır.
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("WiFi'ye bağlanılıyor");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi bağlandı!");

  // Firebase yapılandırması
  config.host = FIREBASE_HOST;
  config.signer.tokens.legacy_token = FIREBASE_AUTH;
  // SSL sertifikasını devre dışı bırak (Geçici çözüm)
  config.cert.data = nullptr;
  config.timeout.serverResponse = 10000;  // 10 saniye
  config.timeout.socketConnection = 30000; // 30 saniye

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  Serial.println("Firebase bağlantısı başarılı!");

  // PCF8574 başlatma
  if (!pcf8574_led.begin() || !pcf8574_button.begin()) {
    Serial.println("PCF8574 hatası!");
    while (1);
  }
  
  // LED'leri başlangıçta sıfırla
  for (int i = 0; i < 8; i++) {
    pcf8574_led.write(i, LOW);
  }

  // İlk OTA kontrolü (isteğe bağlı, başlangıçta kontrol edebilirsiniz)
  lastUpdateCheck = millis();
  checkFirmwareUpdate();
}

void loop() {
  // PCF8574 ile buton/LED işlemleri
  if (Firebase.ready()) {
    for (int i = 0; i < 8; i++) {
      bool currentState = pcf8574_button.read(i); // Buton durumunu oku

      // Debounce kontrolü
      if (currentState != lastButtonState[i]) {
        lastDebounceTime[i] = millis();
      }
      if ((millis() - lastDebounceTime[i]) > debounceDelay) {
        bool buttonState = pcf8574_button.read(i); // Tekrar oku
        // LED durumu ile buton durumu farklı ise güncelle
        if (buttonState != pcf8574_led.read(i)) {
          pcf8574_led.write(i, buttonState ? HIGH : LOW);
          String path = "/buttons/button" + String(i);
          Firebase.setBoolAsync(fbdo, path.c_str(), buttonState);
          Serial.println("Güncellendi: " + path + " = " + String(buttonState));
        }
      }
      lastButtonState[i] = currentState;
    }
  }

  // Periyodik olarak OTA güncelleme kontrolü (örneğin her 10 saniyede bir)
  if (millis() - lastUpdateCheck > updateInterval) {
    lastUpdateCheck = millis();
    checkFirmwareUpdate();
  }
  
  delay(10); // Döngü hızını çok fazla arttırmamak için küçük gecikme
}
