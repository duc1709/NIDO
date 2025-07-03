#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <HardwareSerial.h>
#include <DFRobotDFPlayerMini.h>

// ——— Credenciales Wi-Fi y OpenWeather ———
const char* ssid     = "Red Alumnos Libre";
const char* password = "";
const char* apiKey   = "d762ca7bedb7ea912b92a928f0dc236e";

// ——— Pines ———
const int ledPin     = 10;    // LED siempre ENCENDIDO apuntando al LDR
const int ldrPin     = A3;    // LDR en divisor
const int potMoodPin = A4;    // potenciómetro lineal para mood/track
const int potVolPin  = A0;    // potenciómetro circular para volumen
const int threshold  = 700;   // umbral LDR

// ——— DFPlayer ———
HardwareSerial      dfSerial(1);  // RX=GPIO2, TX=GPIO1
DFRobotDFPlayerMini dfPlayer;
bool                dfReady = false;

// ——— Control reproducción ———
bool      waiting       = false;
unsigned long waitStart = 0;
bool      playing       = false;
unsigned long playStart = 0;
// 5 minutos en milisegundos
const unsigned long playDuration = 5UL * 60UL * 1000UL;

// ——— Ubicación/clima ———
float latitude = 0, longitude = 0;

int readMood() {
  int v = analogRead(potMoodPin);
  if (v < 1365)      return 1;
  else if (v < 2730) return 2;
  else               return 3;
}
const char* moodName(int m) {
  return m==1 ? "Triste" : (m==2 ? "Neutro" : "Feliz");
}

void setup() {
  Serial.begin(115200);
  delay(500);

  // LED siempre ON
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, HIGH);
  Serial.println("LED ON");

  // Pines analógicos
  pinMode(ldrPin, INPUT);
  pinMode(potMoodPin, INPUT);
  pinMode(potVolPin, INPUT);

  // 1) Conectar Wi-Fi
  Serial.print("Conectando a WiFi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(500);
  }
  Serial.println("\nWiFi conectado: " + WiFi.localIP().toString());

  // 2) Detectar ubicación
  Serial.println("Detectando ubicación...");
  {
    HTTPClient http;
    http.begin("http://ip-api.com/json?fields=lat,lon");
    if (http.GET() == 200) {
      StaticJsonDocument<256> doc;
      deserializeJson(doc, http.getString());
      latitude  = doc["lat"];
      longitude = doc["lon"];
      Serial.printf("Ubic: %.4f, %.4f\n", latitude, longitude);
    } else {
      Serial.printf("Error ubicación: %d\n", http.GET());
    }
    http.end();
  }

  // 3) Obtener clima
  Serial.println("Obteniendo clima...");
  {
    HTTPClient http;
    String url = String("http://api.openweathermap.org/data/2.5/weather?lat=")
               + String(latitude,6)
               + "&lon=" + String(longitude,6)
               + "&units=metric&appid=" + apiKey;
    http.begin(url);
    if (http.GET() == 200) {
      StaticJsonDocument<512> doc;
      deserializeJson(doc, http.getString());
      String weather = doc["weather"][0]["main"].as<String>();
      float temp     = doc["main"]["temp"].as<float>();
      Serial.printf("Clima: %s, %.1f°C\n", weather.c_str(), temp);
    } else {
      Serial.printf("Error clima: %d\n", http.GET());
    }
    http.end();
  }

  // 4) Iniciar DFPlayer
  Serial.println("Iniciando DFPlayer...");
  dfSerial.begin(9600, SERIAL_8N1, 2, 1);
  delay(200);
  dfReady = dfPlayer.begin(dfSerial);
  if (dfReady) {
    Serial.println("DFPlayer listo");
    dfPlayer.outputDevice(DFPLAYER_DEVICE_SD);
    dfPlayer.volume(25);
  } else {
    Serial.println("Error DFPlayer");
  }
}

void loop() {
  unsigned long now = millis();

  // Leer LDR
  int ldrVal = analogRead(ldrPin);
  Serial.printf("LDR: %d  ", ldrVal);

  // Control de volumen
  if (dfReady) {
    int volRaw = analogRead(potVolPin);
    int vol    = map(volRaw, 0, 4095, 0, 30);
    dfPlayer.volume(vol);
    Serial.printf("Vol: %d/30\n", vol);
  } else {
    Serial.println();
  }

  if (playing) {
    // Si la luz vuelve, pause y reinicie ciclo
    if (ldrVal > threshold) {
      dfPlayer.pause();
      playing = false;
      waiting = false;
      Serial.println("Luz restaurada: pausa y reinicio");
    }
    // O si cumple 5 minutos, detenga
    else if (now - playStart >= playDuration) {
      dfPlayer.stop();
      playing = false;
      Serial.println("5 min cumplidos: deteniendo");
    }
  }
  else {
    // No está sonando: detectar sombra
    if (ldrVal < threshold) {
      if (!waiting) {
        waiting = true;
        waitStart = now;
        Serial.printf("Sombra detectada: cuenta 5s para mood (%s)\n",
                      moodName(readMood()));
      } else {
        Serial.printf("  (%lus)\n", (now - waitStart)/1000);
      }
      // Tras 5 s, reproducir según pot
      if (waiting && now - waitStart >= 5000) {
        int mood = readMood();
        int track = mood; // 1–3
        Serial.printf("Reproduciendo T%d (%s)\n", track, moodName(mood));
        dfPlayer.play(track);
        playing   = true;
        playStart = now;
        waiting   = false;
      }
    } else if (waiting) {
      // Luz vuelve antes de 5s: cancelar
      waiting = false;
      Serial.println("Luz suficiente: cuenta cancelada");
    } else {
      Serial.println();
    }
  }

  delay(200);
}

