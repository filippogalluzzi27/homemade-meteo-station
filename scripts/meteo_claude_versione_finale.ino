  // Per test ogni 2 minuti invece di 10:
  // int currentBlock = now.minute() / 2;
  // if (currentBlock != lastTriggeredBlock && now.minute() % 2 == 0)

  #include <WiFi.h>
  #include <HTTPClient.h>
  #include <Wire.h>
  #include <SPI.h>
  #include <SD.h>
  #include <RTClib.h>
  #include <Adafruit_SHT31.h>
  #include <time.h>
  #include <esp_task_wdt.h>
  #include <Adafruit_BMP3XX.h>


  // =======================
  // CONFIGURA QUI
  // =======================
  const char* ssid     = "...";
  const char* password = "...";
  String apiKey        = "...";   // Write API Key del canale ThingSpeak
  #define SD_CS    10
  #define RAIN_PIN  2

  // =======================
  // OGGETTI
  // =======================
  RTC_DS3231 rtc;
  Adafruit_SHT31 sht31 = Adafruit_SHT31();
  Adafruit_BMP3XX bmp;


  // =======================
  // COSTANTI
  // =======================
  const float         RAIN_PER_TIP        = 0.29;
  const unsigned long SEND_INTERVAL       = 20000;
  const unsigned long NTP_SYNC_INTERVAL   = 86400000UL;

  // =======================
  // VARIABILI PIOGGIA
  // =======================
  #define RAIN_DEBOUNCE_MS    1000
  #define MAX_VALID_INTENSITY 500.0

  volatile unsigned long rainTips   = 0;
  volatile unsigned long lastTipTime = 0;

  // FIX BUG 1 + 2: tre contatori separati con ruoli distinti
  // - lastBlockTips    → usato SOLO dal blocco 10 min per calcolare mm caduti
  // - lastIntensityTips → usato SOLO da updateRainIntensity() per calcolare intensità
  // Prima c'era un solo lastProcessedTips condiviso che causava tipsBlock = 0
  //   e impediva il calcolo dell'intensità.
  unsigned long lastBlockTips     = 0;
  unsigned long lastIntensityTips = 0;

  unsigned long previousTipTime   = 0;
  float         maxIntensityBlock = 0.0;

  // =======================
  // TIMER
  // =======================
  unsigned long lastSend    = 0;
  unsigned long lastNTPSync = 0;

  // Trigger robusto
  int lastTriggeredBlock = -1;

  // =======================
  // INTERRUPT PIOGGIA
  // =======================
  void IRAM_ATTR rainISR() {
    unsigned long now = millis();
    if (now - lastTipTime > RAIN_DEBOUNCE_MS) {
      rainTips++;
      lastTipTime = now;
    }
  }

  // =======================
  // INTENSITÀ PIOGGIA
  // Usa lastIntensityTips, separato da lastBlockTips.
  // Calcola mm/h tra due tip consecutivi e aggiorna il massimo del blocco.
  // =======================
  void updateRainIntensity() {
    static unsigned long lastLoggedTips = 0;

    noInterrupts();
    unsigned long currentTips = rainTips;
    unsigned long tipTimeCopy = lastTipTime;
    interrupts();

    if (currentTips != lastLoggedTips) {
      Serial.println("[TIP] millis=" + String(tipTimeCopy) + " totale=" + String(currentTips));
      lastLoggedTips = currentTips;
    }

    noInterrupts();
    unsigned long tipsNow    = rainTips;
    unsigned long tipTimeNow = lastTipTime;
    interrupts();

    if (tipsNow <= lastIntensityTips) return;

    // Se è il primo tip in assoluto
    if (previousTipTime == 0) {
      previousTipTime   = tipTimeNow;
      lastIntensityTips = tipsNow;
      return;
    }

    // Se è passato più di 1 ora → considera questo come nuovo inizio
    if (millis() - previousTipTime > 3600000UL) {
      previousTipTime   = tipTimeNow;
      lastIntensityTips = tipsNow;
      return;
    }

    float deltaSec = (tipTimeNow - previousTipTime) / 1000.0;

    if (deltaSec > 0.5) {
      float intensity = RAIN_PER_TIP * 3600.0 / deltaSec;

      if (intensity < MAX_VALID_INTENSITY) {
        if (intensity > maxIntensityBlock) {
          maxIntensityBlock = intensity;
        }
      }
    }

    previousTipTime   = tipTimeNow;
    lastIntensityTips = tipsNow;
  }


  // =======================
  // WIFI
  // =======================
  void connectWiFi() {
    WiFi.begin(ssid, password);
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
      delay(500);
    }
  }

  void checkWiFi() {
    if (WiFi.status() == WL_CONNECTED) return;

    Serial.println("WiFi DISCONNESSO - Tentativo riconnessione...");
    WiFi.disconnect();
    WiFi.begin(ssid, password);

    unsigned long startAttempt = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 10000) {
      delay(500);
      Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nWiFi RICONNESSO");
    } else {
      Serial.println("\nWiFi NON disponibile");
    }
  }

  // =======================
  // NTP SYNC
  // =======================
  void syncRTCfromNTP() {
    configTime(0, 0, "pool.ntp.org");
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      rtc.adjust(DateTime(
        timeinfo.tm_year + 1900,
        timeinfo.tm_mon + 1,
        timeinfo.tm_mday,
        timeinfo.tm_hour,
        timeinfo.tm_min,
        timeinfo.tm_sec
      ));
    }
  }

  // =======================
  // SD
  // =======================
  void appendToFile(String filename, String data) {
    File file = SD.open(filename, FILE_APPEND);
    if (file) {
      file.println(data);
      file.close();
    }
  }

  // =======================
  // INVIO THINGSPEAK
  // FIX BUG 3: aggiunto timeout HTTP e lastSend aggiornato SEMPRE
  //   (prima lastSend veniva aggiornato solo al successo → in assenza di internet
  //   il loop riprovava immediatamente ad ogni iterazione bloccandosi per 30s)
  // =======================
  bool sendToThingSpeak(String dataLine) {

    if (millis() - lastSend < SEND_INTERVAL)  return false;
    if (WiFi.status() != WL_CONNECTED)        return false;

    HTTPClient http;
    String url = "http://api.thingspeak.com/update";

    http.setTimeout(5000);   // FIX BUG 3a: max 5s, evita blocco del loop
    http.begin(url);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");

    Serial.println("[WIFI START] millis=" + String(millis()));
    int httpCode = http.POST(dataLine);
    Serial.println("[WIFI END] millis=" + String(millis()));
    http.end();

    lastSend = millis();     // FIX BUG 3b: aggiorna SEMPRE per rispettare il rate limit

    if (httpCode == 200) {
      Serial.println("Invio ThingSpeak OK");
      return true;
    }

    Serial.println("Invio ThingSpeak FALLITO (httpCode: " + String(httpCode) + ")");
    return false;
  }

  // =======================
  // BUFFER INVIO
  // =======================
  void processBuffer() {

    if (WiFi.status() != WL_CONNECTED)        return;
    if (millis() - lastSend < SEND_INTERVAL)  return;

    File file = SD.open("/buffer.csv");
    if (!file) return;

    String firstLine = file.readStringUntil('\n');
    file.close();

    if (firstLine.length() == 0) return;

    if (sendToThingSpeak(firstLine)) {

      File readFile  = SD.open("/buffer.csv");
      File writeFile = SD.open("/temp.csv", FILE_WRITE);

      bool skip = true;
      while (readFile.available()) {
        String line = readFile.readStringUntil('\n');
        if (skip) { skip = false; continue; }
        writeFile.println(line);
      }

      readFile.close();
      writeFile.close();

      SD.remove("/buffer.csv");
      SD.rename("/temp.csv", "/buffer.csv");
    }
  }

  // =======================
  // SETUP
  // =======================
  void setup() {

    Serial.begin(115200);
    Wire.begin();

    sht31.begin(0x44);
    if (!bmp.begin_I2C(0x77)) {  
      Serial.println("Errore BMP388");
    } else {
      bmp.setTemperatureOversampling(BMP3_OVERSAMPLING_8X);
      bmp.setPressureOversampling(BMP3_OVERSAMPLING_4X);
      bmp.setIIRFilterCoeff(BMP3_IIR_FILTER_COEFF_3);
      bmp.setOutputDataRate(BMP3_ODR_50_HZ);
    }
    rtc.begin();

    pinMode(RAIN_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(RAIN_PIN), rainISR, FALLING);

    SD.begin(SD_CS);

    connectWiFi();
    syncRTCfromNTP();
    esp_task_wdt_init(30, true);   // timeout 30 secondi, reset automatico su blocco
    esp_task_wdt_add(NULL);        // registra il task principale (loop)
  }

  // =======================
  // LOOP
  // =======================
  void loop() {
    esp_task_wdt_reset();
    checkWiFi();
    DateTime now = rtc.now();
    updateRainIntensity();

    // FIX BUG 4: usa checkWiFi() invece di connectWiFi() per non riavviare
    //   inutilmente una connessione già attiva durante la sync NTP giornaliera
    if (millis() - lastNTPSync > NTP_SYNC_INTERVAL) {
      checkWiFi();
      if (WiFi.status() == WL_CONNECTED) {
        syncRTCfromNTP();
      }
      lastNTPSync = millis();
    }

    // =======================
    // TRIGGER ROBUSTO 10 MINUTI
    // =======================
    int currentBlock = now.minute() / 10;

    if (currentBlock != lastTriggeredBlock && now.minute() % 10 == 0) {

      lastTriggeredBlock = currentBlock;

      float temperature = sht31.readTemperature();
      float humidity    = sht31.readHumidity();
      if (!bmp.performReading()) {
        Serial.println("Errore lettura BMP388");
        return;
      }

      float pressure_hPa = bmp.pressure / 100.0; // da Pa → hPa
      float bmpTemp = bmp.temperature;           // opzionale
      if (isnan(temperature) || isnan(humidity)) {
        Serial.println("Errore sensore SHT31 - skip");
        noInterrupts();
        lastBlockTips = rainTips;   // azzera i contatori anche se saltiamo l'invio
        interrupts();
        maxIntensityBlock = 0.0;
        return;
      }

      // FIX BUG 1: usa lastBlockTips (dedicato) invece di lastProcessedTips
      noInterrupts();
      unsigned long tipsNow = rainTips;
      interrupts();

      unsigned long tipsBlock = tipsNow - lastBlockTips;
      lastBlockTips = tipsNow;          // aggiorna SOLO il contatore del blocco

      float rain10      = tipsBlock * RAIN_PER_TIP;
      float maxIntensity = maxIntensityBlock;
      maxIntensityBlock  = 0.0;

      char timestamp[25];
      sprintf(timestamp, "%04d-%02d-%02dT%02d:%02d:%02d",
              now.year(), now.month(), now.day(),
              now.hour(), now.minute(), now.second());

      String csvLine = String(timestamp)          + "," +
                      String(temperature, 2)     + "," +
                      String(humidity, 2)        + "," +
                      String(pressure_hPa, 2)    + "," +
                      String(rain10, 3)          + "," +
                      String(maxIntensity, 3);

      appendToFile("/meteo.csv", csvLine);

      String tsData = "api_key=" + apiKey +
                      "&field1=" + String(temperature, 2) +
                      "&field2=" + String(humidity, 2)    +
                      "&field5=" + String(pressure_hPa, 2) +
                      "&field3=" + String(rain10, 3)      +
                      "&field4=" + String(maxIntensity, 3) +
                      "&created_at=" + String(timestamp);

      if (!sendToThingSpeak(tsData)) {
        Serial.println("Salvo su BUFFER");
        appendToFile("/buffer.csv", tsData);
      }
    }

    processBuffer();
  }
