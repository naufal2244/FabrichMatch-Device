#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <ESP8266WebServer.h>
#include <PubSubClient.h>
#include <ArduinoJson.h> 
#include <EEPROM.h> // Pastikan ini ada untuk menggunakan EEPROM

// --- Pin Sensor TCS3200 ---
#define S0 5
#define S1 4
#define S2 12
#define S3 13
#define sensorOut 14 

// --- EEPROM ---
#define EEPROM_SIZE sizeof(WhiteReference) 
#define EEPROM_ADDR 0

struct WhiteReference {
  int r;
  int g;
  int b;
};

// --- Variabel Sensor (GLOBAL DEKLARASI) ---
int redMin = 0, redMax = 0;
int greenMin = 0, greenMax = 0;
int blueMin = 0, blueMax = 0;

int redValues[6] = { 0 };
int greenValues[6] = { 0 };
int blueValues[6] = { 0 };
int dataIndex = 0; // Deklarasi global

int redPW = 0, greenPW = 0, bluePW = 0;
int redValue = 0, greenValue = 0, blueValue = 0;

// --- Jaringan (GLOBAL DEKLARASI) ---
const char deviceName[] = "Sensor-Benang-Kain";
const char* mqtt_server_ip = "192.168.43.155"; 
const int mqtt_port = 1234; 

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
ESP8266WebServer server(80);

// --- Mode & Perintah (GLOBAL DEKLARASI) ---
char kode_benang[50] = ""; 
int id_merek = 0;
bool modeScanBenang = true; 
bool scanning = false; // Deklarasi global

bool sedangKalibrasiPutih = false;
bool sedangKalibrasiHitam = false;
int kalibrasiCounter = 0; 

long rKalibrasiTotalPW = 0;
long gKalibrasiTotalPW = 0;
long bKalibrasiTotalPW = 0;

// --- Timer (GLOBAL DEKLARASI) ---
unsigned long lastUpdate = 0;
const int updateInterval = 500; 
const int totalScan = 6;        
const int ignoreFirst = 1;      
int scanCount = 0;              // PERBAIKAN PENTING: Deklarasi global untuk scanCount

// --- Prototipe Fungsi ---
// Deklarasi fungsi agar compiler tahu fungsi-fungsi ini ada sebelum digunakan
void mqttCallback(char* topic, byte* payload, unsigned int length);
void connectMQTT();
int getRedPW();
int getGreenPW();
int getBluePW();
void bacaSensorWarna();
int hitungRataRata(int arr[]);
void kirimData();
void handleScanBenang();
void handleScanKain();
void simpanReferensiPutih();
WhiteReference bacaReferensiPutih();
void selfCheck();
void kirimStatusMQTT(const char* message); 
void kirimDataKalibrasi(const char* type, int r, int g, int b); 

// --- Setup ---
// Fungsi setup akan dijalankan sekali saat ESP8266 pertama kali dihidupkan
void setup() {
  Serial.begin(115200); // Kecepatan baud untuk komunikasi serial (debugging)
  EEPROM.begin(EEPROM_SIZE); // Inisialisasi EEPROM

  // Konfigurasi WiFi menggunakan WiFiManager. Ini akan mencoba konek ke WiFi yang
  // tersimpan, atau jika tidak ada/gagal, akan membuat Access Point sendiri.
  WiFiManager wifiManager;
  if (!wifiManager.autoConnect(deviceName, "123!Pwd$")) { // Nama AP dan password default jika gagal konek
    Serial.println("Gagal konek ke WiFi, rebooting...");
    delay(3000);
    ESP.restart(); // Restart ESP jika gagal konek WiFi
  }
  Serial.println("Connected: " + WiFi.localIP().toString());

  // Konfigurasi pin sensor TCS3200 sebagai INPUT/OUTPUT
  pinMode(S0, OUTPUT);
  pinMode(S1, OUTPUT);
  pinMode(S2, OUTPUT);
  pinMode(S3, OUTPUT);
  pinMode(sensorOut, INPUT);
  
  // Mengatur skala frekuensi sensor ke 20% (S0=HIGH, S1=LOW)
  // Ini adalah pengaturan yang umum dan sering optimal untuk ESP8266
  digitalWrite(S0, HIGH);
  digitalWrite(S1, LOW); 

  // Konfigurasi klien MQTT
  mqttClient.setServer(mqtt_server_ip, mqtt_port); // Set alamat broker MQTT
  mqttClient.setCallback(mqttCallback); // Set fungsi yang akan dipanggil saat ada pesan MQTT masuk

  // Konfigurasi server web ESP8266 untuk menerima HTTP request
  server.on("/scan-benang", HTTP_POST, handleScanBenang);
  server.on("/scan-kain", HTTP_POST, handleScanKain);
  server.begin(); // Mulai server web

  lastUpdate = millis(); // Inisialisasi timer untuk kontrol interval

  // Memuat referensi putih yang disimpan di EEPROM saat startup
  WhiteReference storedRef = bacaReferensiPutih();
  if (storedRef.r != 0 || storedRef.g != 0 || storedRef.b != 0) { 
    Serial.printf("Referensi putih dimuat dari EEPROM (nilai 0-255): R=%d G=%d B=%d\n", storedRef.r, storedRef.g, storedRef.b);
  }
}

// --- Loop Utama Program ---
// Fungsi loop akan dijalankan berulang kali setelah setup selesai
void loop() {
  // Pastikan koneksi MQTT tetap terjaga
  if (!mqttClient.connected()) {
    connectMQTT();
  }
  mqttClient.loop(); // Proses pesan MQTT yang tertunda dan jaga koneksi tetap hidup
  server.handleClient(); // Proses permintaan HTTP yang masuk

  // --- Logic Pemindaian RGB Normal (Scan Kain/Benang) ---
  // Akan berjalan jika 'scanning' true dan sudah melewati 'updateInterval'
  if (scanning && millis() - lastUpdate > updateInterval) {
    lastUpdate = millis();
    
    // Abaikan sejumlah scan awal untuk stabilitas sensor.
    // scanCount adalah variabel global yang diatur di sini dan di mqttCallback/handleScan...
    if (scanCount < ignoreFirst) { 
      bacaSensorWarna(); // Tetap baca untuk "mengalirkan" data sensor
      Serial.printf("Mengabaikan scan awal (%d/%d): R=%d G=%d B=%d\n", scanCount + 1, ignoreFirst, redValue, greenValue, blueValue);
    } else {
      // Baca sensor dan simpan nilai ke buffer untuk dihitung rata-ratanya
      bacaSensorWarna();
      if (dataIndex < totalScan) { // dataIndex adalah global
        redValues[dataIndex] = redValue;
        greenValues[dataIndex] = greenValue;
        blueValues[dataIndex] = blueValue;
        dataIndex++;
        Serial.printf("Scan data (%d/%d): R=%d G=%d B=%d\n", dataIndex, totalScan, redValue, greenValue, blueValue);
      }
    }

    scanCount++; // scanCount adalah global
    // Jika semua sampel sudah diambil (termasuk yang diabaikan)
    if (scanCount >= totalScan + ignoreFirst) { 
      kirimData(); // Kirim data rata-rata ke MQTT
      scanCount = 0; // Reset counter untuk scan berikutnya
      dataIndex = 0; // Reset index buffer
      scanning = false; // Hentikan proses scanning
      kirimStatusMQTT("✅ Pemindaian selesai."); // Beri tahu status ke aplikasi
    }
  }

  // --- Logic Kalibrasi Putih ---
  // Akan berjalan jika 'sedangKalibrasiPutih' true dan belum mencapai 'totalScan' sampel
  if (sedangKalibrasiPutih && kalibrasiCounter < totalScan) { 
    bacaSensorWarna(); // Mendapatkan redPW, greenPW, bluePW (nilai mentah dari pulseIn)
    
    // Akumulasi nilai PW untuk dihitung rata-ratanya
    rKalibrasiTotalPW += redPW;
    gKalibrasiTotalPW += greenPW;
    bKalibrasiTotalPW += bluePW;

    Serial.printf("[KALIBRASI PUTIH] Scan %d/%d: R_PW=%d G_PW=%d B_PW=%d\n", kalibrasiCounter + 1, totalScan, redPW, greenPW, bluePW);
    kalibrasiCounter++;
    delay(300); // Jeda sebentar antar setiap scan kalibrasi

    if (kalibrasiCounter >= totalScan) { // Jika semua sampel kalibrasi sudah diambil
      sedangKalibrasiPutih = false; // Hentikan mode kalibrasi
      
      // Hitung rata-rata PW untuk kalibrasi putih, ini akan menjadi batas 'min' untuk fungsi map()
      redMin = rKalibrasiTotalPW / totalScan;
      greenMin = gKalibrasiTotalPW / totalScan;
      blueMin = bKalibrasiTotalPW / totalScan;

      // Reset total akumulator untuk persiapan kalibrasi berikutnya
      rKalibrasiTotalPW = 0;
      gKalibrasiTotalPW = 0;
      bKalibrasiTotalPW = 0;
      
      // Kirim hasil kalibrasi putih ke aplikasi Android dalam format JSON
      kirimDataKalibrasi("white", redMin, greenMin, blueMin);
      Serial.printf("✅ Kalibrasi PUTIH selesai. PW Putih (Rata-rata Min): R=%d G=%d B=%d\n", redMin, greenMin, blueMin);
    }
  }

  // --- Logic Kalibrasi Hitam ---
  // Akan berjalan jika 'sedangKalibrasiHitam' true dan belum mencapai 'totalScan' sampel
  if (sedangKalibrasiHitam && kalibrasiCounter < totalScan) { 
    bacaSensorWarna(); // Mendapatkan redPW, greenPW, bluePW (nilai mentah dari pulseIn)

    // Akumulasi nilai PW untuk dihitung rata-ratanya
    rKalibrasiTotalPW += redPW;
    gKalibrasiTotalPW += greenPW;
    bKalibrasiTotalPW += bluePW;

    Serial.printf("[KALIBRASI HITAM] Scan %d/%d: R_PW=%d G_PW=%d B_PW=%d\n", kalibrasiCounter + 1, totalScan, redPW, greenPW, bluePW);
    kalibrasiCounter++;
    delay(300); // Jeda sebentar antar setiap scan kalibrasi

    if (kalibrasiCounter >= totalScan) { // Jika semua sampel kalibrasi sudah diambil
      sedangKalibrasiHitam = false; // Hentikan mode kalibrasi

      // Hitung rata-rata PW untuk kalibrasi hitam, ini akan menjadi batas 'max' untuk fungsi map()
      redMax = rKalibrasiTotalPW / totalScan;
      greenMax = gKalibrasiTotalPW / totalScan;
      blueMax = bKalibrasiTotalPW / totalScan;

      // Reset total akumulator untuk persiapan kalibrasi berikutnya
      rKalibrasiTotalPW = 0;
      gKalibrasiTotalPW = 0;
      bKalibrasiTotalPW = 0;
      
      // Kirim hasil kalibrasi hitam ke aplikasi Android dalam format JSON
      kirimDataKalibrasi("black", redMax, greenMax, blueMax);
      Serial.printf("✅ Kalibrasi HITAM selesai. PW Hitam (Rata-rata Max): R=%d G=%d B=%d\n", redMax, greenMax, blueMax);
      Serial.printf("Rentang Map Final: R[%d-%d] G[%d-%d] B[%d-%d]\n", redMin, redMax, greenMin, greenMax, blueMin, blueMax);

      // --- PENTING: Perlu Simpan PW min/max ke EEPROM jika ingin persisten ---
      // Jika Anda ingin nilai redMin, redMax, greenMin, greenMax, blueMin, blueMax
      // ini persisten setelah reboot (tidak kembali ke 0),
      // Anda harus membuat struct baru (misalnya CalibrationData)
      // dan menyimpannya ke EEPROM di sini setelah kalibrasi hitam selesai.
      // Jangan lupa untuk memuatnya kembali saat setup().
    }
  }
}

// --- Fungsi MQTT Callback ---
// Dipanggil saat ada pesan MQTT masuk ke topik yang disubscribe
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // Konversi payload byte ke char array (string)
  char msgBuffer[length + 1];
  memcpy(msgBuffer, payload, length);
  msgBuffer[length] = '\0'; // Pastikan string diakhiri null

  Serial.printf("MQTT Pesan Diterima: Topic=%s, Pesan=%s\n", topic, msgBuffer);

  // Periksa topik dan konten pesan untuk menentukan aksi
  if (strcmp(topic, "fabricmatch/perintah") == 0) { 
    if (strcmp(msgBuffer, "scan-kain") == 0) {
      modeScanBenang = false;
      scanCount = -ignoreFirst; // scanCount adalah global
      scanning = true;
      kirimStatusMQTT("🟢 Memulai pemindaian KAIN...");
    } else if (strcmp(msgBuffer, "scan-benang") == 0) {
      modeScanBenang = true;
      scanCount = -ignoreFirst; // scanCount adalah global
      scanning = true;
      kirimStatusMQTT("🟢 Memulai pemindaian BENANG...");
    } else if (strcmp(msgBuffer, "kalibrasi-putih") == 0) {
      sedangKalibrasiPutih = true;
      sedangKalibrasiHitam = false; 
      kalibrasiCounter = 0;
      rKalibrasiTotalPW = 0; 
      gKalibrasiTotalPW = 0;
      bKalibrasiTotalPW = 0;
      kirimStatusMQTT("🟡 Mulai kalibrasi PUTIH...");
      Serial.println("🟡 Mulai kalibrasi PUTIH...");
    } else if (strcmp(msgBuffer, "kalibrasi-hitam") == 0) {
      sedangKalibrasiHitam = true;
      sedangKalibrasiPutih = false; 
      kalibrasiCounter = 0;
      rKalibrasiTotalPW = 0; 
      gKalibrasiTotalPW = 0;
      bKalibrasiTotalPW = 0;
      kirimStatusMQTT("⚫ Mulai kalibrasi HITAM...");
      Serial.println("⚫ Mulai kalibrasi HITAM...");
    } else if (strcmp(msgBuffer, "simpan") == 0) {
      simpanReferensiPutih(); 
    } else if (strcmp(msgBuffer, "cek") == 0) {
      selfCheck(); 
    } else if (strcmp(msgBuffer, "stop") == 0) {
      scanning = false;
      sedangKalibrasiPutih = false;
      sedangKalibrasiHitam = false;
      kalibrasiCounter = 0;
      kirimStatusMQTT("⛔ Pemindaian/Kalibrasi dihentikan oleh pengguna.");
    }
  }
}

// --- Fungsi Kirim Status Umum ke MQTT ---
void kirimStatusMQTT(const char* message) {
  // Peringatan JSON_OBJECT_SIZE adalah normal untuk versi lama ArduinoJson
  // Untuk versi baru, Anda bisa gunakan DynamicJsonDocument(size_t capacity)
  // dengan capacity yang cukup (misal 200-250 byte untuk payload yang lumayan panjang)
  const size_t CAPACITY = JSON_OBJECT_SIZE(2); 
  DynamicJsonDocument doc(CAPACITY);
  doc["type"] = "status";    
  doc["message"] = message;  

  char payload[200]; 
  serializeJson(doc, payload, sizeof(payload)); 
  mqttClient.publish("fabricmatch/status", payload); 
}

// --- Fungsi Kirim Data Kalibrasi Numerik ke MQTT ---
void kirimDataKalibrasi(const char* type, int r, int g, int b) {
  // Peringatan JSON_OBJECT_SIZE adalah normal untuk versi lama ArduinoJson
  const size_t CAPACITY = JSON_OBJECT_SIZE(4); 
  DynamicJsonDocument doc(CAPACITY);

  doc["type"] = type; 
  doc["r"] = r;       
  doc["g"] = g;       
  doc["b"] = b;       
  
  char payload[100]; 
  serializeJson(doc, payload, sizeof(payload)); 

  mqttClient.publish("fabricmatch/status", payload); 
  Serial.print("MQTT Kalibrasi Dikirim: ");
  Serial.println(payload);
}


// --- Koneksi MQTT ---
void connectMQTT() {
  while (!mqttClient.connected()) {
    Serial.print("Menghubungkan MQTT...");
    if (mqttClient.connect("sensor-warna", "try", "try")) {
      Serial.println("Berhasil!");
      mqttClient.subscribe("fabricmatch/perintah");
      kirimStatusMQTT("✅ Terhubung ke MQTT."); 
    } else {
      Serial.print("Gagal. Code: ");
      Serial.println(mqttClient.state());
      delay(2000); 
    }
  }
}

// --- Fungsi Baca Sensor Warna ---
// Mengembalikan durasi pulsa (Pulse Width / PW) dalam mikrosekon.
// Semakin terang cahaya, semakin tinggi frekuensi, semakin PENDEK durasi pulsa (nilai PW kecil).
// Sebaliknya, semakin gelap, semakin rendah frekuensi, semakin PANJANG durasi pulsa (nilai PW besar).
int getRedPW() {
  digitalWrite(S2, LOW);  // Pilih filter Merah
  digitalWrite(S3, LOW);
  return pulseIn(sensorOut, LOW, 100000); // Ukur lebar pulsa LOW, timeout 100ms
}
int getGreenPW() {
  digitalWrite(S2, HIGH); // Pilih filter Hijau
  digitalWrite(S3, HIGH);
  return pulseIn(sensorOut, LOW, 100000); // Ukur lebar pulsa LOW, timeout 100ms
}
int getBluePW() {
  digitalWrite(S2, LOW);  // Pilih filter Biru
  digitalWrite(S3, HIGH);
  return pulseIn(sensorOut, LOW, 100000); // Ukur lebar pulsa LOW, timeout 100ms
}

// Fungsi untuk membaca semua warna (R, G, B) dari PW mentah dan memetakan ke 0-255
void bacaSensorWarna() {
  redPW = getRedPW();
  greenPW = getGreenPW();
  bluePW = getBluePW();

  // Memetakan nilai Pulse Width (PW) mentah ke rentang 0-255 RGB.
  // Note: Fungsi map() secara otomatis akan membalikkan nilai karena
  // range output (255, 0) terbalik dari range input (min, max PW).
  // Ini berarti PW kecil (terang) akan menjadi nilai RGB tinggi (255).
  //
  // Penanganan jika redMax <= redMin (misalnya, belum kalibrasi hitam dengan benar
  // atau PW hitam < PW putih), map() bisa menghasilkan nilai aneh.
  // Diberikan rentang default 0-1000 jika kalibrasi belum valid atau pulseIn timeout.
  if (redPW == 0 || redMax <= redMin) { // Jika pulseIn timeout atau rentang kalibrasi tidak valid
    redValue = map(redPW, 0, 1000, 255, 0); // Gunakan rentang default sementara
  } else {
    redValue = map(redPW, redMin, redMax, 255, 0); // Map berdasarkan nilai kalibrasi
  }
  
  if (greenPW == 0 || greenMax <= greenMin) {
    greenValue = map(greenPW, 0, 1000, 255, 0);
  } else {
    greenValue = map(greenPW, greenMin, greenMax, 255, 0);
  }

  if (bluePW == 0 || blueMax <= blueMin) {
    blueValue = map(bluePW, 0, 1000, 255, 0);
  } else {
    blueValue = map(bluePW, blueMin, blueMax, 255, 0);
  }

  // Memastikan nilai RGB berada dalam rentang 0-255
  redValue = constrain(redValue, 0, 255);
  greenValue = constrain(greenValue, 0, 255);
  blueValue = constrain(blueValue, 0, 255);
}

// Fungsi untuk menghitung rata-rata dari array nilai
int hitungRataRata(int arr[]) {
  long total = 0; // Gunakan long untuk mencegah overflow saat menjumlahkan nilai PW
  for (int i = 0; i < totalScan; i++) {
    total += arr[i];
  }
  return total / totalScan;
}
// Fungsi untuk mengirim data RGB hasil scan ke MQTT
void kirimData() {
  int r = hitungRataRata(redValues);
  int g = hitungRataRata(greenValues);
  int b = hitungRataRata(blueValues);

  // Jika sedang scan BENANG, tambahkan offset
  if (modeScanBenang) {
    r = constrain(r + 39, 0, 255); // Tambah 39, batasi maksimal 255
    g = constrain(g + 9, 0, 255);  // Tambah 9
    b = constrain(b + 15, 0, 255); // Tambah 15
  }

  const size_t CAPACITY = JSON_OBJECT_SIZE(4); 
  DynamicJsonDocument doc(CAPACITY);

  doc["jenis"] = modeScanBenang ? "benang" : "kain"; // Jenis objek yang di-scan
  doc["r"] = r;
  doc["g"] = g;
  doc["b"] = b;

  char payload[100]; // Buffer untuk serialized JSON
  serializeJson(doc, payload, sizeof(payload)); // Konversi JSON Document ke string

  // Publikasikan ke topik yang sesuai (benang atau kain)
  mqttClient.publish(
    modeScanBenang ? "fabricmatch/benang/hasil" : "fabricmatch/kain/hasil",
    payload);

  Serial.print("MQTT dikirim: ");
  Serial.println(payload);
}

// --- HTTP Endpoint ---
// Handler untuk HTTP POST request ke /scan-benang
void handleScanBenang() {
  // Salin kode_benang dari argumen request, pastikan tidak overflow buffer
  strncpy(kode_benang, server.arg("kode_benang").c_str(), sizeof(kode_benang) - 1);
  kode_benang[sizeof(kode_benang) - 1] = '\0'; // Pastikan null terminated

  id_merek = server.arg("id_merek").toInt(); // Ambil id_merek dari argumen
  modeScanBenang = true; // Set mode ke scan benang
  scanCount = -ignoreFirst; // Mulai hitungan dengan mengabaikan
  scanning = true; // Aktifkan scanning
  server.sendHeader("Access-Control-Allow-Origin", "*"); // Izinkan CORS
  server.send(200, "text/plain", "OK"); // Kirim respons OK
  kirimStatusMQTT("🟢 Perintah scan benang diterima via HTTP.");
}

// Handler untuk HTTP POST request ke /scan-kain
void handleScanKain() {
  modeScanBenang = false; // Set mode ke scan kain
  scanCount = -ignoreFirst; // Mulai hitungan dengan mengabaikan
  scanning = true; // Aktifkan scanning
  server.sendHeader("Access-Control-Allow-Origin", "*"); // Izinkan CORS
  server.send(200, "text/plain", "OK"); // Kirim respons OK
  kirimStatusMQTT("🟢 Perintah scan kain diterima via HTTP.");
}

// --- EEPROM Simpan & Cek ---
// Fungsi untuk menyimpan rata-rata RGB (0-255) dari permukaan putih ke EEPROM
void simpanReferensiPutih() {
  int r_avg = 0, g_avg = 0, b_avg = 0;
  // Ambil beberapa sampel untuk rata-rata
  for (int i = 0; i < 6; i++) {
    bacaSensorWarna(); // Ini akan memperbarui redValue, greenValue, blueValue (0-255)
    r_avg += redValue;
    g_avg += greenValue;
    b_avg += blueValue;
    delay(200); // Sedikit jeda antar pembacaan
  }

  WhiteReference ref = { r_avg / 6, g_avg / 6, b_avg / 6 }; // Hitung rata-rata
  EEPROM.put(EEPROM_ADDR, ref); // Simpan struct ke EEPROM
  EEPROM.commit(); // Tulis perubahan ke flash
  
  char pesan[100];
  snprintf(pesan, sizeof(pesan), "✅ Ref. putih disimpan: R=%d G=%d B=%d", ref.r, ref.g, ref.b);
  kirimStatusMQTT(pesan);
  Serial.printf("✅ Referensi putih disimpan ke EEPROM: R=%d G=%d B=%d\n", ref.r, ref.g, ref.b);
}

// Fungsi untuk membaca referensi putih dari EEPROM
WhiteReference bacaReferensiPutih() {
  WhiteReference ref;
  EEPROM.get(EEPROM_ADDR, ref); // Baca struct dari EEPROM
  return ref;
}

// Fungsi untuk melakukan self-check akurasi sensor terhadap referensi putih yang tersimpan
void selfCheck() {
  Serial.println("🧪 Mengecek keakuratan sensor terhadap referensi putih...");
  WhiteReference ref = bacaReferensiPutih(); // Muat referensi tersimpan
  
  // Cek apakah referensi sudah pernah disimpan (asumsi 0,0,0 jika kosong)
  if (ref.r == 0 && ref.g == 0 && ref.b == 0) {
    Serial.println("⚠ Referensi putih belum disimpan.");
    kirimStatusMQTT("⚠ Self-Check: Referensi putih belum disimpan.");
    return;
  }

  int r_current = 0, g_current = 0, b_current = 0;
  // Ambil beberapa sampel RGB saat ini
  for (int i = 0; i < 6; i++) {
    bacaSensorWarna();
    r_current += redValue;
    g_current += greenValue;
    b_current += blueValue;
    delay(200); 
  }

  r_current /= 6; // Hitung rata-rata
  g_current /= 6;
  b_current /= 6;

  // Hitung selisih rata-rata (delta)
  int delta = (abs(r_current - ref.r) + abs(g_current - ref.g) + abs(b_current - ref.b)) / 3;
  Serial.printf("📊 RGB Sekarang: R=%d G=%d B=%d\n", r_current, g_current, b_current);
  Serial.printf("📌 Referensi    : R=%d G=%d B=%d\n", ref.r, ref.g, ref.b);
  Serial.printf("Δ (delta): %d\n", delta);

  char pesan[200];
  if (delta > 10) { // Jika selisih melebihi toleransi (10), anggap tidak stabil
    snprintf(pesan, sizeof(pesan),
             "📊 Current: R%d G%d B%d\n📌 Ref: R%d G%d B%d\nΔ=%d => ⚠ Tidak stabil. Kalibrasi ulang disarankan.",
             r_current, g_current, b_current,
             ref.r, ref.g, ref.b,
             delta);
    Serial.println("⚠ Sensor tidak stabil. Kalibrasi ulang disarankan.");
  } else {
    snprintf(pesan, sizeof(pesan),
             "📊 Current: R%d G%d B%d\n📌 Ref: R%d G%d B%d\nΔ=%d => ✅ Sensor stabil.",
             r_current, g_current, b_current,
             ref.r, ref.g, ref.b,
             delta);
    Serial.println("✅ Sensor stabil.");
  }
  kirimStatusMQTT(pesan); // Kirim hasil self-check ke aplikasi
}