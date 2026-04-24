#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <time.h>

#define LED_INTERNO 2

const int pinoTrig = 5;
const int pinoEcho = 18;
const int pinoTurbidez = 34;
const int pinoPH = 32;
const int pinoTemp = 4;

// Credenciais de rede e Firebase.
const char* WIFI_SSID = "SEU_WIFI";
const char* WIFI_PASSWORD = "SUA_SENHA";
const char* FIREBASE_DATABASE_URL = "https://conexaoazul-295f4-default-rtdb.firebaseio.com";
const char* FIREBASE_RTDB_PATH = "water_readings";
const char* FIREBASE_AUTH_TOKEN = "";
const char* DEVICE_ID = "esp32-rio-01";
const char* DEVICE_LOCATION = "Rio Sao Francisco";

// Intervalo entre envios para o backend.
const unsigned long INTERVALO_ENVIO_MS = 15000;

// Calibracao do pH.
const float voltagemReferencia = 1.93;
const float phReferencia = 7.0;
const float multiplicadorPH = 112.0;
float phEstabilizado = 7.0;

// Calibracao da turbidez.
const int turbidezLimpaADC = 1100;
const int turbidezSujaADC = 2000;

OneWire oneWire(pinoTemp);
DallasTemperature sensors(&oneWire);

unsigned long ultimoEnvio = 0;
bool relogioSincronizado = false;

void conectarWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  Serial.print("Conectando ao Wi-Fi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int tentativas = 0;
  while (WiFi.status() != WL_CONNECTED && tentativas < 30) {
    delay(500);
    Serial.print(".");
    tentativas++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.print("Wi-Fi conectado. IP local: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println();
    Serial.println("Falha ao conectar no Wi-Fi.");
  }
}

void sincronizarRelogio() {
  if (relogioSincronizado || WiFi.status() != WL_CONNECTED) {
    return;
  }

  configTime(0, 0, "pool.ntp.org", "time.nist.gov");

  Serial.print("Sincronizando horario via NTP");
  time_t agora = time(nullptr);
  int tentativas = 0;
  while (agora < 1700000000 && tentativas < 20) {
    delay(500);
    Serial.print(".");
    agora = time(nullptr);
    tentativas++;
  }

  if (agora >= 1700000000) {
    relogioSincronizado = true;
    Serial.println();
    Serial.println("Horario sincronizado.");
  } else {
    Serial.println();
    Serial.println("Nao foi possivel sincronizar o horario. Tentarei novamente.");
  }
}

float lerNivelAguaCm() {
  digitalWrite(pinoTrig, LOW);
  delayMicroseconds(2);
  digitalWrite(pinoTrig, HIGH);
  delayMicroseconds(10);
  digitalWrite(pinoTrig, LOW);

  unsigned long duracao = pulseIn(pinoEcho, HIGH, 30000);
  if (duracao == 0) {
    return -1.0;
  }

  return duracao * 0.034f / 2.0f;
}

float lerTemperaturaC() {
  sensors.requestTemperatures();
  float temperatura = sensors.getTempCByIndex(0);

  if (temperatura == DEVICE_DISCONNECTED_C) {
    return -127.0;
  }

  return temperatura;
}

float lerPh() {
  long somaPH = 0;
  for (int i = 0; i < 100; i++) {
    somaPH += analogRead(pinoPH);
    delay(1);
  }

  float mediaBrutaPH = (float)somaPH / 100.0f;
  float voltPH = mediaBrutaPH * (3.3f / 4095.0f);
  float phInstantaneo = phReferencia - ((voltPH - voltagemReferencia) * multiplicadorPH);

  phEstabilizado = (phEstabilizado * 0.80f) + (phInstantaneo * 0.20f);

  if (phEstabilizado > 14.0f) {
    phEstabilizado = 14.0f;
  }
  if (phEstabilizado < 0.0f) {
    phEstabilizado = 0.0f;
  }

  return phEstabilizado;
}

int lerTurbidezBruta() {
  long soma = 0;
  for (int i = 0; i < 20; i++) {
    soma += analogRead(pinoTurbidez);
    delay(2);
  }

  return (int)(soma / 20);
}

float calcularTurbidezNTU(int leituraBruta) {
  int leituraLimitada = constrain(leituraBruta, turbidezLimpaADC, turbidezSujaADC);
  return map(leituraLimitada, turbidezLimpaADC, turbidezSujaADC, 0, 100);
}

int calcularPurezaPercentual(float turbidezNTU) {
  int pureza = 100 - (int)round(turbidezNTU);
  return constrain(pureza, 0, 100);
}

String montarTimestampIso8601(time_t timestampUtc) {
  struct tm timeinfo;
  gmtime_r(&timestampUtc, &timeinfo);

  char buffer[25];
  strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
  return String(buffer);
}

String montarPayloadJson(
  float temperatura,
  float turbidezNTU,
  float ph,
  float nivelAguaCm,
  String measuredAtIso,
  unsigned long measuredAtUnix
) {
  String payload = "{";
  payload += "\"device_id\":\"";
  payload += DEVICE_ID;
  payload += "\",";
  payload += "\"location\":\"";
  payload += DEVICE_LOCATION;
  payload += "\",";
  payload += "\"temperature_c\":";
  payload += String(temperatura, 2);
  payload += ",";
  payload += "\"turbidity_ntu\":";
  payload += String(turbidezNTU, 2);
  payload += ",";
  payload += "\"ph\":";
  payload += String(ph, 2);
  payload += ",";
  payload += "\"water_level_cm\":";
  payload += String(nivelAguaCm, 2);
  payload += ",";
  payload += "\"measured_at\":\"";
  payload += measuredAtIso;
  payload += "\",";
  payload += "\"measured_at_unix\":";
  payload += String(measuredAtUnix);
  payload += ",";
  payload += "\"received_at\":\"";
  payload += measuredAtIso;
  payload += "\",";
  payload += "\"received_at_unix\":";
  payload += String(measuredAtUnix);
  payload += ",";
  payload += "\"source\":\"esp32\"";
  payload += "}";
  return payload;
}

String montarPayloadSerialUsb(
  float temperatura,
  float turbidezNTU,
  float ph,
  float nivelAguaCm
) {
  String payload = "{";
  payload += "\"device_id\":\"";
  payload += DEVICE_ID;
  payload += "\",";
  payload += "\"location\":\"";
  payload += DEVICE_LOCATION;
  payload += "\",";
  payload += "\"temperature_c\":";
  payload += String(temperatura, 2);
  payload += ",";
  payload += "\"turbidity_ntu\":";
  payload += String(turbidezNTU, 2);
  payload += ",";
  payload += "\"ph\":";
  payload += String(ph, 2);
  payload += ",";
  payload += "\"water_level_cm\":";
  payload += String(nivelAguaCm, 2);
  payload += ",";
  payload += "\"source\":\"esp32-serial-usb\"";
  payload += "}";
  return payload;
}

String montarUrlFirebase(String readingId) {
  String url = String(FIREBASE_DATABASE_URL);
  if (!url.endsWith("/")) {
    url += "/";
  }

  url += FIREBASE_RTDB_PATH;
  url += "/";
  url += readingId;
  url += ".json";

  if (String(FIREBASE_AUTH_TOKEN).length() > 0) {
    url += "?auth=";
    url += FIREBASE_AUTH_TOKEN;
  }

  return url;
}

void enviarLeitura(float temperatura, float turbidezNTU, float ph, float nivelAguaCm) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Envio cancelado: Wi-Fi desconectado.");
    return;
  }

  if (!relogioSincronizado) {
    Serial.println("Envio cancelado: horario ainda nao sincronizado.");
    return;
  }

  time_t agora = time(nullptr);
  String measuredAtIso = montarTimestampIso8601(agora);
  unsigned long measuredAtUnix = (unsigned long)agora;
  String readingId = String(DEVICE_ID) + "-" + String(measuredAtUnix) + "-" + String(millis() % 100000);
  String url = montarUrlFirebase(readingId);
  String payload = montarPayloadJson(
    temperatura,
    turbidezNTU,
    ph,
    nivelAguaCm,
    measuredAtIso,
    measuredAtUnix
  );

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");
  int httpCode = http.PUT(payload);

  Serial.print("PUT ");
  Serial.print(url);
  Serial.print(" -> HTTP ");
  Serial.println(httpCode);

  if (httpCode > 0) {
    String resposta = http.getString();
    Serial.println("Resposta da API:");
    Serial.println(resposta);
  } else {
    Serial.print("Falha no envio: ");
    Serial.println(http.errorToString(httpCode));
  }

  http.end();
}

void enviarLeituraSerialUsb(
  float temperatura,
  float turbidezNTU,
  float ph,
  float nivelAguaCm
) {
  String payload = montarPayloadSerialUsb(temperatura, turbidezNTU, ph, nivelAguaCm);
  Serial.println("FALLBACK_SERIAL_USB");
  Serial.println("SERIAL_JSON:" + payload);
}

void setup() {
  Serial.begin(115200);
  sensors.begin();

  pinMode(LED_INTERNO, OUTPUT);
  pinMode(pinoTrig, OUTPUT);
  pinMode(pinoEcho, INPUT);
  pinMode(pinoPH, INPUT);
  pinMode(pinoTurbidez, INPUT);

  digitalWrite(LED_INTERNO, LOW);

  Serial.println("=======================================");
  Serial.println("   PROJETO CONEXAO AZUL - ESP32 WEB   ");
  Serial.println("=======================================");

  conectarWiFi();
  sincronizarRelogio();
}

void loop() {
  if (millis() - ultimoEnvio < INTERVALO_ENVIO_MS) {
    delay(100);
    return;
  }

  ultimoEnvio = millis();
  digitalWrite(LED_INTERNO, HIGH);

  conectarWiFi();
  sincronizarRelogio();

  float nivelAguaCm = lerNivelAguaCm();
  float temperatura = lerTemperaturaC();
  float ph = lerPh();
  int turbidezBruta = lerTurbidezBruta();
  float turbidezNTU = calcularTurbidezNTU(turbidezBruta);
  int pureza = calcularPurezaPercentual(turbidezNTU);

  Serial.print("Nivel: ");
  if (nivelAguaCm < 0) {
    Serial.print("Erro");
  } else {
    Serial.print(nivelAguaCm, 1);
    Serial.print(" cm");
  }

  Serial.print(" | Temp: ");
  Serial.print(temperatura, 1);
  Serial.print(" C | Turbidez: ");
  Serial.print(turbidezNTU, 1);
  Serial.print(" NTU | Pureza: ");
  Serial.print(pureza);
  Serial.print("% | pH: ");
  Serial.println(ph, 2);

  if (nivelAguaCm >= 0 && temperatura > -100.0f) {
    if (WiFi.status() == WL_CONNECTED && relogioSincronizado) {
      enviarLeitura(temperatura, turbidezNTU, ph, nivelAguaCm);
    } else {
      enviarLeituraSerialUsb(temperatura, turbidezNTU, ph, nivelAguaCm);
    }
  } else {
    Serial.println("Leitura ignorada por erro de sensor.");
  }

  digitalWrite(LED_INTERNO, LOW);
}
