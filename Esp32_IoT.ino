#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <math.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include "config.h" // Carga tus credenciales secretas

#define ANCHO_PANTALLA 128
#define ALTO_PANTALLA 64
Adafruit_SSD1306 display(ANCHO_PANTALLA, ALTO_PANTALLA, &Wire, -1);

Adafruit_MPU6050 mpu;
const int PIN_BUZZER = 18;
const int PIN_BOTON = 13;
const int PIN_LED = 4;
const float UMBRAL_ACELERACION = 26.0;

unsigned long tiempoInicioAlerta = 0;
const unsigned long DURACION_ALERTA = 10000;
bool estadoAlerta = false;
bool pantallaActualizada = false;

bool mostrandoCancelacion = false;
unsigned long tiempoInicioCancelacion = 0;
const unsigned long DURACION_CANCELACION = 2000;

unsigned long tiempoUltimoParpadeo = 0;
const unsigned long INTERVALO_PARPADEO = 250;
bool estadoLed = false;

float offsetX = 0, offsetY = 0, offsetZ = 0;

// --- OBJETOS Y VARIABLES PARA MQTT ---
WiFiClient espClient;
PubSubClient mqtt(espClient);
unsigned long tiempoUltimaPublicacion = 0;
const unsigned long INTERVALO_PUBLICACION_MQTT = 5000; // Publicar JSON cada 5 segundos

// Función básica de reconexión MQTT no bloqueante
void reconnectMQTT() {
  if (!mqtt.connected()) {
    Serial.print("Intentando conexión MQTT...");
    
    // IMPORTANTE: Se usa MQTT_CLIENT_ID en lugar de MQTT_USER como primer parámetro
    if (mqtt.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS)) {
      Serial.println("conectado.");
      
      // Resuscribir al tópico de comandos al reconectar exitosamente[cite: 1]
      mqtt.subscribe(TOPIC_CMD);
      
    } else {
      Serial.print("falló, rc=");
      Serial.println(mqtt.state());
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_BOTON, INPUT);
  pinMode(PIN_LED, OUTPUT);

  digitalWrite(PIN_BUZZER, LOW);
  digitalWrite(PIN_LED, LOW);

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C) || !mpu.begin()) {
    while(1) delay(10);
  }

  // --- ÍTEM A1: CONECTIVIDAD WIFI ---
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 10);
  display.println(F("Conectando WiFi..."));
  display.display();

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi conectado. IP: ");
  Serial.println(WiFi.localIP());
  
  // Configurar el servidor MQTT
  mqtt.setServer(MQTT_BROKER, MQTT_PORT);

  // --- CALIBRACIÓN ORIGINAL DEL MPU6050 ---
  display.clearDisplay();
  display.setCursor(0, 10);
  display.println(F("Calibrando..."));
  display.display();

  delay(2000);

  float sumaX = 0, sumaY = 0, sumaZ = 0;
  int muestras = 100;
  for (int i = 0; i < muestras; i++) {
    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);
    sumaX += a.acceleration.x;
    sumaY += a.acceleration.y;
    sumaZ += a.acceleration.z;
    delay(10);
  }
  offsetX = sumaX / muestras;
  offsetY = sumaY / muestras;
  offsetZ = (sumaZ / muestras) - 9.81;

  display.clearDisplay();
  display.setCursor(0, 20);
  display.setTextSize(2);
  display.println(F("Listo!"));
  display.display();
  delay(1000);

  dibujarMonitoreo();
}

void dibujarMonitoreo() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(10, 25);
  display.println(F("ACTIVO"));
  display.display();
}

void loop() {
  unsigned long tiempoActual = millis();

  // Mantener viva la conexión MQTT (Reconexión no bloqueante)[cite: 1]
  if (WiFi.status() == WL_CONNECTED) {
    if (!mqtt.connected()) {
      static unsigned long ultimoIntento = 0;
      if (tiempoActual - ultimoIntento > 5000) {
        reconnectMQTT();
        ultimoIntento = tiempoActual;
      }
    } else {
      mqtt.loop();
    }
  }

  // Lógica original de lectura del sensor
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

  float ax = a.acceleration.x - offsetX;
  float ay = a.acceleration.y - offsetY;
  float az = a.acceleration.z - offsetZ;

  float magnitud = sqrt((ax * ax) + (ay * ay) + (az * az));

  // --- ÍTEM A3: PUBLICAR PAYLOAD JSON PLANO CON RETAINED MESSAGE ---
  if (mqtt.connected() && (tiempoActual - tiempoUltimaPublicacion >= INTERVALO_PUBLICACION_MQTT)) {
    tiempoUltimaPublicacion = tiempoActual;
    
    JsonDocument doc; 
    
    doc["aceleracion"] = magnitud;
    // IMPORTANTE: Convertir el booleano a 1 o 0 como exige la hoja
    doc["alerta"] = estadoAlerta ? 1 : 0; 
    
    char buf[256];
    serializeJson(doc, buf);
    
    // Publicamos usando la sobrecarga con retained = true[cite: 1]
    mqtt.publish(TOPIC_DATOS, (const uint8_t*)buf, strlen(buf), true); 
    
    Serial.print("JSON Publicado: ");
    Serial.println(buf);
  }

  // 1. REVISAR EL BOTÓN DE CANCELACIÓN
  if (estadoAlerta && digitalRead(PIN_BOTON) == HIGH) {
    estadoAlerta = false;
    digitalWrite(PIN_BUZZER, LOW);
    digitalWrite(PIN_LED, LOW);

    display.clearDisplay();
    display.setTextSize(2);
    display.setCursor(4, 15);
    display.println(F("EMERGENCIA"));
    display.setCursor(10, 35);
    display.println(F("CANCELADA"));
    display.display();

    mostrandoCancelacion = true;
    tiempoInicioCancelacion = tiempoActual;
    pantallaActualizada = true; 
  }

  // 2. CONTROLAR LA PANTALLA DE CANCELACIÓN
  if (mostrandoCancelacion) {
    if (tiempoActual - tiempoInicioCancelacion >= DURACION_CANCELACION) {
      mostrandoCancelacion = false;
      pantallaActualizada = false;
    }
  }

  // 3. FUNCIONAMIENTO NORMAL DEL SENSOR Y ALARMA
  else {
    if (magnitud > UMBRAL_ACELERACION && !estadoAlerta) {
      estadoAlerta = true;
      tiempoInicioAlerta = tiempoActual;
      
      digitalWrite(PIN_BUZZER, HIGH); 
      estadoLed = true; 
      digitalWrite(PIN_LED, HIGH); 
      tiempoUltimoParpadeo = tiempoActual; 
      
      display.clearDisplay();
      display.setTextSize(2);
      display.setCursor(15, 10);
      display.println(F("!CAIDA!"));
      display.setTextSize(1);
      display.setCursor(15, 40);
      display.print(F("Fuerza: ")); 
      display.print(magnitud, 1);
      display.display();
      
      pantallaActualizada = false; 
    }

    if (estadoAlerta) {
      if (tiempoActual - tiempoUltimoParpadeo >= INTERVALO_PARPADEO) {
        tiempoUltimoParpadeo = tiempoActual;
        estadoLed = !estadoLed; 
        
        if (estadoLed) {
          digitalWrite(PIN_LED, HIGH);
        } else {
          digitalWrite(PIN_LED, LOW);
        }
      }
      
      if (tiempoActual - tiempoInicioAlerta >= DURACION_ALERTA) {
        estadoAlerta = false;
        digitalWrite(PIN_BUZZER, LOW);
        digitalWrite(PIN_LED, LOW); 
        pantallaActualizada = false; 
      }
      
    } else {
      digitalWrite(PIN_LED, LOW); 
      
      if (!pantallaActualizada) {
        dibujarMonitoreo();
        pantallaActualizada = true;
      }
    }
  }

  delay(20);
}