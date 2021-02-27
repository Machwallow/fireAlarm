//PACKAGES
#include "Zanshin_BME680.h"  // Include the BME680 Sensor library
#include <Arduino.h>
#include <Tone32.h>
#include <ChainableLED.h>
#include <FirebaseESP32.h>
#include <WiFi.h>

// -- GLOBAL VARIABLES -- 
// ----------------------
//RECUPERER L'ADDRESSE MAC POUR ID CONNEXION

#define BUZZER_PIN 16
#define BUZZER_CHANNEL 0
#define NUM_LEDS  2
//WIFI
#define WIFI_SSID "NoWifi4U"
#define WIFI_PASSWORD "Kalu123AUPOIL"
#define FIREBASE_HOST "https://iot-alarm-9b18a-default-rtdb.firebaseio.com/" // A MODIF
#define FIREBASE_AUTH "lCfC9owbo5LCQqNF2CH2QjLYBGhCVfnrwXxBGHAB" // A MODIF
FirebaseData fbdo;

const uint32_t SERIAL_SPEED{115200};  ///< Set the baud rate for Serial I/O

//LEDS 
byte pos = 0;
ChainableLED leds(26, 25, NUM_LEDS);
//capteur and sempahores
BME680_Class BME680;  ///< Create an instance of the BME680 class
hw_timer_t * timer_sensor = NULL; 
hw_timer_t * timer_alarm = NULL;
SemaphoreHandle_t sem_alarm = NULL;
SemaphoreHandle_t read_capteur = NULL;
SemaphoreHandle_t access_sensor_values = NULL;
SemaphoreHandle_t access_tempThreshold = NULL;
//variables from sensors
double temperature = 0.0;
double humidity = 0.0;
double pressure = 0.0;
double airM = 0.0;
//control variables
double temperatureThreshold = 25.0;
bool recordTemp = false;
bool alarmTriggered = false;
long lastAlarmMillis = 0;
long millisDelayBetweenNotif = 0;


// ----------------------

//Timer methods
void IRAM_ATTR activateAlarm(){
  float tempT = 0.0;
  double tempThreshold = 25.0; //default value just in case

  //access tempThreshold
  if(xSemaphoreTake(access_tempThreshold, (TickType_t) 100) == pdTRUE) {
      tempThreshold = temperatureThreshold;
      xSemaphoreGive(access_tempThreshold);
    } else {
      //cannot get the semaphore (shouldn't happen in any case)
    }

  
  //read and access temperature
  if(xSemaphoreTake(access_sensor_values, (TickType_t) 100) == pdTRUE) {
      tempT = temperature;
      xSemaphoreGive(access_sensor_values);
    } else {
      //cannot get the semaphore (shouldn't happen in any case)
    }
    
  if(tempT >= tempThreshold){
    if(xSemaphoreTake(sem_alarm, (TickType_t) 100) == pdTRUE) {
      alarmTriggered = true;
      Serial.println("--------------- ALARM ACTIVATED --------------");
      xSemaphoreGive(sem_alarm);
    } else {
      //cannot get the semaphore (shouldn't happen in any case)
    }
  }
}

void IRAM_ATTR activateReadCapteurs(){
 //read sensors values
  if(xSemaphoreTake(read_capteur, (TickType_t) 100) == pdTRUE) {
      recordTemp = true;
      xSemaphoreGive(read_capteur);
    } else {
      //cannot get the semaphore (shouldn't happen in any case)
    }
}

void activateSpeakerAndLEDS(){
  Serial.println("- SPEAKERS AND LEDS ON -");
  for(int i=0; i<7; i++){
    tone(BUZZER_PIN, NOTE_C4, 500, BUZZER_CHANNEL);
    leds.setColorRGB(1, 0, 0, 0);
    leds.setColorRGB(0, 255, 0, 0);  
    noTone(BUZZER_PIN, BUZZER_CHANNEL);
    tone(BUZZER_PIN, NOTE_D4, 500, BUZZER_CHANNEL);
    leds.setColorRGB(1, 255, 0, 0);
    leds.setColorRGB(0, 0, 0, 0); 
    noTone(BUZZER_PIN, BUZZER_CHANNEL);
  }
  leds.setColorRGB(1, 0, 0, 0);
  leds.setColorRGB(0, 0, 0, 0); 
}

void lectureCapteurs(void * pvParameters) {
  for(;;) {
    boolean tempReadSensor = false;
    double tempThreshold = 25.0; //default value just in case


    
    if(xSemaphoreTake(read_capteur, (TickType_t) 100) == pdTRUE) {
      tempReadSensor = recordTemp;
      recordTemp = false;
      xSemaphoreGive(read_capteur);
    } else {
      //cannot get the semaphore (shouldn't happen in any case)
    }

    if (tempReadSensor){
      int32_t  tempT, humidityT, pressureT, gasT;  // BME readings
      BME680.getSensorData(tempT, humidityT, pressureT, gasT);  // Get readings

      if(xSemaphoreTake(access_sensor_values, (TickType_t) 100) == pdTRUE) {
        temperature = ((double) tempT) / 100.0;
        humidity = ((double)humidityT) / 1000.0;
        pressure = ((double)pressureT) / 100.0;
        airM = ((double)gasT) / 100.0;
        xSemaphoreGive(access_sensor_values);
      } else {
      //cannot get the semaphore (shouldn't happen in any case)
      }

      //printing values
      Serial.print("Température : "); 
      Serial.println(((double) tempT) / 100.0);
      Serial.print("Humidity %: "); 
      Serial.println(((double)humidityT) / 1000);
      Serial.print("Pressure : "); 
      Serial.println(((double)pressureT) / 100);
      Serial.print("AirM : "); 
      Serial.println(((double)gasT) / 100);
      double temperatureNotif = ((double) tempT) / 100.0;
      double humidityNotif = ((double)humidityT) / 1000.0;
      double pressureNotif = ((double)pressureT) / 100.0;
      double airMNotif = ((double)gasT) / 100.0;
      
      //send infos to firebase
      String path = "/devices/" ;
      //air quality
      if(Firebase.setDouble(fbdo, path + WIFI_SSID + "/airquality", airMNotif)
        && Firebase.setDouble(fbdo, path + WIFI_SSID + "/temperature", temperatureNotif)
        && Firebase.setDouble(fbdo, path + WIFI_SSID + "/humidity", humidityNotif)
        && Firebase.setString(fbdo, path + WIFI_SSID + "/macadress", WiFi.macAddress())){
        //Success
        Serial.println("Update sensors values sent");
      }else{
        Serial.print("Error when sending sensors values: ");
        Serial.println(fbdo.errorReason());
      }

      path = "/devices/";
      if(Firebase.getDouble(fbdo, path + WIFI_SSID + "/temperaturelimit")){
        //success
        Serial.println("updated temperature threshold");
        tempThreshold = fbdo.doubleData();
      }else{
        //failure
        Serial.println("Could not get temperature limit, keeping default value");
        Serial.println(fbdo.errorReason());
      }

      if(xSemaphoreTake(access_tempThreshold, (TickType_t) 100) == pdTRUE) {
        temperatureThreshold = tempThreshold;
        xSemaphoreGive(access_tempThreshold);
      } else {
        //cannot get the semaphore (shouldn't happen in any case)
      }
      
    }
    
    vTaskDelay(5000);
  }
}



//SETUP EVERYTHING
void setup() {

  Serial.begin(SERIAL_SPEED);  // Start serial port at Baud rate

  //Timers and semaphores
  timer_sensor = timerBegin(0, 80, true); // because the quartz is set to 80MHz
  timerAttachInterrupt(timer_sensor, &activateReadCapteurs, true);
  timerAlarmWrite(timer_sensor, 1000000 * 10, true); //every 10secondes

  timer_alarm = timerBegin(1, 80, true);
  timerAttachInterrupt(timer_alarm, &activateAlarm, true);
  timerAlarmWrite(timer_alarm, 1000000 * 5, true);

  sem_alarm = xSemaphoreCreateMutex();
  read_capteur = xSemaphoreCreateMutex();
  access_sensor_values = xSemaphoreCreateMutex();
  access_tempThreshold = xSemaphoreCreateMutex();

  //WIFI
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print(".");
    delay(300);
  }
  Serial.println();
  Serial.print("Connected with IP: ");
  Serial.println(WiFi.localIP());
  Serial.println();

  //Set your Firebase info
  Firebase.begin(FIREBASE_HOST, FIREBASE_AUTH);

  //Enable auto reconnect the WiFi when connection lost
  Firebase.reconnectWiFi(true);

  //BME680

  while (!BME680.begin(I2C_STANDARD_MODE)) {  // Start BME680 using I2C, use first device found
    Serial.print(F("-  Unable to find BME680. Trying again in 5 seconds.\n"));
    delay(5000);
  }

  Serial.print(F("- Setting 16x oversampling for all sensors\n"));
  BME680.setOversampling(TemperatureSensor, Oversample16);  // Use enumerated type values
  BME680.setOversampling(HumiditySensor, Oversample16);     // Use enumerated type values
  BME680.setOversampling(PressureSensor, Oversample16);     // Use enumerated type values
  Serial.print(F("- Setting IIR filter to a value of 4 samples\n"));
  BME680.setIIRFilter(IIR4);  // Use enumerated type values
  Serial.print(F("- Setting gas measurement to 320\xC2\xB0\x43 for 150ms\n"));  // "�C" symbols
  BME680.setGas(320, 150);  // 320�c for 150 milliseconds
  delay(5000);

  //read one time our sensor values to initialize it correctly
  int32_t  tempT, humidityT, pressureT, gasT;  // BME readings
  BME680.getSensorData(tempT, humidityT, pressureT, gasT);  // Get readings

  timerAlarmEnable(timer_sensor);
  timerAlarmEnable(timer_alarm);

  xTaskCreatePinnedToCore(
                  lectureCapteurs,   // Task function. 
                  "lectureCapteurs",     // name of task. 
                  8000,       // Stack size of task 
                  NULL,        // parameter of the task 
                  5,           // priority of the task 
                  NULL,      // Task handle to keep track of created task 
                  0);
}


//MAIN LOOP
void loop() {
  // put your main code here, to run repeatedly:
  //if send_notif (avec accès secu = semaphore) true -> alarm sonore + leds + envoyez notif
  //check if our alarm needs to be activated

  long startLoop = millis();
  boolean tempAlarmTriggered = false;
  
  if(xSemaphoreTake(sem_alarm, (TickType_t) 100) == pdTRUE) {
      tempAlarmTriggered = alarmTriggered;
      alarmTriggered = false;
      xSemaphoreGive(sem_alarm);
    } else {
      //cannot get the semaphore (shouldn't happen in any case)
    }

  long now = millis();
  if(tempAlarmTriggered && ((now - millisDelayBetweenNotif) > lastAlarmMillis)){
    //ONLY FOR SEND NOTIF
    Serial.println("ALLLLLAAAAAAAAAAAAAAARM");
    //send a notif every minute as long as the alarm is triggered
    
    millisDelayBetweenNotif = 60000; //this way we're making sure we can activate our alarm in the first minute of activation
    lastAlarmMillis = millis(); 

    //get token push notif
    String path = "/devices/";
    String token = "";
    if(Firebase.getString(fbdo, path + WIFI_SSID + "/tokenPushNotif")){
      //success
      Serial.println("Found token push notif");
      token = fbdo.stringData();
    }else{
      //failure
      Serial.println("Could not get token push notif, set default value to 123123");
      token = "123123";
    }

    if(token == ""){
      token = "123123";
    }
    
    path = "/notify/";
    if(Firebase.setString(fbdo, path + WiFi.macAddress() + "/", token)){
      //Success
      Serial.println("Notif of alarm activation sent");
    }else{
      Serial.print("Error when setting alarm flag: ");
      Serial.println(fbdo.errorReason());
    }
  } else if(!tempAlarmTriggered) {
    //stop alarm only if flag is set to false
  } else if(tempAlarmTriggered){
    //START LEDS AND SPEAKER
    activateSpeakerAndLEDS();
  }

  long endLoop = millis();
  vTaskDelay(10000 - (endLoop-startLoop));
}
