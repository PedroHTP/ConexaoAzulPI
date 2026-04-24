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

// Debug serial inspirado na estrutura do outro firmware.
const bool SERIAL_DEBUG_HUMAN = true;
const bool SERIAL_DIAGNOSTICS_JSON = true;

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
bool ultimoEnvioFirebaseOk = false;
int ultimoHttpCode = 0;

void imprimirSeparador() {
  Serial.println("--------------------------------------------------");
}

void printJsonBool(bool value) {
  Serial.print(value ? "true" : "false");
}

const char* statusWiFiTexto() {
  return WiFi.status() == WL_CONNECTED ? "CONECTADO" : "DESCONECTADO";
}

const char* statusNtpTexto() {
  return relogioSincronizado ? "SINCRONIZADO" : "NAO SINCRONIZADO";
}

void imprimirStatusConectividade() {
  Serial.print("Wi-Fi: ");
  Serial.print(statusWiFiTexto());
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print(" | IP: ");
    Serial.print(WiFi.localIP());
  }
  Serial.print(" | NTP: ");
  Serial.println(statusNtpTexto());

  Serial.print("Firebase URL: ");
  Serial.println(FIREBASE_DATABASE_URL);
  Serial.print("Firebase Path: ");
  Serial.println(FIREBASE_RTDB_PATH);
  Serial.print("Ultimo envio Firebase: ");
  Serial.print(ultimoEnvioFirebaseOk ? "SUCESSO" : "PENDENTE/FALHA");
  Serial.print(" | HTTP: ");
  Serial.println(ultimoHttpCode);
}

void imprimirLeiturasSensores(
  float nivelAguaCm,
  float temperatura,
  float turbidezNTU,
  int pureza,
  float ph
) {
  Serial.println("Leituras dos sensores:");

  Serial.print("  Nivel da agua: ");
  if (nivelAguaCm < 0) {
    Serial.println("erro");
  } else {
    Serial.print(nivelAguaCm, 1);
    Serial.println(" cm");
  }

  Serial.print("  Temperatura: ");
  Serial.print(temperatura, 1);
  Serial.println(" C");

  Serial.print("  Turbidez: ");
  Serial.print(turbidezNTU, 1);
  Serial.println(" NTU");

  Serial.print("  Pureza estimada: ");
  Serial.print(pureza);
  Serial.println("%");

  Serial.print("  pH: ");
  Serial.println(ph, 2);
}

String classificarLeitura(
  float ph,
  float turbidezNTU,
  float temperatura,
  float nivelAguaCm
) {
  if (nivelAguaCm < 0 || turbidezNTU > 25 || ph < 6.0 || ph > 9.0 || temperatura < 15.0 || temperatura > 35.0) {
    return "CRITICA";
  }

  if (turbidezNTU > 5 || ph < 6.5 || ph > 8.5 || temperatura < 20.0 || temperatura > 30.0 || nivelAguaCm < 40.0 || nivelAguaCm > 180.0) {
    return "ALERTA";
  }

  return "BOA";
}

String diagnosticarTemperatura(float rawTempC, bool tempFallback) {
  if (tempFallback) return "sensor_ausente_ou_falha";
  if (rawTempC < -20.0 || rawTempC > 85.0) return "fora_da_faixa_esperada";
  return "ok";
}

String diagnosticarPH(float vPH, float phFinal) {
  if (vPH < 0.02) return "sem_sinal";
  if (phFinal < 0.0 || phFinal > 14.0) return "fora_da_escala";
  return "ok";
}

String diagnosticarTurbidez(int adcTurb, float vTurb) {
  if (adcTurb <= 10) return "sem_sinal";
  if (adcTurb >= 4090) return "saturado";
  if (vTurb < 0.05) return "sinal_muito_baixo";
  return "ok";
}

String diagnosticarNivel(float nivelAguaCm, bool nivelErro) {
  if (nivelErro) return "sem_echo_ou_timeout";
  if (nivelAguaCm < 0.0) return "invalido";
  if (nivelAguaCm > 400.0) return "fora_da_faixa_esperada";
  return "ok";
}

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

float lerNivelAguaCm(bool &nivelErro) {
  digitalWrite(pinoTrig, LOW);
  delayMicroseconds(2);
  digitalWrite(pinoTrig, HIGH);
  delayMicroseconds(10);
  digitalWrite(pinoTrig, LOW);

  unsigned long duracao = pulseIn(pinoEcho, HIGH, 30000);
  if (duracao == 0) {
    nivelErro = true;
    return -1.0;
  }

  nivelErro = false;
  return duracao * 0.034f / 2.0f;
}

float lerTemperaturaC(float &rawTempC, bool &tempFallback) {
  sensors.requestTemperatures();
  rawTempC = sensors.getTempCByIndex(0);

  tempFallback = (rawTempC == DEVICE_DISCONNECTED_C || rawTempC == -127.0);
  if (tempFallback) {
    return 25.0;
  }

  return rawTempC;
}

float lerPh(int &adcPH, float &vPH) {
  long somaPH = 0;
  for (int i = 0; i < 100; i++) {
    somaPH += analogRead(pinoPH);
    delay(1);
  }

  adcPH = round((float)somaPH / 100.0f);
  vPH = adcPH * (3.3f / 4095.0f);
  float phInstantaneo = phReferencia - ((vPH - voltagemReferencia) * multiplicadorPH);

  phEstabilizado = (phEstabilizado * 0.80f) + (phInstantaneo * 0.20f);

  if (phEstabilizado > 14.0f) {
    phEstabilizado = 14.0f;
  }
  if (phEstabilizado < 0.0f) {
    phEstabilizado = 0.0f;
  }

  return phEstabilizado;
}

float lerTurbidezNTU(int &adcTurb, float &vTurb, int &pureza) {
  long soma = 0;
  for (int i = 0; i < 20; i++) {
    soma += analogRead(pinoTurbidez);
    delay(2);
  }

  adcTurb = round((float)soma / 20.0f);
  vTurb = adcTurb * 3.3f / 4095.0f;

  int leituraLimitada = constrain(adcTurb, turbidezLimpaADC, turbidezSujaADC);
  float turbidezNTU = map(leituraLimitada, turbidezLimpaADC, turbidezSujaADC, 0, 100);
  pureza = constrain(100 - (int)round(turbidezNTU), 0, 100);
  return turbidezNTU;
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
  float nivelAguaCm,
  float rawTempC,
  bool tempFallback,
  bool nivelErro,
  int adcPH,
  float vPH,
  int adcTurb,
  float vTurb
) {
  String payload = "{";
  String status = classificarLeitura(ph, turbidezNTU, temperatura, nivelAguaCm);
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
  payload += "\"status\":\"";
  payload += status;
  payload += "\",";
  payload += "\"source\":\"esp32-serial-usb\"";

  if (SERIAL_DIAGNOSTICS_JSON) {
    payload += ",\"diag\":{";
    payload += "\"temp_status\":\"";
    payload += diagnosticarTemperatura(rawTempC, tempFallback);
    payload += "\",\"temp_raw_c\":";
    payload += String(rawTempC, 1);
    payload += ",\"temp_fallback\":";
    payload += tempFallback ? "true" : "false";
    payload += ",\"ph_status\":\"";
    payload += diagnosticarPH(vPH, ph);
    payload += "\",\"ph_adc\":";
    payload += String(adcPH);
    payload += ",\"ph_v\":";
    payload += String(vPH, 3);
    payload += ",\"turbidity_status\":\"";
    payload += diagnosticarTurbidez(adcTurb, vTurb);
    payload += "\",\"turbidity_adc\":";
    payload += String(adcTurb);
    payload += ",\"turbidity_v\":";
    payload += String(vTurb, 3);
    payload += ",\"water_level_status\":\"";
    payload += diagnosticarNivel(nivelAguaCm, nivelErro);
    payload += "\"}";
  }

  payload += "}";
  return payload;
}

void printJsonReading(
  float ph,
  float turbidezNTU,
  float temperatura,
  float nivelAguaCm,
  float rawTempC,
  bool tempFallback,
  bool nivelErro,
  int adcPH,
  float vPH,
  int adcTurb,
  float vTurb
) {
  String payload = montarPayloadSerialUsb(
    temperatura,
    turbidezNTU,
    ph,
    nivelAguaCm,
    rawTempC,
    tempFallback,
    nivelErro,
    adcPH,
    vPH,
    adcTurb,
    vTurb
  );
  Serial.println("SERIAL_JSON:" + payload);
}

void printHumanReadableReading(
  float ph,
  float turbidezNTU,
  float temperatura,
  float nivelAguaCm,
  int pureza,
  float rawTempC,
  bool tempFallback,
  bool nivelErro,
  int adcPH,
  float vPH,
  int adcTurb,
  float vTurb
) {
  String status = classificarLeitura(ph, turbidezNTU, temperatura, nivelAguaCm);

  Serial.println("Resumo legivel:");
  Serial.print("  STATUS: ");
  Serial.println(status);

  imprimirLeiturasSensores(nivelAguaCm, temperatura, turbidezNTU, pureza, ph);

  Serial.print("  [DIAG] TEMP: ");
  Serial.print(diagnosticarTemperatura(rawTempC, tempFallback));
  Serial.print(" | raw=");
  Serial.print(rawTempC, 1);
  Serial.print(" C | fallback=");
  Serial.println(tempFallback ? "sim" : "nao");

  Serial.print("  [DIAG] pH: ");
  Serial.print(diagnosticarPH(vPH, ph));
  Serial.print(" | ADC=");
  Serial.print(adcPH);
  Serial.print(" | V_pH=");
  Serial.print(vPH, 3);
  Serial.println("V");

  Serial.print("  [DIAG] TURB: ");
  Serial.print(diagnosticarTurbidez(adcTurb, vTurb));
  Serial.print(" | ADC=");
  Serial.print(adcTurb);
  Serial.print(" | V_Turb=");
  Serial.print(vTurb, 3);
  Serial.println("V");

  Serial.print("  [DIAG] NIVEL: ");
  Serial.println(diagnosticarNivel(nivelAguaCm, nivelErro));
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
    ultimoEnvioFirebaseOk = false;
    ultimoHttpCode = 0;
    return;
  }

  if (!relogioSincronizado) {
    Serial.println("Envio cancelado: horario ainda nao sincronizado.");
    ultimoEnvioFirebaseOk = false;
    ultimoHttpCode = 0;
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
  ultimoHttpCode = httpCode;
  ultimoEnvioFirebaseOk = httpCode >= 200 && httpCode < 300;

  Serial.print("PUT ");
  Serial.print(url);
  Serial.print(" -> HTTP ");
  Serial.println(httpCode);

  if (httpCode > 0) {
    String resposta = http.getString();
    Serial.println("Resposta do Firebase:");
    Serial.println(resposta);
    if (ultimoEnvioFirebaseOk) {
      Serial.println("Leitura gravada com sucesso no Firebase.");
    } else {
      Serial.println("Firebase respondeu, mas a gravacao nao foi confirmada como sucesso.");
    }
  } else {
    Serial.print("Falha no envio: ");
    Serial.println(http.errorToString(httpCode));
    Serial.println("Leitura NAO foi gravada no Firebase.");
  }

  http.end();
}

void enviarLeituraSerialUsb(
  float temperatura,
  float turbidezNTU,
  float ph,
  float nivelAguaCm,
  float rawTempC,
  bool tempFallback,
  bool nivelErro,
  int adcPH,
  float vPH,
  int adcTurb,
  float vTurb,
  int pureza
) {
  ultimoEnvioFirebaseOk = false;
  ultimoHttpCode = 0;
  Serial.println("Firebase indisponivel neste ciclo. Ativando fallback por Serial USB.");
  Serial.println("FALLBACK_SERIAL_USB");
  printJsonReading(
    ph,
    turbidezNTU,
    temperatura,
    nivelAguaCm,
    rawTempC,
    tempFallback,
    nivelErro,
    adcPH,
    vPH,
    adcTurb,
    vTurb
  );

  if (SERIAL_DEBUG_HUMAN) {
    printHumanReadableReading(
      ph,
      turbidezNTU,
      temperatura,
      nivelAguaCm,
      pureza,
      rawTempC,
      tempFallback,
      nivelErro,
      adcPH,
      vPH,
      adcTurb,
      vTurb
    );
  }
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

  bool nivelErro = false;
  float nivelAguaCm = lerNivelAguaCm(nivelErro);

  float rawTempC = 0.0f;
  bool tempFallback = false;
  float temperatura = lerTemperaturaC(rawTempC, tempFallback);

  int adcPH = 0;
  float vPH = 0.0f;
  float ph = lerPh(adcPH, vPH);

  int adcTurb = 0;
  float vTurb = 0.0f;
  int pureza = 0;
  float turbidezNTU = lerTurbidezNTU(adcTurb, vTurb, pureza);

  imprimirSeparador();
  imprimirStatusConectividade();
  imprimirLeiturasSensores(nivelAguaCm, temperatura, turbidezNTU, pureza, ph);

  if (nivelAguaCm >= 0 && temperatura > -100.0f) {
    if (WiFi.status() == WL_CONNECTED && relogioSincronizado) {
      Serial.println("Modo ativo: envio direto para Firebase.");
      enviarLeitura(temperatura, turbidezNTU, ph, nivelAguaCm);
    } else {
      Serial.println("Modo ativo: fallback por Serial USB.");
      enviarLeituraSerialUsb(
        temperatura,
        turbidezNTU,
        ph,
        nivelAguaCm,
        rawTempC,
        tempFallback,
        nivelErro,
        adcPH,
        vPH,
        adcTurb,
        vTurb,
        pureza
      );
    }
  } else {
    Serial.println("Leitura ignorada por erro de sensor.");
    ultimoEnvioFirebaseOk = false;
    ultimoHttpCode = 0;
  }

  imprimirStatusConectividade();
  imprimirSeparador();

  digitalWrite(LED_INTERNO, LOW);
}
