#include <WiFi.h>
#include <PubSubClient.h>
#include <SPI.h>
#include <Adafruit_BMP280.h>
#include <hp_BH1750.h> 
#include <RTClib.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "ArduinoJson.h"

// Char array utilizzato per l'invio dei dati come pachetto unico.
char payload[100];
//Definizione globali per la descrizione delle parti del payload.
#define VARIABLE_temperatura "temperatura"
#define VARIABLE_pressione "pressione"
#define VARIABLE_luminosita "luminosità"
#define VARIABLE_timestamp "timestamp"


//Variabile utilizzato per un controllo che serve nella dashboard.
bool stato=false;
//Variabili per la connessione WiFi.
const char* ssid = "You SSID";
const char* password = "password";



// Inserire il proprio indirizzo MQTT.
const char* mqtt_server = "your mqtt server";


// Dichiarazione di un'istanza della classe WifiClient e PubSubClient, utilizzati per stabilire la comunicazione MQTT con un server.
WiFiClient espClient;
PubSubClient client(espClient);
// Dichiarazione variabile di tipo long, inizializzata a 0 per poi essere utilizzata per memorizzare un timestamp.
long lastMsg = 0;
// Dichiarazione di un char array di dimensione 50
char msg[50];

// Utillizzo delle diverse librerie per interfacciarsi con i diversi sensori e per la creazione degli oggetti dei diversi sensori.
// Si utilizza I2c come protocollo di comunicazione.
RTC_PCF8523 rtc;
hp_BH1750 BH1750;
Adafruit_BMP280 bmp;
Adafruit_Sensor *bmp_temp = bmp.getTemperatureSensor();
Adafruit_Sensor *bmp_pressure = bmp.getPressureSensor();

//Definizione variabili globali.

float temperatura,pressione,lux;
String tempo;

// Dichiarazioni dei task.
void task1_misure(void *param);
void task2_invio_dati(void *param);

void setup() {
  Serial.begin(9600);

  // Inizializzazione del modulo RTC.
  rtc.begin();

 

  bmp.begin(0x77);

    /* Default settings from the datasheet. */
  bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,     /* Operating Mode. */
                  Adafruit_BMP280::SAMPLING_X2,     /* Temp. oversampling */
                  Adafruit_BMP280::SAMPLING_X16,    /* Pressure oversampling */
                  Adafruit_BMP280::FILTER_X16,      /* Filtering. */
                  Adafruit_BMP280::STANDBY_MS_500); /* Standby time. */
  // inizializzazione del sensore di illuminazione BH1750 attraverso indirizzo I2C del sensore  
  BH1750.begin(0x23);
  
  // Avvio del sensore di luminosità.
  BH1750.start();
   //Configurazione della connessione Wifi ed impostazione del server MQTT a cui il cliente si deve connettere all'indirizzo 1883
  setup_wifi();
  client.setServer(mqtt_server, 1883);
  // Funzione di callback che verrà chiamata quando il client MQTT riceve un messaggio dal server.
  client.setCallback(callback);
  //Avvio del modulo RTC.
  rtc.start();
  //Task
  xTaskCreate(task1_misure,"Task delle misure",5000,NULL,2,NULL);
  xTaskCreate(task2_invio_dati,"Task stampa e invio dati",5000,NULL,1,NULL);
 
}
// Task utilizzato per l'acquisizione delle misure tramite i sensori.
void task1_misure(void *param){
  sensors_event_t temp_event, pressure_event;
  
  TickType_t xLastWakeTime;
  const TickType_t xFrequency = pdMS_TO_TICKS(600);
  // Inizializzazione della variabile xLastWakeTime con il tempo attuale.
  xLastWakeTime = xTaskGetTickCount();
  
   while(true){
      //Lettura dell'ora e data
      DateTime adesso = rtc.now();
      tempo=adesso.timestamp(DateTime::TIMESTAMP_FULL);
      // Lettura della temperatura
      bmp_temp->getEvent(&temp_event);
      temperatura=temp_event.temperature;
      // Lettura della pressione 
      bmp_pressure->getEvent(&pressure_event);
      pressione=pressure_event.pressure;
      // Lettura della luminosità
      lux=BH1750.getLux();
      BH1750.start();
      // Configurazione nuova chiamata del task
     
     vTaskDelayUntil( &xLastWakeTime, xFrequency );
  }
}
// Task utilizzato per l'invio dei dati al broker MQTT.
void task2_invio_dati(void *param){
  
  TickType_t xLastWakeTime;
  const TickType_t xFrequency = pdMS_TO_TICKS(5000);
  // Inizializzazione della variabile xLastWakeTime con il tempo attuale.
  xLastWakeTime = xTaskGetTickCount();
  while(true){
    // Controllo utilizzato per la connessione al WiFi
    if (!client.connected()) {
         reconnect();
  }
  
  client.loop();
  // Controllo per vedere se lo switch è on/off
  if(stato==true){
     long now = millis();
     // Controllo per invidare dati ogni 5 secondi
     if (now - lastMsg > 5000) {
       lastMsg = now;
       // Conversione della data e dell'ora in un char array
       Serial.println("Stampa del tempo:");
       Serial.println(tempo);
       char tempoString[20];
       tempo.toCharArray(tempoString, 20);
       // Utilizzato per pubblicare il valore della data e dell'ora in uno spazio specifico della dashboard
       client.publish("data", tempoString);
       // Conversione del valore della temperatura in un  char array
       char tempString[8];
       dtostrf(temperatura, 1, 2, tempString);
       Serial.print("Temperatura: ");
       Serial.println(tempString);
       // Conversione del valore pressione in un char array.
       char preString[8];
       dtostrf(pressione, 1, 2, preString);
       Serial.print("Pressione: ");
       Serial.println(preString);
       // Conversione del valore luminosità in un char array.
       char lucString[8];
       dtostrf(lux, 1, 2, lucString);
       Serial.print("Luminosità: ");
       Serial.println(lucString);
       Serial.println();
       // Pacchetto di dati costruito aggiungendo i vari char array che verrà pubblicato utilizzando il client MQTT attraverso il payload.
       sprintf(payload, "\"%s\"_%s,",VARIABLE_temperatura, tempoString );
       sprintf(payload, "%s \"%s\"_%s,", payload,VARIABLE_temperatura, tempString); 
       sprintf(payload, "%s \"%s\"_%s,", payload,VARIABLE_pressione, preString);
       sprintf(payload, "%s \"%s\"_%s", payload, VARIABLE_luminosita, lucString); 
       client.publish("dati", payload);
       //Messaggio "I dati vengono trasmessi" sul topic MQTT chiamato "on/off" utilizzando il client MQTT.
       client.publish("on/off","I dati vengono trasmessi");
       //Introduzione di un ritardo di 5K millesecondi nel task.
       vTaskDelayUntil( &xLastWakeTime, xFrequency );
    }
  }
  else if(stato==false){
       client.publish("on/off","I dati non vengono trasmessi");
    }

}}

//Funzione usata per connettersi a una rete Wi-Fi specificata tramite SSID e password.
void setup_wifi() {
  delay(10);
  // Si inizia provando una connessione nella rete WiFi.
  Serial.println();
  Serial.print("Connessione a ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
       // Funzione utilizzata per la riconnessione alla rete WiFi in caso di disconnessione
       WiFi.reconnect();
       Serial.println("Riconnessione alla rete WiFi ...");
       delay(500);
  }

  Serial.println("");
  Serial.println("WiFi connesso");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}
//Funzione di callback che viene richiamata quando il client MQTT riceve un messaggio su un determinato topic. 
void callback(char* topic, byte* message, unsigned int length) {
  Serial.print("Messaggio arrivato nel topic: ");
  Serial.print(topic);
  Serial.print(". Message: ");
  String messageTemp;
  
  for (int i = 0; i < length; i++) {
    Serial.print((char)message[i]);
    messageTemp += (char)message[i];
  }
  Serial.println();
  // Se un messaggio è ricevuto nel topic esp32/output, si controlla se il messaggio è "on" o "off".
  // Si cambia lo stato dell'output a seconda del messaggio.
  if (String(topic) == "esp32/output") {

    if(messageTemp == "on"){
      stato=true;
    }
    else if(messageTemp == "off"){
      stato=false;
    }
  }

  // Parte della funzione di callback utilizzata per sincronizzare il modulo rtc con un pulsante che genera il timestamp da nodered.
  // Charr array per salvare il timestamp ricevuto da nodered.
  char sensorertc[300];
  // Variabili usaper per salvare anno,mese,giorno,ora,minuti,secondi.
  int anno,mese,giorno,ora,minuti,secondi;
  StaticJsonDocument<300> doc;
  if (strcmp(topic,"esp32/rtc")==0){
  // Stampa a seriale se il topic è esp32/rtc il tempo come formato file json.
  Serial.print("Message arrived in topic: ");
  Serial.println(topic);
  Serial.print("Message:");
  
  for (int i = 0; i < length; i++)
   {sensorertc[i]=message[i];
  }
  Serial.print(sensorertc);
  // Riga di codice uitilizzato per deserializzare il file json in modo da ricavare anno,mese,giorno,ora,minuti,secondi.
  DeserializationError error = deserializeJson(doc, sensorertc);
   if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.f_str());
    return;
  }
  //Estrazione del time stamp
  anno = doc["year"];
  mese = doc["month"];
  giorno = doc["day"];
  ora = doc["hours"];
  minuti = doc["mins"];
  secondi = doc["secs"];
  //Settagio del timestamp del rtc.
  rtc.adjust(DateTime(anno, mese, giorno, ora, minuti, secondi));
  
}
}

//Funzione viene utilizzata per riconnettersi al broker MQTT se la connessione viene persa. 

void reconnect() {
  // Ciclo finchè la riconnessione ha successo
  while (!client.connected()) {
    Serial.print("Tentativo di una connessione MQTT...");
    // Tentativo di connessione
    if (client.connect("ESP32Client")) {
      Serial.println("Connesso");
      // Subscribe
      client.subscribe("esp32/output");
      client.subscribe("esp32/rtc");
    } else {
      Serial.print("fallito, rc=");
      Serial.print(client.state());
      Serial.println("prova ancora tra 5 secondi");
      // Aspetta 5 secondi prima di riprovare
      delay(5000);
    }
  }
}

void loop() {
     // Non si scrive niente dato che viene fatto tutto dai task.
}
