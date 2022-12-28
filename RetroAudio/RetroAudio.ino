// 2022 Maximilian Kern		
#include <Arduino.h>
#include <JC_Button.h> // https://github.com/JChristensen/Button
#include "Audio.h" // https://github.com/schreibfaul1/ESP32-audioI2S
#include "SPI.h"
#include "SD.h"
#include "FS.h"
#include <TFT_eSPI.h> // Hardware-specific library

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

//#define LCD_DC       16 // defined in TFT_eSPI User_Setup.h
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
boolean _f_eof = false;
unsigned long savePosFlag = 60000;

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

boolean headphones = true;

unsigned int batteryVoltageMilliVolts;
#define batteryVoltageGood 4500
#define batteryVoltageBad 4000
boolean batteryLow = false;

// Power saving
boolean screenOff = false;
unsigned int screenOffTime = 15000;
unsigned long screenOffFlag = screenOffTime;

// interrupt service routine vars
boolean A_set = false;            
boolean B_set = false;

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

void turnScreenOff(boolean powerdown){
  // low Power mode
  if(powerdown){
    digitalWrite(LCD_BL, LOW);  
    //setCpuFrequencyMhz(80);
    screenOff = true;
  }
  // high power mode
  else {
    digitalWrite(LCD_BL, HIGH);  
    //setCpuFrequencyMhz(240);
    screenOffFlag = millis() + screenOffTime;
    screenOff = false;
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
  unsigned long currentPos = audio.getAudioCurrentTime();
  unsigned long audioDuration = audio.getAudioFileDuration();

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
  tempString = "/persistence/" + tempString;
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

void initGraphics(){  

  //Draw Background
  tft.fillRect(0, 0, 320, 240, 0x0000);
  
  tft.fillSmoothCircle(320-80, tapePosY, 72, 0xFFFF); //Outer circle
  tft.fillSmoothCircle(320-80, tapePosY, 67, 0x0000, 0xFFFF);
  tft.fillSmoothCircle(80, tapePosY, 72, 0xFFFF); //Outer circle
  tft.fillSmoothCircle(80, tapePosY, 67, 0x0000, 0xFFFF);
  tft.fillRect(80, tapePosY+68, 320-80-80, 5, 0xFFFF); 
}

void drawTapeSpools(){

  img.fillSprite(TFT_TRANSPARENT);
  img.fillCircle(59, 59, 59, 0x0000);
  audio.loop();
  static float angle = 0;  
  static unsigned long oldMillis = millis();  
  if(audio.isRunning()) angle -= float(millis() - oldMillis)/600;
  oldMillis = millis();
  for(int i = 1; i <= 3; i++){
    audio.loop();
    float angleOffset = i*TWO_PI/3;    
    float s = sin(angle+angleOffset);
    float c = cos(angle+angleOffset);
    audio.loop();
    img.drawWideLine(spriteX/2+(0*c-36*s), spriteX/2+(0*s+36*c), spriteX/2+(0*c-56*s), spriteX/2+(0*s+56*c), 5, 0xFFFF, 0x0000);
    audio.loop();
    s = sin(angle+angleOffset+1.0472);
    c = cos(angle+angleOffset+1.0472);
    img.drawWideLine(spriteX/2+(0*c-14*s), spriteX/2+(0*s+14*c), spriteX/2+(0*c-16*s), spriteX/2+(0*s+16*c), 5, 0xFFFF, 0x0000);    
    audio.loop();
  }
  if(angle > 2*PI) angle = 0;
  audio.loop();
  img.fillSmoothCircle(spriteX/2, spriteX/2, 15, 0xFFFF); //Inner circle
  img.fillSmoothCircle(spriteX/2, spriteX/2, 10, 0x0000, 0xFFFF);
  audio.loop();
  img.pushSprite(320-80-spriteX/2, tapePosY-spriteX/2, TFT_TRANSPARENT);
  img.pushSprite(80-spriteX/2, tapePosY-spriteX/2, TFT_TRANSPARENT);
}

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
  Serial.println("gestartet!");

  tft.init();
  tft.setRotation(3);
  tft.fillScreen(TFT_BLACK);
  tft.setSwapBytes(true);


  digitalWrite(SD_CS, HIGH);
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI);
  SD.begin(SD_CS);
  scanSD();
  //create a folder to store save files
  if(!SD.exists("/persistence"))  SD.mkdir("/persistence");    

  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
  restoreVolume();
  
  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
  audio.setVolume(volume_hp);

  audio.connecttoSD(tracks[currentTrackNum].c_str(), restorePosition(tracks[currentTrackNum]));

  //draw Background
  initGraphics();

  // turn on display after initialization
  digitalWrite(LCD_BL, HIGH);  
  
  img.setColorDepth(8);  
  img.createSprite(spriteX, spriteX);

  //delay(200);
  /* Änderung für Hörbuch Modus
  audioPauseResume();
  */
}

void loop()
{
  audio.loop();

  Serial.println(audio.getAudioCurrentTime());
  
  // Turn screen off after a timeout
  if(!screenOff && millis() > screenOffFlag){
    turnScreenOff(true);
  }

  
  //check battery
  batteryVoltageMilliVolts = 2*analogReadMilliVolts(VBAT_SENS);
  if(batteryVoltageMilliVolts < batteryVoltageBad && !batteryLow){
    batteryLow = true;
    // draw low battery symbol
    tft.drawRect(WIDTH-15, HEIGHT-25, 10, 19, LIGHTPINK);
    tft.fillRect(WIDTH-13, HEIGHT-27, 6, 2, LIGHTPINK);
    tft.fillRect(WIDTH-13, HEIGHT-11, 6, 3, LIGHTPINK);
  }
  else if(batteryVoltageMilliVolts > batteryVoltageGood && batteryLow){
    batteryLow = false;
    // erase low battery symbol
    tft.fillRect(WIDTH-15, HEIGHT-27, 10, 21, TFT_BLACK);
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
    turnScreenOff(false);    
    int volumeBarWidth = 0;
    if(headphones){ 
      audio.setVolume(volume_hp);
      volumeBarWidth = map(volume_hp, 0, volume_hp_max, 0, 300);
    }
    else {
      audio.setVolume(volume_spk);
      volumeBarWidth = map(volume_spk, 0, volume_spk_max, 0, 300);
    }
    
    tft.fillRect(10, 0, volumeBarWidth, 2, LIGHTGREEN);
    tft.fillRect(10+volumeBarWidth, 0, 300-volumeBarWidth, 2, DARKGREEN);

    saveVolFlag = millis() + 10000; // save volume only after 10s

    volumeChanged = false; // reset volume flag
  }
  
  // handle end of file
  if(_f_eof == true){
    audio.pauseResume();
    nextTrack(); //remove this line to repeat track
    audio.connecttoSD(tracks[currentTrackNum].c_str(), restorePosition(tracks[currentTrackNum]));
    _f_eof = false;
  }

  // check if it's time to save the volume
  // do this only when necessary to reduce SD wear and visual glitches
  if(millis() > saveVolFlag){
    saveVolume();
    saveVolFlag = millis() + 3600000; // reset the flag far away
  }
  
  // check if it's time to save the file position
  if(millis() > savePosFlag && audio.isRunning()){    
    savePosition();
    savePosFlag = millis() + 60000; // set flag to save settings to 60 seconds from now
  }
  
    // animate tape spools
  drawTapeSpools();
  
  //draw progress bar
  if(audio.isRunning())  drawProgressBar();

  if(buttonA.pressedFor(1000)){
    turnScreenOff(false);    
    audio.setTimeOffset(-30);
    buttonA.read();
  }
  else if(buttonA.wasReleased()){
    turnScreenOff(false);    
    Serial.println("<<");
    // Hörbuchmodus --- temporäre Änderung!!!
    prevTrack();
    if(audio.isRunning()) audio.pauseResume();
    audio.connecttoSD(tracks[currentTrackNum].c_str(), restorePosition(tracks[currentTrackNum])); 
  }
  
  if(buttonB.pressedFor(1000)){
    turnScreenOff(false);    
    Serial.println("long press pause");
    buttonB.read();
  }
  else if(buttonB.wasReleased()){
    turnScreenOff(false);    
    Serial.println("pause");
    audio.pauseResume();
  }
  
  if(buttonC.pressedFor(1000)){
    turnScreenOff(false);    
    audio.setTimeOffset(30);
    buttonC.read();
  }
  else if(buttonC.wasReleased()){
    turnScreenOff(false);    
    // Hörbuchmodus --- temporäre Änderung!!!
    Serial.println(">>");
    nextTrack();
    if(audio.isRunning()) audio.pauseResume();
    audio.connecttoSD(tracks[currentTrackNum].c_str(), restorePosition(tracks[currentTrackNum]));
  }
  else if(buttonENC.wasReleased()){
    turnScreenOff(false);
    // Flip Screen
    if(tft.getRotation() == 1) tft.setRotation(3);
    else tft.setRotation(1);
    initGraphics();
    drawTapeSpools();
    drawProgressBar();
    drawTitleInfo();
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