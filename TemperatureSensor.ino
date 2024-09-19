#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ESP_Mail_Client.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include "LittleFS.h"
#include <Adafruit_AHTX0.h>
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/timers.h>

//FreeRTOS
TimerHandle_t tempTimer;  // Timer handle
const char* MAC="c8:f0:9e:4e:2b:74";

// TempSensor
#define I2C_SDA 21
#define I2C_SCL 22
Adafruit_AHTX0 aht;  // Create AHTX0 object
volatile bool wifiOn=false;
volatile bool first=true;
volatile bool calculated=false;
float temperature = 0.0;  // Store the latest temperature
float temp[3]={0.0,0.0,0.0};
float maxTemp;
int indexx=0;
String ipString;
String hostnameString;
String serverName = "TERMNALDATA";// Adjust here

// Get Data from WiFi
AsyncWebServer server(80);
const char* PARAM_INPUT_1 = "ssid";
const char* PARAM_INPUT_2 = "pass";
const char* PARAM_INPUT_3 = "mail";
const char* PARAM_INPUT_4 = "tempAlarm";
String ssid;
String pass;
String mail;
String tempAlarm;
String ip;
String Message;
const char* ssidPath = "/ssid.txt";
const char* passPath = "/pass.txt";
const char* mailPath = "/mail.txt";
const char* tempAlarmPath = "/tempAlarm.txt";
const char* ipPath = "/ip.txt";
IPAddress localIP;
unsigned long previousMillis = 0;
const long interval = 60000;

//Send Email
SMTPSession smtp;
Session_Config config;
HTTPClient http1; // For INIT and Production signal
HTTPClient http0; // For Alive signal

// Function to read temperature
void readTemperatureAndHumidity(int i) { //No humidity usage
  sensors_event_t humidity_event, temp_event;
  aht.getEvent(&humidity_event, &temp_event);
  temp[i]=temp_event.temperature; // Update global temperature
  Serial.printf("%d 'th datas temp is %f\n",i,temp[i]);
}

// Timer callback function
void IRAM_ATTR takeTemp(TimerHandle_t xTimer) {
  readTemperatureAndHumidity(indexx);
  Serial.printf("Index of current temp is %d \n",indexx);
  indexx++;
  if(indexx==3){
    indexx=0;
  }
}

void tempTask(void *pvParameters) {
  while (1) {
    vTaskDelay(910000 / portTICK_PERIOD_MS);  // 15 minutes and 10 sec
    temperature=(temp[2]*3+temp[1]*2+temp[0])/6;
    temp[0]=0.0;
    temp[1]=0.0;
    temp[2]=0.0;
    calculated=true;
    Serial.println(temperature);
  }
}
void keepAliveTask(void *pvParameters) {
  while (1) {
    vTaskDelay(900000 / portTICK_PERIOD_MS);  // 16 minutes
    KeepAlive();
  }
}

void setup() {
  Serial.begin(115200);

  Serial.println("Trying to init LittleFS");
  while (!LittleFS.begin(true)) {
    Serial.println(".");
    delay(500);
  }
  Serial.println("LittleFS mounted successfully");

  Wire.begin(I2C_SDA, I2C_SCL);

  Serial.println("Trying to init AHT10!");
  while (!aht.begin()) {
    Serial.println(".");
    delay(500);
  }
   Serial.println("AHT10 init successfully");

  // Load values saved in LittleFS
  ssid = readFile(LittleFS, ssidPath);
  pass = readFile(LittleFS, passPath);
  mail= readFile(LittleFS, mailPath);
  tempAlarm= readFile(LittleFS, tempAlarmPath);

  if(initWiFi()) {
    Serial.println("Connected the WiFi Successfully!");
    int respond= SendMail("Sender Gmail","gzxertwaoxqvhnbi",mail,"smtp.gmail.com","Initialization",Message); //Adjust Here
    tempTimer = xTimerCreate("TempTimer", pdMS_TO_TICKS(300000), pdTRUE, (void *)0, takeTemp); // take temp for each 5min
    if (tempTimer != NULL) {
      xTimerStart(tempTimer, 0);  // Start the timer (every 300,000 ms or 5 minutes)
    }
    const char* hostname = WiFi.getHostname();
    hostnameString = String(hostname);
    maxTemp=tempAlarm.toFloat(); // 10C to 100C
    wifiOn=true;
  }
  else {

    // Connect to Wi-Fi network with SSID and password
    Serial.println("Setting AP (Access Point)");
    // NULL sets an open Access Point
    WiFi.softAP("WIFI_MANAGER", NULL);

    IPAddress IP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(IP);

    // Web Server Root URL
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
      request->send(LittleFS, "/wifimanager.html", "text/html");
    });
    
    server.serveStatic("/", LittleFS, "/");
    
    server.on("/", HTTP_POST, [](AsyncWebServerRequest *request) {
      int params = request->params();
      for(int i=0;i<params;i++){
        const AsyncWebParameter* p = request->getParam(i);
        if(p->isPost()){
          // HTTP POST ssid value
          if (p->name() == PARAM_INPUT_1) {
            ssid = p->value().c_str();
            Serial.print("SSID set to: ");
            Serial.println(ssid);
            // Write file to save value
            writeFile(LittleFS, ssidPath, ssid.c_str());
          }
          // HTTP POST pass value
          if (p->name() == PARAM_INPUT_2) {
            pass = p->value().c_str();
            Serial.print("Password set to: ");
            Serial.println(pass);
            // Write file to save value
            writeFile(LittleFS, passPath, pass.c_str());
          }
          if (p->name() == PARAM_INPUT_3) {
            mail = p->value().c_str();
            Serial.print("Mail set to: ");
            Serial.println(mail);
            // Write file to save value
            writeFile(LittleFS, mailPath, mail.c_str());
          }
          if (p->name() == PARAM_INPUT_4) {
            tempAlarm = p->value().c_str();
            Serial.print("Temp Alarm set to: ");
            Serial.println(tempAlarm);
            // Write file to save value
            writeFile(LittleFS, tempAlarmPath, tempAlarm.c_str());
          }
          //Serial.printf("POST[%s]: %s\n", p->name().c_str(), p->value().c_str());
        }
      }
      request->send(200, "text/plain", "Done. ESP will restart within 5 seconds. Please check your E-mail adress");
      delay(5000);
      ESP.restart();
    });
    server.begin();
  }
  // Create the FreeRTOS task to handle temperature data processing
  xTaskCreate(tempTask, "TempTask", 1024, NULL, 0, NULL);
  xTaskCreate(keepAliveTask, "KeepAliveTask", 1024, NULL, 1, NULL);
}



void loop() {
  if(calculated){
    String serverPath = serverName + "?parametre1=" + String(MAC) + "&" + "parametre2=" + String(temperature) + "&" + "parametre3=" + "0"+ "&" + "parametre4=" + "0" + "&" + "parametre5=" + "U" + "&" + "IPAddr=" + ipString + "&" + "parametrehost=" + hostnameString;
    if(temperature>maxTemp){
      String mess="Current temperature (" + String(temperature) +") is higher your Max Temperature(" + String(maxTemp) + ") setup!";
      int respond= SendMail("Sender Gmail","gzxertwaoxqvhnbi",mail,"smtp.gmail.com","Warning Over Heat",mess);// Adjust Here
    }
    int respond= SendData(serverPath,"Core0");
    calculated=false;
  }

}

int SendMail(String Sender,String Pass,String Receiver,String Hostname,String Subject,String Message){

  Serial.println("Trying to Send Mail!");

  config.server.host_name =Hostname; // for outlook.com
  config.server.port = 587; // for TLS with STARTTLS or 25 (Plain/TLS with STARTTLS) or 465 (SSL)
  config.login.email = Sender; // set to empty for no SMTP Authentication
  config.login.password =Pass; // set to empty for no SMTP Authentication
  config.time.ntp_server = "time.nist.gov";
  config.time.gmt_offset = 3; // For turkey
  config.time.day_light_offset = 0;

  SMTP_Message message;
  message.sender.name = "SICAKLIK SENSORU";
  message.sender.email = Sender;
  message.subject = Subject;
  message.text.content = Message;
  message.addRecipient("name1", Receiver);

  //smtp.debug(1);
  smtp.connect(&config);

  if (!MailClient.sendMail(&smtp, &message)){
    Serial.println("Error sending Email, " + smtp.errorReason());
    return 400;
  }else{
    Serial.println("Mail Sended Successfully!");
    return 200;
  }
}

// Initialize WiFi
bool initWiFi() {

  if(ssid==""){
    Serial.println("Undefined SSID");
    return false;
  }

  WiFi.begin(ssid.c_str(), pass.c_str());
  Serial.println("Connecting to WiFi...");

  unsigned long currentMillis = millis();
  previousMillis = currentMillis;

  while(WiFi.status() != WL_CONNECTED) {
    currentMillis = millis();
    if (currentMillis - previousMillis >= interval) {
      Serial.println("Failed to connect.");
      return false;
    }
  }
  localIP=WiFi.localIP();
  ipString=localIP.toString();
  Serial.println(WiFi.localIP());
  Message="SSID : " + ssid + "\n" + "PASS : " + pass + "\n" + "Temperature Alarm(Max) : "+ tempAlarm + "\n" + "IP Address : " +ipString;
  return true;
}

// Read File from LittleFS
String readFile(fs::FS &fs, const char * path){
  Serial.printf("Reading file: %s\r\n", path);

  File file = fs.open(path);
  if(!file || file.isDirectory()){
    Serial.println("- failed to open file for reading");
    return String();
  }
  
  String fileContent;
  while(file.available()){
    fileContent = file.readStringUntil('\n');
    break;     
  }
  return fileContent;
}

// Write file to LittleFS
void writeFile(fs::FS &fs, const char * path, const char * message){
  Serial.printf("Writing file: %s\r\n", path);

  File file = fs.open(path, FILE_WRITE);
  if(!file){
    Serial.println("- failed to open file for writing");
    return;
  }
  if(file.print(message)){
    Serial.println("- file written");
  } else {
    Serial.println("- write failed");
  }
}

void KeepAlive(){  // Needs to adjust for other core
  int deneme=0;
  String serverPath = serverName + "?parametre1=" + String(MAC) + "&" + "parametre2=" + "0" + "&" + "parametre3=" + "0" + "&" + "parametre4=" + "0" + "&" + "parametre5=" + "info" + "&" + "IPAddr=" + ipString + "&" + "parametrehost=" + hostnameString;
  int httpResponseCodeA = SendData(serverPath,"Core1");

  while(httpResponseCodeA!=200&&deneme!=5){
    httpResponseCodeA=SendData(serverPath,"Core1");
    if(httpResponseCodeA==200){
      break;
    }
    deneme++;
    delay(1000);
  }
  if(httpResponseCodeA==200){
    Serial.println("Machine is Alive!");
  }
}

int SendData(String path,String Core){ // Send Get function from Specific Core
  if(Core.equals("Core0")){
    http0.end();
    http0.begin(path.c_str());
    int respond=http0.GET();
    if(respond==200){
      Serial.println("Data Sended Successfully!(0)");
      http0.end();
      return respond;
    }else{
      http0.end();
      Serial.println("Data Could Not Sended!(0)");
      return respond;
    }
  }
  if(Core.equals("Core1")){
    http1.end();
    http1.begin(path.c_str());
    int respond=http1.GET();
    if(respond==200){
      Serial.println("Data Sended Successfully!(1)");
      http1.end();
      return respond;
    }else{
      http1.end();
      Serial.println("Data Could Not Sended!(1)");
      return respond;
    } 
  }
}

