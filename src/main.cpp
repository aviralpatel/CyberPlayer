#include <Arduino.h>
#include "driver/i2s.h"
#include <SD.h>
#include <FS.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <TFT_ILI9163C.h>
#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiAP.h>
#include <EEPROM.h>

TaskHandle_t Task1;
TaskHandle_t Task2;

// Pin definitions for the SD card
#define SD_CS_PIN 5

// I2S pins
#define I2S_BCLK_PIN 26
#define I2S_WS_PIN 25
#define I2S_DATA_PIN 22

// Color definitions
#define	BLACK   0x0000
#define	BLUE    0x001F
#define	RED     0xF800
#define	GREEN   0x07E0
#define CYAN    0x07FF
#define MAGENTA 0xF81F
#define YELLOW  0xFFE0  
#define WHITE   0xFFFF
#define UTIL    0x75DD

// TFT Pins
#define __TFTCS 16  //prev 16
#define __DC 17

// Interrupt Pins
const int nextPin = 33;
const int prevPin = 34;
const int playPin = 32;
//const int menuPin = 4;

const int ledPin = 27;
const int ledCount = 2;

File wavFile;

TFT_ILI9163C tft = TFT_ILI9163C(__TFTCS, __DC);

Adafruit_NeoPixel strip(ledCount, ledPin, NEO_GRB + NEO_KHZ800);

// Audio Playback Variables
const int volPin = 35;
int16_t buffer[512];
float volume = 0.1;
float volArr[10];
int rotation = 0;
long timeStamp;

// File System Variables
int capacity = 100;
String contents[100];
String renderContent[6];
String mainMenuContent[7];
int nFiles = 0;
String currentDir = "/";
int header = 0;
int songHeader = -1;
int playHeader = -1;
String wavPath;

// Flags
bool isInsideDir = false;
bool isPlaying = false;
bool toRender = true;
bool initFlag = true;
bool playInit = false;
int batton = true;
bool pressFlag = false;

unsigned long lastInterruptTime = 0;
unsigned long switchPressTime = 0;
const long debounceTime = 300;  // Debounce time in milliseconds

// WiFi credentials
const char *ssid = "esp32AP";
const char *password = "1234@4321";

WiFiServer server(80);

// EEPROM parameters

#define EEPROM_SIZE 7

uint8_t r1 = 0;
uint8_t r2 = 0;
uint8_t g1 = 0;
uint8_t g2 = 0;
uint8_t b1 = 0;
uint8_t b2 = 0;

bool debounce(){
    unsigned long currentInterruptTime = millis();
    if (currentInterruptTime - lastInterruptTime > debounceTime){
        lastInterruptTime = currentInterruptTime;
        return true;
    }
    return false;
}

// interrupt functions
void IRAM_ATTR incrementHeader(){
  if(debounce() && header < nFiles - 1){
    header = header + 1;
    toRender = true;
  }
}

void IRAM_ATTR decrementHeader(){
  if(debounce() && header > 0){
    header = header - 1;
    toRender = true;
  }
}

void IRAM_ATTR playPause(){
  if(!pressFlag){
    if(debounce()){
      pressFlag = true;
    }
  }
}


void setup() {
  Serial.begin(115200);
  Serial.println("Setup Started");
  
  // Initialize TFT
  tft.begin();
  tft.clearScreen(BLACK);
  tft.clearWriteError();
  tft.setRotation(2);
  tft.setTextSize(1);
  tft.setTextColor(YELLOW);
  tft.setCursor(0, 7);
  prints("starting up");

  // Initialize the SD card
  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("Card Mount Failed");
    return;
  }

  for(int i = 0; i < 10; i++){
    volArr[i] = volume;
  }

  // Configure the I2S peripheral
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = 44100,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_I2S),
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 128,
    .tx_desc_auto_clear = true,
  };

  // Install and start the I2S driver
  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  
  // Set up the pin configurations
  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_BCLK_PIN,
    .ws_io_num = I2S_WS_PIN,
    .data_out_num = I2S_DATA_PIN,
    .data_in_num = I2S_PIN_NO_CHANGE
  };
  
  i2s_set_pin(I2S_NUM_0, &pin_config);

  delay(100);

  pinMode(nextPin, INPUT_PULLUP);
  pinMode(prevPin, INPUT_PULLUP);
  pinMode(playPin, INPUT_PULLUP);
  //pinMode(menuPin, INPUT_PULLUP);
  delay(10);
  attachInterrupt(nextPin, incrementHeader, FALLING);
  attachInterrupt(prevPin, decrementHeader, FALLING);
  attachInterrupt(digitalPinToInterrupt(playPin), playPause, FALLING);

  WiFi.softAP(ssid, password);
  IPAddress myIP = WiFi.softAPIP(); //default ip 192.168.4.1
  Serial.print("AP IP address: ");
  Serial.println(myIP);
  server.begin();
  
  EEPROM.begin(EEPROM_SIZE);

  printColors();

  Serial.println("exiting setup");


  xTaskCreatePinnedToCore(
                    Task1code,   /* Task function. */
                    "Task1",     /* name of task. */
                    10000,       /* Stack size of task */
                    NULL,        /* parameter of the task */
                    1,           /* priority of the task */
                    &Task1,      /* Task handle to keep track of created task */
                    0);          /* pin task to core 0 */                  
  delay(500); 
}

// Loop
void Task1code( void * pvParameters ){
  
  for(;;){
    setVolume();
    backend();
    wifi_reader();
    if(millis() - lastInterruptTime > 200 && pressFlag){
      if(digitalRead(playPin) == 1){
        if(millis() - lastInterruptTime < 800){
          if(isInsideDir && !isPlaying){
          isPlaying = true;
          playInit = true;
          }
          else if(isPlaying){
          isPlaying = false;
          }
          toRender = true;
        }
        else{
          if(isInsideDir){
          toRender = true;
          isInsideDir = false;
          initFlag = true;
          isPlaying = false;
          }
          else{
            isInsideDir = true;
            toRender = true;
            initFlag = true;
          }
        }
        pressFlag = false;
      }
    }
    delay(10);
    rotation++;
  } 
}

// volume functions
void setVolume(){
  if(rotation >= 10)rotation = 0;
  int potVal = analogRead(volPin);
  int volumeint = map(potVal, 0, 4095, 0, 15);
  float volT = float(0.55/15)*float(volumeint);
  volArr[rotation] = volT;
  if(counts(volArr) >= 7){
    volume = volArr[0];
  }
}

void adjustVolume(int16_t *buffer, size_t samples) {
  for (size_t i = 0; i < samples; i++) {
    buffer[i] = (int16_t)(buffer[i] * volume);
  }
}

int counts(float arr[10]){
  float setVal = arr[0];
  int count = 0;
  for(int i = 0; i < 10; i++){
    if(arr[i] == setVal){
      count++;
    }
  }
  return count;
}

// display functions
void prints(String text){
  int16_t x, y;
  x = tft.getCursorX();
  y = tft.getCursorY();
  tft.setCursor(x + 7, y + 5);
  tft.println(text);
}

// file system functions
void listDir(String currentDir){
  nFiles = 0;
  File root = SD.open(currentDir);
  if(!root){
    Serial.println("Failed to open directory");
    return;
  }
  if(!root.isDirectory()){
    Serial.println("Not a directory");
    return;
  }

  File file = root.openNextFile();
  while(file){
    if(file.isDirectory() ){
      if(file.name()[0] != '.'){
        Serial.print("  DIR : ");
        Serial.println(file.name());
        contents[nFiles] = file.name();
        nFiles++;
      }
    } 
    else {
      if(file.name()[0] != '.'){
        Serial.print("  FILE: ");
        Serial.println(file.name());
        contents[nFiles] = file.name();
        nFiles++;
      }
    }
    file = root.openNextFile();
  }
}

void backend(){
  if(isInsideDir){
    if(initFlag){
      currentDir.concat(contents[header]);
      listDir(currentDir);
      header = 0;
      initFlag = false;
    }

    if(isPlaying && playInit && header != songHeader){
      songHeader = header;
      playHeader = header;
      wavPath = currentDir;
      wavPath.concat("/");
      wavPath.concat(contents[header]);
      //Serial.println(wavPath);
      batton = false;
      wavFile = SD.open(wavPath);
      wavFile.seek(44);
      batton = true;
      Serial.println(isPlaying);
      Serial.println(batton);
      Serial.print("backend core - ");
      Serial.println(xPortGetCoreID());
      playInit = false;
    }

    if(header <  6){
      if(nFiles <= 6){
        for(int i = 0; i < nFiles; i++){
          renderContent[i] = contents[i];
        }
      }
      else{
        for(int i = 0; i < 6; i++){
          renderContent[i] = contents[i];
        }
      }
    }
    else{
      int j = 0;
      for(int i = header - 5; i <= header; i++){
        renderContent[j] = contents[i];
        j++;
      }
    }

    if(toRender){
      render();
    }
  }
  else{
    if(initFlag){
        currentDir = "/";
        listDir(currentDir);
        header = 0;
        initFlag = false;
    }

    if(header <  7){
      if(nFiles <= 7){
        for(int i = 0; i < nFiles; i++){
          mainMenuContent[i] = contents[i];
        }
      }
      else{
        for(int i = 0; i < 7; i++){
          mainMenuContent[i] = contents[i];
        }
      }
    }
    else{
      int j = 0;
      for(int i = header - 6; i <= header; i++){
        mainMenuContent[j] = contents[i];
        j++;
      }
    }

    if(toRender){
      render();
    }
  }

}

void render(){
  String playlist;
  if(currentDir == "/")playlist = "Main Menu";
  else{
    playlist = currentDir.substring(1);
  }
  batton = false;
  tft.clearScreen();
  tft.setTextColor(WHITE);
  tft.setCursor(1, 7);
  tft.print("|");
  tft.setCursor(tft.getCursorX() + 2, tft.getCursorY());
  tft.println(playlist);
  tft.setCursor(114, 7);
  tft.print(":)");
  if(isInsideDir){
    tft.setCursor(0, 21);
    if(wavFile){
      if(isPlaying){
        tft.setTextColor(GREEN);
      }
      else{
        tft.setTextColor(MAGENTA);
      }
      String currentSong = wavFile.name();
      int dotIndex = currentSong.indexOf(".");
      currentSong = currentSong.substring(0, dotIndex);
      prints(currentSong);
    }
    tft.setCursor(0, 42);
    tft.setTextColor(YELLOW);
    tft.setTextSize(1);
    if(nFiles <= 6){
      for(int i = 0; i < nFiles; i++){
        if(i == header){
          tft.setTextColor(BLACK, YELLOW);
        }
        else{
          tft.setTextColor(YELLOW);
        }
        String song = renderContent[i];
        int endIndex = song.indexOf(".");
        song = song.substring(0, endIndex);
        prints(song);
      }
    }
    else{
      for(int i = 0; i < 6; i++){
        if(header < 6){
          if(i == header){
            tft.setTextColor(BLACK, YELLOW);
          }
          else{
            tft.setTextColor(YELLOW);
          }
          String song = renderContent[i];
          int endIndex = song.indexOf(".");
          song = song.substring(0, endIndex);
          prints(song);
        }
        else{
          if(i < 5){
              tft.setTextColor(YELLOW);
            }
          else{
            tft.setTextColor(BLACK, YELLOW);
          }
          String song = renderContent[i];
          int endIndex = song.indexOf(".");
          song = song.substring(0, endIndex);
          prints(song);
        }
      }
    }
  }
  else{
    tft.setCursor(0, 21);
    if(nFiles <= 7){
      for(int i = 0; i < nFiles; i++){
        if(i == header){
          tft.setTextColor(BLACK, YELLOW);
        }
        else{
          tft.setTextColor(YELLOW);
        }
        prints(mainMenuContent[i]);
      }
    }
    else{
     if(header < 7){
      for(int i = 0; i < 7; i++){
        if(i == header){
          tft.setTextColor(BLACK, YELLOW);
        }
        else{
          tft.setTextColor(YELLOW);
        }
        prints(mainMenuContent[i]);
      }
     }
     else{
      for(int i = 0; i < 7; i++){
        if(i == 6){
          tft.setTextColor(BLACK, YELLOW);
        }
        else{
          tft.setTextColor(YELLOW);
        }
        prints(mainMenuContent[i]);
      }
     }
    }
  }
  batton = true;
  toRender = false;
}

void wifi_reader(){
  WiFiClient client = server.available();
  if(client){
    Serial.println("new client");
    String currentLine = "";
    while(client.connected()){
      if(client.available()){
        char c = client.read();
        Serial.write(c);
        if(c == '\n'){
          if(currentLine.length() == 0){
            client.println("HTTP/1.1 200 OK");
            client.println("Content-type:text/html");
            client.println("Connection: close");
            client.println();

            // the content of the HTTP response follows the header:
            client.println("<!DOCTYPE html><html>");
            client.println("<head><title>ESP32 Input Form</title></head>");
            client.println("<body><h1>Enter values</h1>");
            client.println("<form action=\"/submit\" method=\"GET\">");
            for (int i = 1; i <= 6; i++) {
              client.println("Value " + String(i) + ": <input type=\"number\" name=\"value" + String(i) + "\" min=\"0\" max=\"255\"><br>");
            }
            client.println("<input type=\"submit\" value=\"Submit\"></form>");
            client.println("</body></html>");

            // The HTTP response ends with another blank line:
            client.println();
            break;
          }
          else{
            currentLine = "";
          }
        }
        else if(c != '\r'){
          currentLine += c;
        }
        
        Serial.println(currentLine);
        setColors(currentLine);
        
      }
    }
    client.stop();
    Serial.println("Client Disconnected.");
  }
}

void setColors(String currentLine){
  if(currentLine.startsWith("GET") && currentLine.endsWith("HTTP/1.1") && currentLine.length() > 72){
    int Ival1 = currentLine.indexOf("=");
    int Eval1 = currentLine.indexOf("&");
    uint8_t val1 = currentLine.substring(Ival1 + 1, Eval1).toInt();
    
    int Ival2 = currentLine.indexOf("=", Ival1 + 1);
    int Eval2 = currentLine.indexOf("&", Eval1 + 1);
    uint8_t val2 = currentLine.substring(Ival2 + 1, Eval2).toInt();

    int Ival3 = currentLine.indexOf("=", Ival2 + 1);
    int Eval3 = currentLine.indexOf("&", Eval2 + 1);
    uint8_t val3 = currentLine.substring(Ival3 + 1, Eval3).toInt();

    int Ival4 = currentLine.indexOf("=", Ival3 + 1);
    int Eval4 = currentLine.indexOf("&", Eval3 + 1);
    uint8_t val4 = currentLine.substring(Ival4 + 1, Eval4).toInt();

    int Ival5 = currentLine.indexOf("=", Ival4 + 1);
    int Eval5 = currentLine.indexOf("&", Eval4 + 1);
    uint8_t val5 = currentLine.substring(Ival5 + 1, Eval5).toInt();

    int Ival6 = currentLine.indexOf("=", Ival5 + 1);
    int Eval6 = currentLine.indexOf("&", Eval5 + 1);
    uint8_t val6 = currentLine.substring(Ival6 + 1, Eval6).toInt();

    Serial.println(val1);
    Serial.println(val2);
    Serial.println(val3);
    Serial.println(val4);
    Serial.println(val5);
    Serial.println(val6);
    EEPROM.write(0, val1);
    delay(10);
    EEPROM.write(1, val2);
    delay(10);
    EEPROM.write(2, val3);
    delay(10);
    EEPROM.write(3, val4);
    delay(10);
    EEPROM.write(4, val5);
    delay(10);
    EEPROM.write(5, val6);
    delay(10);
    EEPROM.commit();
  }
}

void printColors(){
  r1 = EEPROM.read(0);
  delay(10);
  g1 = EEPROM.read(1);
  delay(10);
  b1 = EEPROM.read(2);
  delay(10);
  r2 = EEPROM.read(3);
  delay(10);
  g2 = EEPROM.read(4);
  delay(10);
  b2 = EEPROM.read(5);
  delay(10);

  strip.begin();
  delay(10);
  strip.clear();
  strip.setBrightness(20);
  strip.setPixelColor(0, r1, g1, b1); // (index, R, G, B)
  strip.setPixelColor(1, r2, g2, b2); // (index, R, G, B)
  strip.show();

}

void loop() {
  if(isPlaying && batton && wavFile){
      // Read audio data from the file
    size_t bytesRead = wavFile.read((uint8_t *)buffer, sizeof(buffer));
    // If there's audio data available
    if (bytesRead > 0) {
      // Write the audio data to the I2S
      adjustVolume(buffer, bytesRead/2);
      size_t bytesWritten;
      i2s_write(I2S_NUM_0, buffer, bytesRead, &bytesWritten, portMAX_DELAY);
      timeStamp = millis();
    } 
    else if(millis() - timeStamp > 500) {
      // If we've reached the end of the file, loop back to the start
      if(playHeader < nFiles - 1){
        header = playHeader + 1;
        delay(250);
      }
      else{
        header = 0;
        delay(250);
      }

      batton = false;
      delay(250);
      playInit = true;
      toRender = true;
    }
  }
}

