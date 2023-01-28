#include <Adafruit_VS1053.h>
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <HTTPClient.h>

// Wiring of VS1053 board (SPI (VSPI) connected in a standard way)
#define VS1053_CS    4
#define VS1053_DCS   2
#define VS1053_DREQ  15
#define VS1053_RST   5

enum WebserverStateEnum {STATE_NEW, STATE_STATION, STATE_VOLUME, STATE_INFO, STATE_END};
WebserverStateEnum WebserverState = STATE_NEW;

//SPI: Default https://learn.sparkfun.com/tutorials/esp32-thing-hookup-guide/using-the-arduino-addon
// MOSI 23
// MISO 19
// CLK  18

uint8_t VOLUME = 20; // volume level 0-100, inverse
Adafruit_VS1053 player = Adafruit_VS1053(VS1053_RST, VS1053_CS, VS1053_DCS, VS1053_DREQ);

char* ssid = "SSID_HERE";
char* pass = "PASSWORD_HERE";

const String host = "<tvheadend-host>:9981";
String stationName = "";
String volumeString = "";
String currentStation = "No Station";
String profile = "esp"; // use/create a streaming profile in tvheadend where the video is disabled and the audio is transcoded to "aac" inside "raw audio stream"-container


const size_t buffLen = 32;

uint8_t buff[buffLen] = {0};

HTTPClient client;
WiFiServer wifiServer(80);
WiFiClient sclient;
WiFiClient* stream = NULL;

void setup () 
{
    Serial.begin(115200);
    delay(500);
    
    // initialize SPI bus;
    SPI.begin();
    delay(100);
    // initialize VS1053 player
     if (! player.begin()) { // initialise the music player
     Serial.println(F("Couldn't find VS1053, do you have the right pins defined?"));
     while (1);
    }

    Serial.println(F("VS1053 found"));

  // Enable differential output
    //uint16_t mode = VS1053_MODE_SM_LAYER12 ; //VS1053_MODE_SM_STREAM | 
    //player.sciWrite(VS1053_REG_MODE, mode);

    //Workaround midi and so on
    player.sciWrite(VS1053_REG_WRAMADDR, VS1053_GPIO_DDR);
    player.sciWrite(VS1053_REG_WRAM , 3); // GPIO DDR = 3
    player.sciWrite(VS1053_REG_WRAMADDR, VS1053_GPIO_ODATA);
    player.sciWrite(VS1053_REG_WRAM , 0); // GPIO ODATA = 0
    delay(100); //Needed?
    player.sciWrite(VS1053_REG_MODE, VS1053_MODE_SM_LINE1 | VS1053_MODE_SM_SDINEW | VS1053_MODE_SM_RESET | VS1053_MODE_SM_STREAM ); // soft reset
    delay(100);
    player.dumpRegs();

    player.setVolume(VOLUME, VOLUME);

    while(!player.readyForData()) { 
      Serial.println("Player not ready. Wait...");
      delay(100); 
    }
    
    player.dumpRegs();
    
    Serial.println("connect to wifi");  
    WiFi.begin(ssid, pass);
    while (WiFi.status() != WL_CONNECTED) 
    {
      delay(500);
      Serial.print(".");
    }

    Serial.println("WiFi connected");  
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
    //player.playChunk(hello2, sizeof(hello2)); //VS1053 is wake up & running

    wifiServer.begin();
}

void loop() 
{
  if (Serial.available()) {
    char c = Serial.read();
    handle_input(c);
  }
  if (stream != NULL) {      
      if (stream->available() > 0) 
      {
        uint8_t bytesread = stream->readBytes(buff, buffLen);
        //bool pinREQ = digitalRead(VS1053_DREQ);
        while (!player.readyForData()) {
        }
        player.playData(buff, bytesread);
      }
  }

  //handling server
  if (sclient.connected()) {
    if (sclient.available()) {
      char c = sclient.read();
      handle_input(c);
    }
  } else {
    sclient = wifiServer.available();
    WebserverState = STATE_NEW;
  }
}

void handle_input(char c) {
  switch (WebserverState) {
    case STATE_NEW:
      stationName = "";
      volumeString = "";
      switch (c) {
        case 'E':
          Serial.println("Terminate stream");
          currentStation = "TERMINATED";
          if (stream != NULL) {
            stream->stop();
            stream = NULL;
          }
          WebserverState = STATE_END;
          break;
        case 'S':
          WebserverState = STATE_STATION;
          break;
        case 'V':
          WebserverState = STATE_VOLUME;
          break;
        case 'I':
          WebserverState = STATE_INFO;
          break;
        default:
          Serial.println(c);
          Serial.println(" is not recognized as selector");
      }
      break;
    case STATE_STATION:
      stationName += c;
      if (stationName.length() == 32) {
        Serial.print("New station: ");
        Serial.println(stationName);
        if (stream == NULL) {
          station_connect();
          Serial.println("reject new stream during running stream");
        }
        WebserverState = STATE_END;
      }
      break;
    case STATE_VOLUME:
      volumeString += c;
      if (volumeString.length() == 3) {
        int newVolume = volumeString.toInt();
        Serial.print("New volume: ");
        Serial.println(newVolume);
        if (newVolume < 0) newVolume = 0;
        if (newVolume > 100) newVolume = 100;
        VOLUME = 100-newVolume;
        player.setVolume(VOLUME, VOLUME);
        WebserverState = STATE_END;
      }
      break;
    case STATE_INFO:
      sclient.print("{\"volume\": " + String(VOLUME) + ", \"station: \": \"" + currentStation + "\"}");
      WebserverState = STATE_END;
      break;
    case STATE_END:
      Serial.println("ignore more input in END state!");
      break;
  }
  if (WebserverState == STATE_END) sclient.stop();
}

void station_connect () 
{
    String url = "http://" + host + "/stream/channel/" + stationName + "?profile=" + profile;
    Serial.println(url);
    client.begin(url);
    
    int httpCode = client.GET();
    if (httpCode > 0) {
      Serial.printf("GET <%d>\n", httpCode);
    } else {
      printf("[HTTP] GET <error: %s>\n", client.errorToString(httpCode).c_str());
    }
    if (httpCode == 200) {
      currentStation = stationName;
      stream = client.getStreamPtr();
    } else {
      currentStation = "FAILED" + String(httpCode);
      client.end();
    }
}
