#ifndef CONFIG_H
#define CONFIG_H

const char* WIFI_SSID = "TU_WIFI";
const char* WIFI_PASS = "TU_CLAVE";
const char* MQTT_BROKER = "IP_SERVIDOR"; 
const int   MQTT_PORT = 1883;
const char* MQTT_USER = "TU_EQUIPO"; 
const char* MQTT_PASS = "TU_CLAVE_SECRETA";
const char* MQTT_CLIENT_ID = "EQUIPO-nodoX"; 
const char* TOPIC_DATOS = "curso/EQUIPO/Pxx/nodo"; 
const char* TOPIC_ESTADO = "curso/EQUIPO/Pxx/nodo/estado"; 
const char* TOPIC_CMD = "curso/EQUIPO/Pxx/nodo/cmd"; 

#endif