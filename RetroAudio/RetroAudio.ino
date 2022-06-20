// 2022 Maximilian Kern
// Tested with ESP32 Core V1.0.6
#include <Arduino.h>
#include <JC_Button.h> // https://github.com/JChristensen/Button
#include "Audio.h" // https://github.com/schreibfaul1/ESP32-audioI2S
#include "SPI.h"
#include "SD.h"
#include "FS.h"
#include <TFT_eSPI.h> // Hardware-specific library
//#include "WiFiMulti.h"

#define BLACK 0
#define WHITE 1
#define LIGHTGREEN 0x05AE
#define DARKGREEN  0x022B
#define LIGHTPINK  0xC169
#define DARKPINK   0x30A4
#define LIGHTBLUE  0x6C7F
#define DARKBLUE   0x1929

#define WIDTH    320
#define HEIGHT   240

#define I2S_DOUT     25
#define I2S_BCLK     27
#define I2S_LRC      26

#define SD_CS        5
#define SD_MOSI      23
#define SD_MISO      19
#define SD_SCK       18

//#define LCD_DC       16 // defined within TFT_eSPI User_Setup.h
//#define LCD_CS       15
//#define LCD_MOSI     13
//#define LCD_SCK      14
//#define LCD_MISO     12
#define LCD_BL     17 

#define ABUT      22
#define BBUT      21
#define CBUT      34
#define ENCINTA   39
#define ENCINTB   35
#define ENCBUT    36

#define JACK_SENS 32
#define VBAT_SENS 33

Button buttonA(ABUT, false, true, 5); //pin, pullup, invert, debounce
Button buttonB(BBUT, false, true, 5);
Button buttonC(CBUT, false, true, 5);
Button buttonENC(ENCBUT, false, true, 5);

TFT_eSPI tft = TFT_eSPI();       // Invoke custom library
TFT_eSprite img = TFT_eSprite(&tft);
unsigned int spriteX = 118; //Tape animation sprite size
const int tapePosY = 84; //Tape animation Y Center Position


Audio audio;
// Audio position and duration for UI etc.
unsigned long currentPos;
unsigned long audioDuration;
boolean positionChanged = true;
boolean _f_eof = false;
unsigned long savePosFlag;

int volume_hp = 1;
int volume_spk = 3;
const int volume_hp_max = 4;
const int volume_spk_max = 21;
boolean volumeChanged = true;
unsigned long saveVolFlag;

String tracks[255];
unsigned int trackTotal = 0;
int currentTrackNum = 0;
String currentTitle;
String currentArtist;
boolean audioIsPlaying = false;

boolean headphones = true;

unsigned int batteryVoltageMilliVolts;
#define batteryVoltageGood 4500
#define batteryVoltageBad 4000
boolean batteryLow = false;

// interrupt service routine vars
boolean A_set = false;            
boolean B_set = false;

/*
WiFiMulti wifiMulti;
String ssid = "";
String password = "";
*/

//****************************************************************************************
//                                   A U D I O _ T A S K                                 *
//****************************************************************************************

struct audioMessage{
    uint8_t     cmd;
    const char* txt;
    uint32_t    value;
    uint32_t    ret;
} audioTxMessage, audioRxMessage;

enum : uint8_t { SET_VOLUME, GET_VOLUME, CONNECTTOHOST, CONNECTTOSD, PAUSE_RESUME, GET_DURATION, GET_POSITION, SET_POSITION, SET_OFFSET };

QueueHandle_t audioSetQueue = NULL;
QueueHandle_t audioGetQueue = NULL;

void CreateQueues(){
    audioSetQueue = xQueueCreate(10, sizeof(struct audioMessage));
    audioGetQueue = xQueueCreate(10, sizeof(struct audioMessage));
}

void audioTask(void *parameter) {
    CreateQueues();
    if(!audioSetQueue || !audioGetQueue){
        log_e("queues are not initialized");
        while(true){;}  // endless loop
    }

    struct audioMessage audioRxTaskMessage;
    struct audioMessage audioTxTaskMessage;

    audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
    audio.setVolume(0); // 0...21

    while(true){
        if(xQueueReceive(audioSetQueue, &audioRxTaskMessage, 1) == pdPASS) {
            if(audioRxTaskMessage.cmd == SET_VOLUME){
                audioTxTaskMessage.cmd = SET_VOLUME;
                audio.setVolume(audioRxTaskMessage.value);
                audioTxTaskMessage.ret = 1;
                xQueueSend(audioGetQueue, &audioTxTaskMessage, portMAX_DELAY);
            }
            else if(audioRxTaskMessage.cmd == CONNECTTOHOST){
                audioTxTaskMessage.cmd = CONNECTTOHOST;
                audioTxTaskMessage.ret = audio.connecttohost(audioRxTaskMessage.txt);
                xQueueSend(audioGetQueue, &audioTxTaskMessage, portMAX_DELAY);
            }
            else if(audioRxTaskMessage.cmd == CONNECTTOSD){
                audioTxTaskMessage.cmd = CONNECTTOSD;
                audio.stopSong(); // free memory
                audioTxTaskMessage.ret = audio.connecttoSD(audioRxTaskMessage.txt, audioRxTaskMessage.value);
                xQueueSend(audioGetQueue, &audioTxTaskMessage, portMAX_DELAY);
            }
            else if(audioRxTaskMessage.cmd == GET_VOLUME){
                audioTxTaskMessage.cmd = GET_VOLUME;
                audioTxTaskMessage.ret = audio.getVolume();
                xQueueSend(audioGetQueue, &audioTxTaskMessage, portMAX_DELAY);
            }
            else if(audioRxTaskMessage.cmd == GET_POSITION){
                audioTxTaskMessage.cmd = GET_POSITION;
                audioTxTaskMessage.ret = audio.getAudioCurrentTime();
                xQueueSend(audioGetQueue, &audioTxTaskMessage, portMAX_DELAY);
            }
            else if(audioRxTaskMessage.cmd == SET_POSITION){
                audioTxTaskMessage.cmd = SET_POSITION;
                audioTxTaskMessage.ret = audio.setAudioPlayPosition(audioRxTaskMessage.value);
                xQueueSend(audioGetQueue, &audioTxTaskMessage, portMAX_DELAY);
            }
            else if(audioRxTaskMessage.cmd == GET_DURATION){
                audioTxTaskMessage.cmd = GET_DURATION;
                audioTxTaskMessage.ret = audio.getAudioFileDuration();
                xQueueSend(audioGetQueue, &audioTxTaskMessage, portMAX_DELAY);
            }
            else if(audioRxTaskMessage.cmd == SET_OFFSET){
                audioTxTaskMessage.cmd = SET_OFFSET;
                audio.setTimeOffset(audioRxTaskMessage.value);
                audioTxTaskMessage.ret = 1;
                xQueueSend(audioGetQueue, &audioTxTaskMessage, portMAX_DELAY);
            }
            else if(audioRxTaskMessage.cmd == PAUSE_RESUME){
                audioTxTaskMessage.cmd = PAUSE_RESUME;
                audioTxTaskMessage.ret = audio.pauseResume();
                xQueueSend(audioGetQueue, &audioTxTaskMessage, portMAX_DELAY);
            }
            else{
                log_i("error");
            }
        }
        audio.loop();
        // refresh audio track position
        int newAudioPos = audio.getAudioCurrentTime();
        if(currentPos != newAudioPos){
          currentPos = newAudioPos;
          positionChanged = true;
        }
        // refresh audio track duration
        unsigned long newAudioDuraton = audio.getAudioFileDuration();
        if(newAudioDuraton > 0 && audioDuration != newAudioDuraton){
          audioDuration = audio.getAudioFileDuration();          
        } 
    }
}

void audioInit() {
    xTaskCreatePinnedToCore(
        audioTask,             /* Function to implement the task */
        "audioplay",           /* Name of the task */
        5000,                  /* Stack size in words */
        NULL,                  /* Task input parameter */
        2 | portPRIVILEGE_BIT, /* Priority of the task */
        NULL,                  /* Task handle. */
        1                      /* Core where the task should run */
    );
}

audioMessage transmitReceive(audioMessage msg){
    xQueueSend(audioSetQueue, &msg, portMAX_DELAY);
    if(xQueueReceive(audioGetQueue, &audioRxMessage, portMAX_DELAY) == pdPASS){
        if(msg.cmd != audioRxMessage.cmd){
            log_e("wrong reply from message queue");
        }
    }
    return audioRxMessage;
}

void audioSetVolume(uint8_t vol){
    audioTxMessage.cmd = SET_VOLUME;
    audioTxMessage.value = vol;
    transmitReceive(audioTxMessage);
}

uint8_t audioGetVolume(){
    audioTxMessage.cmd = GET_VOLUME;
    audioMessage RX = transmitReceive(audioTxMessage);
    return RX.ret;
}

bool audioPauseResume(){
    audioIsPlaying = !audioIsPlaying;
    audioTxMessage.cmd = PAUSE_RESUME;
    audioMessage RX = transmitReceive(audioTxMessage);
    return RX.ret;
}

bool audioConnecttohost(const char* host){
    audioTxMessage.cmd = CONNECTTOHOST;
    audioTxMessage.txt = host;
    audioMessage RX = transmitReceive(audioTxMessage);
    audioIsPlaying = true;
    return RX.ret;
}

bool audioConnecttoSD(const char* filename, unsigned long resumePos){
    audioTxMessage.cmd = CONNECTTOSD;
    audioTxMessage.txt = filename;
    audioTxMessage.value = resumePos;
    audioMessage RX = transmitReceive(audioTxMessage);
    audioIsPlaying = true;
    savePosFlag = millis() + 600000; // save position only for files longer than 10min
    return RX.ret;
}

uint32_t audioGetPosition(){
    audioTxMessage.cmd = GET_POSITION;
    audioMessage RX = transmitReceive(audioTxMessage);
    return RX.ret;
}

uint32_t audioSetPosition(unsigned int secPosition){
    audioTxMessage.cmd = SET_POSITION;
    audioTxMessage.value = secPosition;
    audioMessage RX = transmitReceive(audioTxMessage);
    return RX.ret;
}

uint32_t audioGetDuration(){
    audioTxMessage.cmd = GET_DURATION;
    audioMessage RX = transmitReceive(audioTxMessage);
    return RX.ret;
}

bool audioSetOffset(int secOffset){
    audioTxMessage.cmd = SET_OFFSET;
    audioTxMessage.value = secOffset;
    audioMessage RX = transmitReceive(audioTxMessage);
    return RX.ret;
}

//****************************************************************************************
//                                    HELPER FUNCTIONS                                   *
//****************************************************************************************

void IRAM_ATTR isr_enca(){
  if( digitalRead(ENCINTA) != A_set ) {  // debounce once more
    A_set = !A_set;
    // adjust counter + if A leads B
    if( A_set && !B_set ){
      if(headphones){
        if(volume_hp++ >= volume_hp_max) volume_hp = volume_hp_max;
      }
      else{
        if(volume_spk++ >= volume_spk_max) volume_spk = volume_spk_max;
      }
    }
    volumeChanged = true;
  }
}

void IRAM_ATTR isr_encb(){
  if( digitalRead(ENCINTB) != B_set ) {
    B_set = !B_set;
    //  adjust counter - 1 if B leads A
    if( B_set && !A_set ){
      if(headphones){
        if(volume_hp-- <= 0) volume_hp = 0;
      }
      else{
        if(volume_spk-- <= 0) volume_spk = 0;
      }
    }
    volumeChanged = true;
  }
} 

void drawTitleInfo(){
  tft.setTextPadding(312-18);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(0xFFFF, TFT_BLACK);
  if(currentTitle.length() > 21) currentTitle = currentTitle.substring(0, 20) + "...";
  tft.drawString(currentTitle, 8, 190, 4);
  tft.drawString(currentArtist, 10, 218, 2);
}

void drawProgressBar(){
  static int progressBar;
  const int progressBarWidth = 235;
  progressBar = map(currentPos, 0, audioDuration, 1, progressBarWidth); 
  progressBar = min(progressBar, progressBarWidth);
  tft.fillRect(10, 174, progressBar+1, 2, LIGHTPINK);
  tft.fillRect(11 + progressBar, 174, progressBarWidth-progressBar, 2, DARKPINK);
  tft.setTextPadding(60);
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  char posString[16];
  // Hide hour digits until needed
  if(currentPos < 3600) sprintf(posString, "%02d:%02d", int(currentPos/60)%60, int(currentPos%60));
  else sprintf(posString, "%2d:%02d:%02d", int(currentPos/3600), int(currentPos/60)%60, int(currentPos%60));
  tft.drawString(posString, 310, 167, 2);
}

bool isMusic(const char* filename) {
  int8_t len = strlen(filename);
  bool result;
  if (  strstr(filename + (len - 4), ".mp3")
     || strstr(filename + (len - 4), ".wav")
    ) {
    result = true;
  } else {
    result = false;
  }
  return result;
}

void scanSD(){
  File root = SD.open("/");
  root.rewindDirectory();
  for(int i = 0; i < 255; i++){
    File entry =  root.openNextFile();
    if(!entry){
      break;
    }
    else if(!entry.isDirectory() && isMusic(entry.name())){
      tracks[trackTotal] = entry.name();
      Serial.println(entry.name());
      trackTotal += 1;
    }
  }
  root.close();
}

String getSaveFileName(String audioFileName){
  String tempString = audioFileName;
  tempString.replace(audioFileName.c_str() + (audioFileName.length() - 4), ".txt");
  tempString = "/persistence" + tempString;
  return tempString;
}

void nextTrack(){
  if(currentTrackNum++ >= trackTotal-1) currentTrackNum = 0;
}

void prevTrack(){
  if(currentTrackNum-- <= 0) currentTrackNum = trackTotal-1;
}

void savePosition(){
  String saveFileName = getSaveFileName(tracks[currentTrackNum]);
    
  File saveFile = SD.open(saveFileName, FILE_APPEND);
  if(saveFile){
      char posString[10];
      sprintf(posString, "%010d", audio.getFilePos());
      saveFile.println(posString);
      saveFile.close(); // close the file:
      Serial.print("File saved at position ");
      Serial.println(posString);
  }
  saveFile.close();
}

unsigned int restorePosition(String tempFileName){
  String tempSaveFile = getSaveFileName(tempFileName);
  unsigned int filePos = 0;

  File saveFile = SD.open(tempSaveFile, FILE_READ);
  if(saveFile){
    saveFile.seek(saveFile.size()-12); // go to the last 10-char line
    filePos = saveFile.readStringUntil('\n').toInt();

    Serial.print("File restored to Position ");
    Serial.println(filePos);
  }
  return filePos;
}

void saveVolume(){
  String saveFileName = "/persistence/setting_volume.txt";
    
  File saveFile = SD.open(saveFileName, FILE_APPEND);
  if(saveFile){
      char volString[10];
      sprintf(volString, "%02d%02d", volume_spk, volume_hp);
      saveFile.println(volString);
      saveFile.close();
      Serial.print("Volume saved: ");
      Serial.println(volString);
  }
  saveFile.close();
}

unsigned int restoreVolume(){
  String tempSaveFile = "/persistence/setting_volume.txt";
  unsigned int filePos = 0;

  File saveFile = SD.open(tempSaveFile, FILE_READ);
  if(saveFile){
    saveFile.seek(saveFile.size()-6); // go to the last 6-char line
    String tempString;
    tempString = saveFile.readStringUntil('\n');
    volume_spk = tempString.substring(0, 2).toInt();
    volume_hp = tempString.substring(2, 4).toInt();

    Serial.print("volume restored to spk:");
    Serial.print(volume_spk);
    Serial.print(" and hp:");
    Serial.println(volume_hp);
  }
  return filePos;
}

//****************************************************************************************
//                                          SETUP                                        *
//****************************************************************************************

void setup() {
  
  pinMode(LCD_BL, OUTPUT);
  pinMode(ABUT, INPUT);
  pinMode(BBUT, INPUT);
  pinMode(CBUT, INPUT);
  pinMode(ENCINTA, INPUT);
  pinMode(ENCINTB, INPUT);
  pinMode(ENCBUT, INPUT);
  pinMode(SD_CS, OUTPUT);

  attachInterrupt(ENCINTA, isr_enca, CHANGE);
  attachInterrupt(ENCINTB, isr_encb, CHANGE);

  
  Serial.begin(115200);

  tft.init();
  tft.setRotation(3);
  tft.fillScreen(TFT_BLACK);
  tft.setSwapBytes(true);


  digitalWrite(SD_CS, HIGH);
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI);
  SPI.setFrequency(20000000);
  SD.begin(SD_CS);
  scanSD();
  //create a folder to store save files
  if(!SD.exists("/persistence"))  SD.mkdir("/persistence");    

  /*
  WiFi.mode(WIFI_STA);
  wifiMulti.addAP(ssid.c_str(), password.c_str());
  wifiMulti.run();
  if(WiFi.status() != WL_CONNECTED){
      WiFi.disconnect(true);
      wifiMulti.run();
  }
  */

  restoreVolume();
  
  audioInit();
  audioSetVolume(volume_hp);
  //log_i("current volume is: %d", audioGetVolume());
  //audioConnecttohost("https://edge05.live-sm.absolutradio.de/absolut-relax/stream/mp3");
  //audioConnecttoSD("Windows XP.mp3");
  audioConnecttoSD(tracks[currentTrackNum].c_str(), restorePosition(tracks[currentTrackNum]));

  //Draw Background
  tft.fillRect(0, 0, 320, 240, 0x0000);
  
  tft.fillSmoothCircle(320-80, tapePosY, 72, 0xFFFF); //Outer circle
  tft.fillSmoothCircle(320-80, tapePosY, 67, 0x0000, 0xFFFF);
  tft.fillSmoothCircle(80, tapePosY, 72, 0xFFFF); //Outer circle
  tft.fillSmoothCircle(80, tapePosY, 67, 0x0000, 0xFFFF);
  tft.fillRect(80, tapePosY+68, 320-80-80, 5, 0xFFFF); 

  // turn on display after initialization
  digitalWrite(LCD_BL, HIGH);  
  
  img.setColorDepth(8);  
  img.createSprite(spriteX, spriteX);

  delay(200);
  audioPauseResume();
}

//****************************************************************************************
//                                          LOOP                                         *
//****************************************************************************************

void loop() {

  //check battery
  batteryVoltageMilliVolts = 2*analogReadMilliVolts(VBAT_SENS);
  if(batteryVoltageMilliVolts < batteryVoltageBad && !batteryLow){
    // draw low battery symbol
    tft.drawRect(WIDTH-15, HEIGHT-25, 10, 19, LIGHTPINK);
    tft.fillRect(WIDTH-13, HEIGHT-27, 6, 2, LIGHTPINK);
    tft.fillRect(WIDTH-13, HEIGHT-11, 6, 3, LIGHTPINK);
    batteryLow = true;
  }
  else if(batteryVoltageMilliVolts > batteryVoltageGood && batteryLow){
    // erase low battery symbol
    tft.fillRect(WIDTH-15, HEIGHT-27, 10, 21, TFT_BLACK);
    batteryLow = false;
  }
  
  //check headphone jack
  if(analogReadMilliVolts(JACK_SENS) > 2800){
    if(headphones != true){
      headphones = true; 
      volumeChanged = true;   
    }
  }
  else{
    if(headphones != false){
      headphones = false; 
      volumeChanged = true;   
    }
  }

  // set volume and draw volume bar
  if(volumeChanged){
    int volumeBarWidth = 0;
    if(headphones){ 
      audioSetVolume(volume_hp);
      volumeBarWidth = map(volume_hp, 0, volume_hp_max, 0, 300);
    }
    else {
      audioSetVolume(volume_spk);
      volumeBarWidth = map(volume_spk, 0, volume_spk_max, 0, 300);
    }

    tft.fillRect(10, 0, volumeBarWidth, 2, LIGHTGREEN);
    tft.fillRect(10+volumeBarWidth, 0, 300-volumeBarWidth, 2, DARKGREEN);

    saveVolFlag = millis() + 10000; // save volume only after 10s

    volumeChanged = false; // reset volume flag
  }
  
  // handle end of file
  if(_f_eof == true){
    audioPauseResume();
    nextTrack(); //remove this line to repeat track
    audioConnecttoSD(tracks[currentTrackNum].c_str(), restorePosition(tracks[currentTrackNum]));
    _f_eof = false;
  }

  // check if it's time to save the volume
  // do this only when necessary to reduce SD wear and visual glitches
  if(millis() > saveVolFlag){
    saveVolume();
    saveVolFlag = millis() + 3600000; // reset the flag far away
  }
  
  // check if it's time to save the file position
  if(millis() > savePosFlag && audioIsPlaying){    
    savePosition();
    savePosFlag = millis() + 60000; // set flag to save settings to 60 seconds from now
  }
  
  // animate tape spools
  img.fillSprite(TFT_TRANSPARENT);
  img.fillCircle(59, 59, 59, 0x0000);
    
  static float angle = 0;  
  static unsigned long oldMillis = millis();  
  if(audioIsPlaying) angle -= float(millis() - oldMillis)/600;
  oldMillis = millis();
  for(int i = 1; i <= 3; i++){
    float angleOffset = i*TWO_PI/3;    
    float s = sin(angle+angleOffset);
    float c = cos(angle+angleOffset);
    img.drawWideLine(spriteX/2+(0*c-36*s), spriteX/2+(0*s+36*c), spriteX/2+(0*c-56*s), spriteX/2+(0*s+56*c), 5, 0xFFFF, 0x0000);
    s = sin(angle+angleOffset+1.0472);
    c = cos(angle+angleOffset+1.0472);
    img.drawWideLine(spriteX/2+(0*c-14*s), spriteX/2+(0*s+14*c), spriteX/2+(0*c-16*s), spriteX/2+(0*s+16*c), 5, 0xFFFF, 0x0000);    
  }
  if(angle > 2*PI) angle = 0;
  
  img.fillSmoothCircle(spriteX/2, spriteX/2, 15, 0xFFFF); //Inner circle
  img.fillSmoothCircle(spriteX/2, spriteX/2, 10, 0x0000, 0xFFFF);

  img.pushSprite(320-80-spriteX/2, tapePosY-spriteX/2, TFT_TRANSPARENT);
  img.pushSprite(80-spriteX/2, tapePosY-spriteX/2, TFT_TRANSPARENT);
  
  //draw progress bar
  if(positionChanged){
    drawProgressBar();
    positionChanged = false;
  }  

  if(buttonA.pressedFor(1000)){
    audioSetOffset(-30);
    buttonA.read();
  }
  else if(buttonA.wasReleased()){
    Serial.println("<<");
    prevTrack();
    if(audioIsPlaying) audioPauseResume();
    audioConnecttoSD(tracks[currentTrackNum].c_str(), restorePosition(tracks[currentTrackNum]));
  }
  
  if(buttonB.pressedFor(1000)){
    Serial.println("long press pause");
    buttonB.read();
  }
  else if(buttonB.wasReleased()){
    Serial.println("pause");
    audioPauseResume();
  }
  
  if(buttonC.pressedFor(1000)){
    audioSetOffset(30);
    buttonC.read();
  }
  else if(buttonC.wasReleased()){
    Serial.println(">>");
    nextTrack();
    if(audioIsPlaying) audioPauseResume();
    audioConnecttoSD(tracks[currentTrackNum].c_str(), restorePosition(tracks[currentTrackNum]));    
  }  

  buttonA.read();
  buttonB.read();
  buttonC.read();
  buttonENC.read();
}

//*****************************************************************************************
//                                  E V E N T S                                           *
//*****************************************************************************************

void audio_info(const char *info){
  Serial.print("info        "); Serial.println(info);
  // Display the file name if id3 data is unavailable
  String infoString = (String)info;
  if(infoString.indexOf("file has no mp3") == 0){
    currentTitle = tracks[currentTrackNum].substring(1);
    currentArtist = "";
    drawTitleInfo();
  }
}
void audio_id3data(const char *info){  //id3 metadata
  Serial.print("id3data     ");Serial.println(info);
  
  String id3String = (String)info;
  if(id3String.indexOf("Title:") == 0){
    currentTitle = id3String.substring(7);
    drawTitleInfo();
  }
  else if(id3String.indexOf("Artist:") == 0){
    currentArtist = id3String.substring(8);
    drawTitleInfo();
  }
}
void audio_showstation(const char *info){
  Serial.print("station     ");Serial.println(info);
}
void audio_showstreamtitle(const char *info){
  Serial.print("streamtitle ");Serial.println(info);
}
void audio_bitrate(const char *info){
  Serial.print("bitrate     ");Serial.println(info);
}
void audio_commercial(const char *info){  //duration in sec
  Serial.print("commercial  ");Serial.println(info);
}
void audio_icyurl(const char *info){  //homepage
  Serial.print("icyurl      ");Serial.println(info);
}
void audio_lasthost(const char *info){  //stream URL played
  Serial.print("lasthost    ");Serial.println(info);
}
void audio_eof_speech(const char *info){
  Serial.print("eof_speech  ");Serial.println(info);
}
void audio_eof_mp3(const char * info){
  Serial.print("End of file");Serial.println(info);
  _f_eof = true;
}
