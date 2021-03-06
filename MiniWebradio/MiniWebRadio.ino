//*********************************************************************************************************
//*    MiniWebRadio -- Webradio receiver for ESP32, 2.8 color display (320x240px) and VS1053 MP3 module.  *
//*********************************************************************************************************
//
// Version 20 , Oct.13 2018
//
// Preparations:
//
// 1)  change the partitiontable (defaut.csv in folder esp32/tools/partitions/)
//     MiniWebRadio need 2.3MByte flash and 200KByte nvs
//
//   # Name,     Type,   SubType,   Offset,   Size,     Flags
//     phy_init, data,   phy,       0x9000,   0x7000,
//     factory,  app,    factory,   0x10000,  0x300000,
//     nvs,      data,   nvs,       0x310000, 0x32000,
//     spiffs,   data,   spiffs,    0x342000, 0xB0000,
//     eeprom,   data,   0x99,      0x3F2000, 0xD000,
//
//     or copy the default.csv from the repository to replace the original one
//
//     Arduino IDE only: set treshold in boards.txt from [boardname].upload.maximum_size=1310720 to 3145728
//
// 2)  set the Timezone mentioned below, examples are in rtime.cpp
//
// 3)  extract the zip file to SD Card
//
// 4)  set WiFi credentials below, more credentials can be set in networks.txt (SD Card)
//
// 5)  change GPIOs if nessessary, e.g ESP32 Pico V4: GPIO16 and 17 are connected to FLASH
//
// 6)  add libraries from my repositories to this project: vs1053_ext, IR and tft
//     TFT controller can be ILI9341 or HX8347D, set tft(0) or tft(1) below
//
// 7)  uncomment #include "fonts/Times_New_Roman.h" in tft.h
//
// 8)  translate _hl_title, entries below, in Your language
//
//
//  Display 320x240
//  +-------------------------------------------+ _yHeader=0
//  | Header                                    |       _hHeader=20px
//  +-------------------------------------------+ _yName=20
//  |                                           |
//  | Logo                   StationName        |       _hName=100px
//  |                                           |
//  +-------------------------------------------+ _yTitle=120
//  |                                           |
//  |              StreamTitle                  |       _hTitle=100px
//  |                                           |
//  +-------------------------------------------+ _yFooter=220
//  | Footer                                    |       _hFooter=20px
//  +-------------------------------------------+ 240
//                                             320
//
// THE SOFTWARE IS PROVIDED "AS IS" FOR PRIVATE USE ONLY, IT IS NOT FOR COMMERCIAL USE IN WHOLE OR PART OR CONCEPT.
// FOR PERSONAL USE IT IS SUPPLIED WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
// WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHOR
// OR COPYRIGHT HOLDER BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE
//
//
// system libraries
#include <Preferences.h>
#include <SPI.h>
#include <SD.h>
#include <FS.h>
#include <WiFiClient.h>
#include <WiFiMulti.h>
#include <Arduino.h>

// own libraries
#include "IR.h"             // see my repository at github "ESP32-IR-Remote-Control"
#include "vs1053_ext.h"     // see my repository at github "ESP32-vs1053_ext"
#include "tft.h"            // see my repository at github "ESP32-TFT-Library-ILI9431-HX8347D"

#include "html.h"
#include "rtime.h"
#include "web.h"

// Digital I/O used
#define VS1053_CS      2  // do not use GPI032 or GPIO33 here
#define VS1053_DCS     4  // do not use GPI032 or GPIO33 here
#define VS1053_DREQ   36
#define TFT_CS        22  // do not use GPI032 or GPIO33 here
#define TFT_DC        21  // do not use GPI032 or GPIO33 here
#define TFT_BL        17  // 33 (pico V4)
#define TP_IRQ        39
#define TP_CS         16  // 32 (pico V4)
#define SD_CS          5
#define IR_PIN        34
#define SPI_MOSI      23
#define SPI_MISO      19
#define SPI_SCK       18

// Timezone
#define TZName  "CET-1CEST,M3.5.0,M10.5.0/3"

String   _SSID = "mySSID";      // Your WiFi credentials here
String   _PW   = "myWiFiPassword";

//global variables
char     _chbuf[1024];
String   _station="",   _stationname="",   _stationURL="",      _homepage="";
String   _title="",     _info="",          _alarmtime="",       _filename="";
String   _time_s="",    _hour="",          _bitrate="",         _mp3Name[10];
String   _pressBtn[5],  _releaseBtn[5],    _myIP="0.0.0.0",     _lastconnectedhost="";
int8_t   _mp3Index=0;           // pointer _mp3Name[]
uint8_t  _releaseNr=0;
uint8_t  _timefile=0;           // speak the time
uint8_t  _commercial_dur=0;     // duration of advertising
uint16_t _sleeptime=0;          // time in min until MiniWebRadio goes to sleep
uint32_t _millis=0;
uint32_t _alarmdays=0;
boolean  f_1sec=false;          // flag is set every second
boolean  f_1min=false;          // flag is set every minute
boolean  f_SD_okay=false;       // true if SD card in place and readable
boolean  f_mute=false;
boolean  f_rtc=false;           // true if time from ntp is received
boolean  f_mp3eof=false;        // set at the end of mp3 file
boolean  f_alarm=false;         // set if alarmday and alarmtime is equal localtime
boolean  f_timespeech=false;    // if true activate timespeech
boolean  f_has_ST=false;        // has StreamTitle?
boolean  f_sleeping=false;      // true if sleepmode
boolean  semaphore=false;
boolean  f_upload=false;        // if true next file is saved to SD

String _hl_title[10]{                           // Title in headline
                "** Internet Radio **",         // "* интернет-радио *"  "ραδιόφωνο Internet"
                "** Internet Radio **",
                "** Internet Eadio **",
                "** Uhr **",                    // Clock "** часы́ **"  "** ρολόι **"
                "** Uhr **",
                "** Helligkeit **",             // Brightness яркость λάμψη
                "** MP3-Player **",             // "** цифрово́й плеер **"
                "** MP3-Player **",
                "" ,                            // Alarm should be empty
                "* Einschlafautomatik *",       // "Sleeptimer" "Χρονομετρητής" "Таймер сна"
};

enum status{RADIO=0, RADIOico=1, RADIOmenue=2, CLOCK=3, CLOCKico=4, BRIGHTNESS=5, MP3PLAYER=6,
            MP3PLAYERico=7, ALARM=8, SLEEP=9};
status _state=RADIO;            //statemaschine

enum request{none=0, saveprefs=1, savefiles=2};
request _req=none;              //requesttype

// display layout
const uint16_t _yHeader =0;                     // yPos Header
const uint16_t _hHeader =20;                    // height Header
const uint16_t _yName  =_yHeader+_hHeader;      // yPos StationName
const uint16_t _hName  =100;                    // height Stationname
const uint16_t _yTitle =_yName+_hName;          // yPos StreamTitle
const uint16_t _hTitle =100;                    // height StreamTitle
const uint16_t _yFooter =_yTitle+_hTitle;       // yPos Footer
const uint16_t _hFooter=20;                     // height Footer
const uint16_t _yVolBar=_yTitle+30;             // yPos VolumeBar
const uint16_t _hVolBar=5;                      // height VolumeBar
const uint16_t _wLogo=96;                       // width Logo
const uint16_t _hLogo=96;                       // height Logo
const uint16_t _yLogo=_yName+(_hName-_hLogo)/2; // yPos Logos
const uint16_t _wBtn=64;                        // width Button
const uint16_t _hBtn=64;                        // height Button
const uint16_t _yBtn=_yVolBar+_hVolBar+10;      // yPos Buttons

//objects
TFT tft(1);                     // parameter:  (0)ILI9341, (1)HX8347D
VS1053 mp3(VS1053_CS, VS1053_DCS, VS1053_DREQ);
hw_timer_t* timer=NULL;         // instance of the timer
HTML web;
Preferences pref;
Preferences stations;
RTIME rtc;
IR ir(IR_PIN);                  // do not change the objectname, it must be "ir"
TP tp(TP_CS, TP_IRQ);
WiFiMulti wifiMulti;

//**************************************************************************************************
//                                D E F A U L T S E T T I N G S                                    *
//**************************************************************************************************
void defaultsettings(){
    String str="";

    Serial.println("set default");
    //
    pref.clear();
    //
    pref.putUInt("brightness",100); // 100% display backlight
    pref.putUInt("alarm_weekday",0); // for alarmclock
    pref.putString("alarm_time","00:00");
    pref.putUInt("ringvolume",21);
    //
    pref.putUInt("volume",12); // 0...21
    pref.putUInt("mute",   0); // no mute
    pref.putUInt("toneha", 0); // BassFreq 0...15
    pref.putUInt("tonehf", 0); // TrebleGain 0...14
    pref.putUInt("tonela", 0); // BassGain 0...15
    pref.putUInt("tonelf", 0); // BassFreq 0...13
    //
    pref.putUInt("station", 1);
    pref.putUInt("sleeptime", 1);
    //
    saveStationsToNVS();
}
boolean saveStationsToNVS(){
    String cy="", str="", str_s="", str_u="", info="";
    uint16_t i=0, j=0, cnt=0;
    // StationList
    File file = SD.open("/stations.txt");
    if(file){  // try to read from SD
        stations.clear();
        str=file.readStringUntil('\n');             // read the headline
        while(file.available()){
            str=file.readStringUntil('\n');         // read the line
            if(str[0]=='*' ) continue;              // ignore this, goto next line
            if(str[0]=='\n') continue;              // empty line
            i=1;  while(str[i]=='\t') i++;          // seek first entry, skip tabs
            j=i;  while(str[j] >= 32) j++;          // end of first entry?
            cy =  str.substring(i, j);              // is country
            cy.trim();
            if(cy.length()==0) {};                  // empty country?
            i=j;  while(str[i]=='\t') i++;          // seek next entry, skip tabs
            j=i;  while(str[j] >= 32) j++;          // end of next entry?
            str_s=str.substring(i, j);
            i=j;  while(str[i]=='\t') i++;          // seek next entry, skip tabs
            j=i;  while(str[j] >= 32) j++;          // end of next entry?
            str_u=str.substring(i, j);
            i=j;  while(str[i]=='\t') i++;          // seek last entry, skip tabs
            j=i;  while(str[j] >= 32) j++;          // end of last entry?
            info =str.substring(i, j);
            if(str_s.length()==0) str_s="empty";
            if(str_u.length()==0) str_u="empty";
            str_s.trim(); str_u.trim();
            cnt++;
            if(cnt==1000){
                Serial.println("No more than 999 entries in stationlist allowed!");
                cnt--; // maxstations 999
                break;
            }
            str=str_s;  // station
            str+="#";
            str+=str_u; // url
            //log_i("cy=%s  str_s=%s  str_u=%s  info=%s", cy.c_str(), str_s.c_str(), str_u.c_str(), info.c_str());
            sprintf(_chbuf, "station_%03d", cnt);
            stations.putString(_chbuf, str);
            sprintf(_chbuf, "info_%03d", cnt);
            if(info.length()>4){  // is reasonable? then save additional info
                info.trim();
                stations.putString(_chbuf, info);
            }
        }
        stations.putLong("stations.size", file.size());
        file.close();
        stations.putUInt("maxstations", cnt);
        Serial.printf("stationlist internally loaded\n");
        Serial.printf("maxstations: %i\n", cnt);
        //log_i("nvs free entries %i", stations.freeEntries());
        return true;
    }
    else return false;
}
boolean ST_rep(){  // if station has no streamtitle: replace streamtitle, seek in info
    uint16_t station=pref.getUInt("station");
    sprintf(_chbuf, "info_%03d", station);
    String str=stations.getString(_chbuf, String("No streamtitle available")); // found probably replacement information
    if(str.length()>5){showTitle(str); return true;}
    return false;
}
//**************************************************************************************************
//                                T F T   B R I G H T N E S S                                      *
//**************************************************************************************************
void setTFTbrightness(uint8_t duty){ //duty 0...100 (min...max)
    ledcAttachPin(TFT_BL, 1);        //Configure variable led, TFT_BL pin to channel 1
    ledcSetup(1, 12000, 8);          // 12 kHz PWM and 8 bit resolution
    ledcWrite(1, duty);
}
inline uint32_t getTFTbrightness(){
    return ledcRead(1);
}
inline uint8_t downBrightness(){
    uint8_t br; br=pref.getUInt("brightness");
    if(br>5) {br-=5; pref.putUInt("brightness", br); setTFTbrightness(br);} return br;
}
inline uint8_t upBrightness(){
    uint8_t br; br=pref.getUInt("brightness");
    if(br<100){br+=5; pref.putUInt("brightness", br); setTFTbrightness(br);} return br;
}
inline uint8_t getBrightness(){
    return pref.getUInt("brightness");
}
//**************************************************************************************************
//                                       A S C I I                                                *
//**************************************************************************************************
const char* UTF8toASCII(const char* str){
    uint16_t i=0, j=0;
    char tab[96]={
          96,173,155,156, 32,157, 32, 32, 32, 32,166,174,170, 32, 32, 32,248,241,253, 32,
          32,230, 32,250, 32, 32,167,175,172,171, 32,168, 32, 32, 32, 32,142,143,146,128,
          32,144, 32, 32, 32, 32, 32, 32, 32,165, 32, 32, 32, 32,153, 32, 32, 32, 32, 32,
         154, 32, 32,225,133,160,131, 32,132,134,145,135,138,130,136,137,141,161,140,139,
          32,164,149,162,147, 32,148,246, 32,151,163,150,129, 32, 32,152
     };
    while((str[i]!=0)&&(j<1020)){
        _chbuf[j]=str[i];
        if(str[i]==0xC2){ // compute unicode from utf8
            i++;
            if((str[i]>159)&&(str[i]<192)) _chbuf[j]=tab[str[i]-160];
            else _chbuf[j]=32;
        }
        else if(str[i]==0xC3){
            i++;
            if((str[i]>127)&&(str[i]<192)) _chbuf[j]=tab[str[i]-96];
            else _chbuf[j]=32;
        }
        i++; j++;
    }
    _chbuf[j]=0;
    return (_chbuf);
}
const char* ASCIItoUTF8(const char* str){
    uint16_t i=0, j=0, uni=0;
    uint16_t tab[128]={
         199, 252, 233, 226, 228, 224, 229, 231, 234, 235, 232, 239, 238, 236, 196, 197,
         201, 230, 198, 244, 246, 242, 251, 249, 255, 214, 220, 162, 163, 165,8359, 402,
         225, 237, 243, 250, 241, 209, 170, 186, 191,8976, 172, 189, 188, 161, 171, 187,
        9617,9618,9619,9474,9508,9569,9570,9558,9557,9571,9553,9559,9565,9564,9563,9488,
        9492,9524,9516,9500,9472,9532,9566,9567,9562,9556,9577,9574,9568,9552,9580,9575,
        9576,9572,9573,9561,9560,9554,9555,9579,9578,9496,9484,9608,9604,9612,9616,9600,
         945, 223, 915, 960, 931, 963, 181, 964, 934, 920, 937, 948,8734, 966, 949,8745,
        8801, 177,8805,8804,8992,8993, 247,8776, 176,8729, 183,8730,8319, 178,9632, 160
    };
    while((str[i]!=0)&&(j<1020)){
        uni=str[i];
        if(uni>=128){uni-=128; uni=tab[uni];}
//            uni=UTF8fromASCII(str[i]);
            switch(uni){
            case   0 ... 127:{_chbuf[j]=str[i]; i++; j++; break;}
            case 160 ... 191:{_chbuf[j]=0xC2; _chbuf[j+1]=uni; j+=2; i++; break;}
            case 192 ... 255:{_chbuf[j]=0xC3; _chbuf[j+1]=uni-64; j+=2; i++; break;}
            default:{_chbuf[j]=' '; i++; j++;}} // ignore all other
    }
    _chbuf[j]=0;
    return _chbuf;
}
//**************************************************************************************************
//                                        T I M E R                                                *
//**************************************************************************************************
void IRAM_ATTR timer1sec() {
    static uint8_t sec=0;
    f_1sec=true;
    sec++;
    if(sec==60){sec=0; f_1min=true;}

}
void IRAM_ATTR timer5(){                               // called every 5ms
    static uint8_t  count1sec=0;
    count1sec++;
    if(count1sec == 200){
        count1sec=0; timer1sec();                      // 1 second passed?
    }
}
void startTimer() {
    timer = timerBegin(0, 80, true); // timer_id = 0; divider=80; countUp = true;
    timerAttachInterrupt(timer, &timer5, true); // edge = true
    timerAlarmWrite(timer, 5000, true); //5 ms
    timerAlarmEnable(timer);
    delay(1000);
}
//**************************************************************************************************
//                                       D I S P L A Y                                             *
//**************************************************************************************************
inline void clearHeader() {tft.fillRect(0, _yHeader, tft.width(), _hHeader, TFT_BLACK);}  // y   0...19
inline void clearStation(){tft.fillRect(0, _yName,   tft.width(), _hName,   TFT_BLACK);}  // y  20...119
inline void clearTitle()  {tft.fillRect(0, _yTitle,  tft.width(), _hTitle,  TFT_BLACK);}  // y 120...219
inline void clearFooter() {tft.fillRect(0, _yFooter, tft.width(), _hFooter, TFT_BLACK);}  // y 220...239
inline void clearDisplay(){tft.fillScreen(TFT_BLACK);}                       // y   0...239
inline uint16_t txtlen(String str) {uint16_t len=0; for(int i=0; i<str.length(); i++) if(str[i]<=0xC2) len++; return len;}

void display_info(const char *str, int ypos, int height, uint16_t color, uint16_t indent){
    tft.fillRect(0, ypos, tft.width(), height, TFT_BLACK);  // Clear the space for new info
    tft.setTextColor(color);                                // Set the requested color
    tft.setCursor(indent, ypos);                            // Prepare to show the info
    tft.print(str);                                         // Show the string
}
void showTitle(String str){
    str.trim();  // remove all leading or trailing whitespaces
    str.replace(" | ", "\n");   // some stations use pipe as \n or
    str.replace("| ", "\n");    // or
    str.replace("|", "\n");
    if(_state!=RADIO) return;
    if(txtlen(str)>  4) f_has_ST=true; else f_has_ST=false;
    tft.setFont(Times_New_Roman43x35);
    if(txtlen(str)> 30) tft.setFont(Times_New_Roman38x31);
    if(txtlen(str)> 45) tft.setFont(Times_New_Roman34x27);
    if(txtlen(str)> 65) tft.setFont(Times_New_Roman27x21);
    if(txtlen(str)>130) tft.setFont(Times_New_Roman21x17);
//    for(int i=0;i<str.length(); i++) log_i("str[%i]=%i", i, str[i]);  // see what You get
    display_info(str.c_str(), _yTitle, _hTitle, TFT_CYAN, 0);
}
void showStation(){
    String str1="", str2="", str3="";
    int16_t idx=0;
    if(_stationname=="") str1=_station;
    else str1=_stationname;
    str2=str1;
    str3=str1;  // now str1, str2 and str3 contains either _station or _stationname
    idx=str1.indexOf('|');
    if(idx>0){
        str2=str1.substring(0, idx); // before pipe
        str2.trim();
        str3=str1.substring(idx+1); // after pipe
        str3.trim();
    }

    tft.setFont(Times_New_Roman43x35);
    if(txtlen(str2)>20) tft.setFont(Times_New_Roman38x31);
    if(txtlen(str2)>32) tft.setFont(Times_New_Roman34x27);
    if(txtlen(str2)>45) tft.setFont(Times_New_Roman27x21);
    if(txtlen(str2)>60) tft.setFont(Times_New_Roman21x17);
    if(txtlen(str2)>90) tft.setFont(Times_New_Roman15x14);
    display_info(str2.c_str(), _yName, _hName, TFT_YELLOW, _wLogo+14);// Show station name

    showTitle("");
    showFooter();

    str2="/logo/" + String(UTF8toASCII(str3.c_str())) +".jpg";
    if(f_SD_okay){
        if(tft.drawJpgFile(SD, str2.c_str(), 0, _yLogo)==false){ // filename mostly given from _stationname exist?
            str2="/logo/" + String(UTF8toASCII(_station.c_str())) +".jpg";
            if(tft.drawJpgFile(SD, str2.c_str(), 0, _yLogo)==false){ // filename given from station exist?
                tft.drawJpgFile(SD, "/unknown.jpg", 1,22);  // if no draw unknown
            }
        }
    }
}
void showHeadlineVolume(uint8_t vol){
    if(_state == ALARM || _state== BRIGHTNESS) return;
    sprintf(_chbuf, "Vol %02d", vol);
    tft.fillRect(183, _yHeader, 67, _hHeader, TFT_BLACK);
    tft.setCursor(183, _yHeader);
    tft.setFont(Times_New_Roman27x21);
    tft.setTextColor(TFT_DEEPSKYBLUE);
    tft.print(_chbuf);
}
void showHeadlineItem(){
    tft.setFont(Times_New_Roman27x21);
    display_info(_hl_title[_state].c_str(), _yHeader, _hHeader, TFT_WHITE, 0);
    if(_state!=SLEEP) showHeadlineVolume(getvolume());
}
void showHeadlineTime(){
    if(_state==CLOCK || _state==CLOCKico || _state==BRIGHTNESS || _state==ALARM || _state==SLEEP) return;
    tft.setFont(Times_New_Roman27x21);
    tft.setTextColor(TFT_GREENYELLOW);
    tft.fillRect(250, _yHeader, 70, _hHeader, TFT_BLACK);
    if(!f_rtc) return; // has rtc the correct time? no -> return
    tft.setCursor(250, 0);
    tft.print(rtc.gettime_s());
}
void showFooter(){  // bitrate stationnumber, IPaddress
    if(_state!=RADIO) return;
    clearFooter();
    if(_bitrate.length()==0) _bitrate="   ";  // if bitrate is unknown
    tft.setFont(Times_New_Roman21x17);
    tft.setTextColor(TFT_GREENYELLOW);
    tft.setCursor(0, _yFooter);
    tft.print("BR:");
    tft.setTextColor(TFT_LAVENDER);
    tft.print(_bitrate.c_str());
    tft.setCursor(60, _yFooter);
    tft.setTextColor(TFT_GREENYELLOW);
    tft.print("STA:");
    tft.setTextColor(TFT_LAVENDER);
    tft.printf("%03d", pref.getUInt("station"));
    tft.setCursor(130, _yFooter);
    tft.setTextColor(TFT_GREENYELLOW);
    tft.print("myIP:");
    tft.setTextColor(TFT_LAVENDER);
    tft.print(_myIP.c_str());
    tft.setCursor(280, _yFooter);
    tft.setTextColor(TFT_GREENYELLOW);
    tft.print("S:");
    if(_sleeptime==0 ) tft.setTextColor(TFT_LAVENDER); else tft.setTextColor(TFT_ORANGE);
    tft.printf("%03d", _sleeptime);
}
void updateSleepTime(){
    if(_sleeptime>0){
        _sleeptime--;
        if(_sleeptime==0){
            setTFTbrightness(0);    // backlight off
            mp3.setVolume(0);       // silence
            f_sleeping=true;        // MiniWebRadio is in sleepmode now
        }
    }
    if(_state==RADIO) showFooter();
}
//**************************************************************************************************
//                                T O N E / S E T  S T A T I O N S                                 *
//**************************************************************************************************
String tone(){
    String str_tone="";
    uint8_t u8_tone[4];
    u8_tone[0]=pref.getUInt("toneha"); u8_tone[1]=pref.getUInt("tonehf");
    u8_tone[2]=pref.getUInt("tonela"); u8_tone[3]=pref.getUInt("tonelf");
    sprintf(_chbuf, "toneha=%i\ntonehf=%i\ntonela=%i\ntonelf=%i\n",u8_tone[0],u8_tone[1],u8_tone[2],u8_tone[3]);
    str_tone=String(_chbuf);
    f_mute=pref.getUInt("mute");
    if(f_mute==false) mp3.setVolume(pref.getUInt("volume"));
    else {mp3.setVolume(0);showHeadlineVolume(0);}
    mp3.setTone(u8_tone);
    return str_tone;
}
String setStation(int16_t stationNr) // -1: previous station, 0: next station, 1...maxstations: set stationNr
{
    uint16_t maxstations=stations.getUInt("maxstations");
    int16_t  station=pref.getUInt("station");

    if     (stationNr==0) {if(++station>maxstations)  station=1;}
    else if(stationNr==-1){if(--station<1) station=maxstations;}
    else    station=stationNr;

    sprintf (_chbuf, "station_%03d", station);// Result of search
    String  content=stations.getString(_chbuf);
    //log_i("content %s", content.c_str());
    _stationname=content.substring(0, content.indexOf("#")); //get stationname
    content=content.substring(content.indexOf("#")+1, content.length()); //get URL
    content.trim();
    _stationURL=content;
    _homepage="";
    pref.putUInt("station", station);
    f_has_ST=false; // will probably be set in ShowStreamtitle
    return content;
}

//**************************************************************************************************
//                                   H A N D L E   R E Q U E S T                                   *
//**************************************************************************************************
void savefile(String request){ //save the uploadfile on SD
    //log_i("request=%s",request.c_str());
    if(request=="end request\n"){
        //log_i("end request seen");
        if(!_filename.startsWith("/")) _filename = "/"+_filename;
        _req=none;                      // reset requesttype
        if(_filename.endsWith("jpg")){
            _filename="/logo"+_filename;
            if(web.uploadB64image(SD, UTF8toASCII(_filename.c_str()))) web.reply("OK");
            else web.reply("failure");
        }
        else{
            if(web.uploadfile(SD, UTF8toASCII(_filename.c_str()))) web.reply("OK");
            else web.reply("failure");
            if(_filename==String("/stations.txt")) saveStationsToNVS();
        }
    }
}
//**************************************************************************************************
//                                       L I S T M P 3 F I L E                                     *
//**************************************************************************************************
String listmp3file(const char * dirname="/mp3files", uint8_t levels=2, fs::FS &fs=SD){
    static String SD_outbuf="";            // Output buffer for cmdclient
    String filename;                       // Copy of filename for lowercase test
    uint8_t index=0;
    if(!f_SD_okay) return "";              // See if known card
    File root = fs.open(dirname);
    if(!root){log_e("Failed to open directory"); return ""; }
    if(!root.isDirectory()){log_e("Not a directory"); return "";}
    SD_outbuf="";
    File file = root.openNextFile();
    while(file){
        if(file.isDirectory()){
            if(levels){
                listmp3file(file.name(), levels -1, fs);
            }
        } else {
            //log_i("FILE: %s, SIZE: %i",file.name(), file.size());
            filename=file.name();
            filename.substring(filename.length()-4).toLowerCase();
            filename=filename.substring(1,filename.length()); // remove first '/'
            if(filename.endsWith(".mp3")){
                filename+="\n";
                if(index<10){_mp3Name[index]=filename; index++;}  //store the first 10 Names
                SD_outbuf+=ASCIItoUTF8(filename.c_str());}
        }
        file = root.openNextFile();
    }
    if(SD_outbuf=="") SD_outbuf+"\n"; //nothing found
    return SD_outbuf;
}
//**************************************************************************************************
//                               C O N N E C T   TO   W I F I                                      *
//**************************************************************************************************
bool connectToWiFi(){
    String s_ssid="", s_password="", s_info="";
    uint16_t i=0, j=0;
    wifiMulti.addAP(_SSID.c_str(), _PW.c_str());                // SSID and PW in code
    if(wifiMulti.run()!=WL_CONNECTED){
        if(f_SD_okay){  // try credentials given in "/networks.txt"
            File file = SD.open("/networks.txt");
            if(file){                                           // try to read from SD
                String str="";
                while(file.available()){
                    str=file.readStringUntil('\n');         // read the line
                    if(str[0]=='*' ) continue;              // ignore this, goto next line
                    if(str[0]=='\n') continue;              // empty line
                    i=1;  while(str[i]=='\t') i++;          // seek first entry, skip tabs
                    j=1;  while(str[j] >= 32) j++;          // end of first entry?
                    s_ssid =  str.substring(i, j);          // SSID
                    s_ssid.trim();
                    i=j;  while(str[i]=='\t') i++;          // seek next entry, skip tabs
                    j=i;  while(str[j] >= 32) j++;          // end of next entry?
                    s_password=str.substring(i, j);         // PW
                    s_password.trim();
                    i=j;  while(str[i]=='\t') i++;          // seek next entry, skip tabs
                    j=i;  while(str[j] >= 32) j++;          // end of next entry?
                    s_info=str.substring(i, j);             // Info
                    s_info.trim();
                    if(s_ssid.length()==0) continue;
                    if(s_password.length()==0) continue;
                    //log_i("s_ssid=%s  s_password=%s  s_info=%s", s_ssid.c_str(), s_password.c_str(), s_info.c_str());
                    wifiMulti.addAP(s_ssid.c_str(), s_password.c_str());
                }
                file.close();
            }
        }
    }
    Serial.println("WiFI_info  : Connecting WiFi...");
    if(wifiMulti.run()==WL_CONNECTED){
        _myIP=WiFi.localIP().toString();
        _SSID = WiFi.SSID();
        Serial.printf("WiFI_info  : WiFi connected\n");
        Serial.printf("WiFI_info  : IP address %s\n", _myIP.c_str());
        Serial.printf("WiFI_info  : connected to %s\n", _SSID.c_str());
        return true;
    }else{
        Serial.printf("WiFi credentials are not correct\n");
        _SSID = ""; _myIP="0.0.0.0";
        return false;  // can't connect to any network
    }
}
//**************************************************************************************************
//                                           S E T U P                                             *
//**************************************************************************************************
void setup(){
    // first set all components inactive
    pinMode(SD_CS, OUTPUT);      digitalWrite(SD_CS, HIGH);
    pinMode(TFT_CS, OUTPUT);     digitalWrite(TFT_CS, HIGH);
    pinMode(TP_CS, OUTPUT);      digitalWrite(TP_CS, HIGH);
    pinMode(VS1053_CS, OUTPUT);  digitalWrite(VS1053_CS, HIGH);
    SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
    Serial.begin(115200); // For debug
    SD.end();       // to recognize SD after reset correctly
    Serial.println("setup      : Init SD card");
    SD.begin(SD_CS);
    delay(100); // wait while SD is ready
    tft.begin(TFT_CS, TFT_DC, SPI_MOSI, SPI_MISO, SPI_SCK, TFT_BL);    // Init TFT interface
    SD.begin(SD_CS, SPI, 16000000);  // fast SDcard set 80000000, must have short SPI-wires
    ir.begin();  // Init InfraredDecoder
    tft.setRotation(3); // Use landscape format
    tp.setRotation(3);
    pref.begin("MiniWebRadio", false);  // instance of preferences for defaults (tone, volume ...)
    stations.begin("Stations", false);  // instance of preferences for stations (name, url ...)
    setTFTbrightness(pref.getUInt("brightness"));
    f_SD_okay=(SD.cardType() != CARD_NONE); // See if known card
    if(!f_SD_okay){
        Serial.println("setup       : SD card not found");
        while(1){};                     // endless loop, MiniWebRadio does not work without SD
    }
    else Serial.println("setup      : found SD card");
    File file=SD.open("/stations.txt");
    if(!file){
        Serial.println("setup       : stations.txt not found");
        while(1){};                     // endless loop, MiniWebRadio does not work without stations.txt
    }
    int size= file.size();
    file.close();
    if(stations.getLong("stations.size") != size) defaultsettings();  // first init
    tft.drawJpgFile(SD, "/MiniWebRadio.jpg", 0, 0); //Welcomescreen
    Serial.println("setup      : Init VS1053");
    mp3.begin(); // Initialize VS1053 player
    if(!mp3.printVersion()) Serial.println("setup      : VS1053 not found");
    else Serial.println("setup      : found VS1053");
    _alarmdays=pref.getUInt("alarm_weekday");
    _alarmtime=pref.getString("alarm_time");
    setTFTbrightness(pref.getUInt("brightness"));
    WiFi.disconnect();
    WiFi.mode(WIFI_STA);
    WiFi.setHostname("MiniWebRadio");
    delay(100);
    if(!connectToWiFi()){
        tft.fillScreen(TFT_BLACK);      // Clear screen
        tft.setFont(Times_New_Roman43x35);
        display_info("can't connect to WiFi, check Your credentials", 20, 220, TFT_YELLOW, 5);
        while(1){};                     // endless loop until reset
    }
    web.begin();
    f_rtc= rtc.begin(TZName);
    tft.fillScreen(TFT_BLACK); // Clear screen
    showHeadlineItem();
    tone();
    mp3.connecttohost(setStation(pref.getUInt("station"))); //last used station
    //mp3.printDetails();
    if(_station=="") showStation(); // if station gives no icy-name display _stationname
    startTimer();
}
//**************************************************************************************************
inline uint8_t downvolume(){
    uint8_t vol; vol=pref.getUInt("volume");
    if(vol>0) {vol--; pref.putUInt("volume", vol); if(f_mute==false) mp3.setVolume(vol);}
    showHeadlineVolume(vol); return vol;
}
inline uint8_t upvolume(){
    uint8_t vol; vol=pref.getUInt("volume");
    if(vol<21){vol++; pref.putUInt("volume", vol); if(f_mute==false) mp3.setVolume(vol);}
    showHeadlineVolume(vol); return vol;
}
inline uint8_t getvolume(){
    return pref.getUInt("volume");
}
inline void mute(){
    if(f_mute==false){f_mute=true; mp3.setVolume(0); showHeadlineVolume(0);}
    else {f_mute=false; mp3.setVolume(getvolume()); showHeadlineVolume(getvolume());}
    pref.putUInt("mute", f_mute);
}
inline void showVolumeBar(){
    uint16_t vol=tft.width()* pref.getUInt("volume")/21;
    tft.fillRect(0, _yVolBar, vol, _hVolBar, TFT_RED);
    tft.fillRect(vol+1, _yVolBar, tft.width()-vol+1, _hVolBar, TFT_GREEN);
}
inline void showBrightnessBar(){
    uint16_t br=tft.width()* pref.getUInt("brightness")/100;
    tft.fillRect(0, 140, br, 5, TFT_RED); tft.fillRect(br+1, 140, tft.width()-br+1, 5, TFT_GREEN);
}
inline String StationsItems(){
    return (String(pref.getUInt("station")) + " " + _stationURL + " " + _stationname);
}
//**************************************************************************************************
//                                M E N U E / B U T T O N S                                        *
//**************************************************************************************************
void changeState(int state){
    if(!f_SD_okay) return;
    _state=static_cast<status>(state);
    switch(_state) {
    case RADIO:{
        showFooter(); showHeadlineItem();
        showStation(); showTitle(_title);
        break;
    }
    case RADIOico:{
        _pressBtn[0]="/btn/Button_Mute_Red.jpg";           _releaseBtn[0]="/btn/Button_Mute_Green.jpg";
        _pressBtn[1]="/btn/Button_Volume_Down_Yellow.jpg"; _releaseBtn[1]="/btn/Button_Volume_Down_Blue.jpg";
        _pressBtn[2]="/btn/Button_Volume_Up_Yellow.jpg";   _releaseBtn[2]="/btn/Button_Volume_Up_Blue.jpg";
        _pressBtn[3]="/btn/Button_Previous_Yellow.jpg";    _releaseBtn[3]="/btn/Button_Previous_Green.jpg";
        _pressBtn[4]="/btn/Button_Next_Yellow.jpg";        _releaseBtn[4]="/btn/Button_Next_Green.jpg";
        clearTitle(); clearFooter(); showVolumeBar();
        break;}
    case RADIOmenue:{
        _pressBtn[0]="/btn/MP3_Yellow.jpg";                _releaseBtn[0]="/btn/MP3_Green.jpg";
        _pressBtn[1]="/btn/Clock_Yellow.jpg";              _releaseBtn[1]="/btn/Clock_Green.jpg";
        _pressBtn[2]="/btn/Radio_Yellow.jpg";              _releaseBtn[2]="/btn/Radio_Green.jpg";
        _pressBtn[3]="/btn/Button_Sleep_Yellow.jpg";       _releaseBtn[3]="/btn/Button_Sleep_Green.jpg";
        _pressBtn[4]="/btn/Bulb_Yellow.jpg";               _releaseBtn[4]="/btn/Bulb_Green.jpg";
        clearTitle(); clearFooter();
        break;}
    case CLOCKico:{
        _pressBtn[0]="/btn/MP3_Yellow.jpg";                _releaseBtn[0]="/btn/MP3_Green.jpg";
        _pressBtn[1]="/btn/Bell_Yellow.jpg";               _releaseBtn[1]="/btn/Bell_Green.jpg";
        _pressBtn[2]="/btn/Radio_Yellow.jpg";              _releaseBtn[2]="/btn/Radio_Green.jpg";
        _pressBtn[3]="/btn/Black.jpg";                     _releaseBtn[3]="/btn/Black.jpg";
        _pressBtn[4]="/btn/Black.jpg";                     _releaseBtn[4]="/btn/Black.jpg";
        break;}
    case BRIGHTNESS:{
        _pressBtn[0]="/btn/Button_Left_Yellow.jpg";        _releaseBtn[0]="/btn/Button_Left_Blue.jpg";
        _pressBtn[1]="/btn/Button_Right_Yellow.jpg";       _releaseBtn[1]="/btn/Button_Right_Blue.jpg";
        _pressBtn[2]="/btn/Button_Ready_Yellow.jpg";       _releaseBtn[2]="/btn/Button_Ready_Blue.jpg";
        _pressBtn[3]="/btn/Black.jpg";                     _releaseBtn[3]="/btn/Black.jpg";
        _pressBtn[4]="/btn/Black.jpg";                     _releaseBtn[4]="/btn/Black.jpg";
        break;}
    case MP3PLAYER:{
        _pressBtn[0]="/btn/Radio_Yellow.jpg";              _releaseBtn[0]="/btn/Radio_Green.jpg";
        _pressBtn[1]="/btn/Button_Left_Yellow.jpg";        _releaseBtn[1]="/btn/Button_Left_Blue.jpg";
        _pressBtn[2]="/btn/Button_Right_Yellow.jpg";       _releaseBtn[2]="/btn/Button_Right_Blue.jpg";
        _pressBtn[3]="/btn/Button_Ready_Yellow.jpg";       _releaseBtn[3]="/btn/Button_Ready_Blue.jpg";
        _pressBtn[4]="/btn/Black.jpg";                     _releaseBtn[4]="/btn/Black.jpg";
        break;}
    case MP3PLAYERico:{
        _pressBtn[0]="/btn/Button_Mute_Red.jpg";           _releaseBtn[0]="/btn/Button_Mute_Green.jpg";
        _pressBtn[1]="/btn/Button_Volume_Down_Yellow.jpg"; _releaseBtn[1]="/btn/Button_Volume_Down_Blue.jpg";
        _pressBtn[2]="/btn/Button_Volume_Up_Yellow.jpg";   _releaseBtn[2]="/btn/Button_Volume_Up_Blue.jpg";
        _pressBtn[3]="/btn/MP3_Yellow.jpg";                _releaseBtn[3]="/btn/MP3_Green.jpg";
        _pressBtn[4]="/btn/Radio_Yellow.jpg";              _releaseBtn[4]="/btn/Radio_Green.jpg";
        break;}
    case ALARM:{
        _pressBtn[0]="/btn/Button_Left_Yellow.jpg";        _releaseBtn[0]="/btn/Button_Left_Blue.jpg";
        _pressBtn[1]="/btn/Button_Right_Yellow.jpg";       _releaseBtn[1]="/btn/Button_Right_Blue.jpg";
        _pressBtn[2]="/btn/Button_Up_Yellow.jpg";          _releaseBtn[2]="/btn/Button_Up_Blue.jpg";
        _pressBtn[3]="/btn/Button_Down_Yellow.jpg";        _releaseBtn[3]="/btn/Button_Down_Blue.jpg";
        _pressBtn[4]="/btn/Button_Ready_Yellow.jpg";       _releaseBtn[4]="/btn/Button_Ready_Blue.jpg";
        break;}
    case SLEEP:{
        _pressBtn[0]="/btn/Button_Up_Yellow.jpg";          _releaseBtn[0]="/btn/Button_Up_Blue.jpg";
        _pressBtn[1]="/btn/Button_Down_Yellow.jpg";        _releaseBtn[1]="/btn/Button_Down_Blue.jpg";
        _pressBtn[2]="/btn/Button_Ready_Yellow.jpg";       _releaseBtn[2]="/btn/Button_Ready_Blue.jpg";
        _pressBtn[3]="/btn/Black.jpg";                     _releaseBtn[3]="/btn/Black.jpg";
        _pressBtn[4]="/btn/Button_Cancel_Yellow.jpg";      _releaseBtn[4]="/btn/Button_Cancel_Blue.jpg";
        break;}
    case CLOCK:{ break;}

    }
    if(_state!=RADIO && _state!=CLOCK){ // RADIO and CLOCK have no Buttons
        int j=0;
        if(_state==RADIOico || _state==MP3PLAYERico){  // show correct mute button
            if(f_mute==false) {tft.drawJpgFile(SD, _releaseBtn[0].c_str(), 0, _yBtn); mp3.loop();}
            else {tft.drawJpgFile(SD, _pressBtn[0].c_str(), 0, _yBtn); mp3.loop();}
            j=1;}
        for(int i=j; i<5; i++){tft.drawJpgFile(SD, _releaseBtn[i].c_str(), i*_wBtn, _yBtn); mp3.loop();}
    }
}
void changeBtn_pressed(uint8_t btnNr){
    if(_state!=RADIO && _state!=CLOCK) tft.drawJpgFile(SD, _pressBtn[btnNr].c_str(), btnNr*_wBtn , _yBtn);
}
void changeBtn_released(uint8_t btnNr){
    if(_state!=RADIO && _state!=CLOCK) tft.drawJpgFile(SD, _releaseBtn[btnNr].c_str(), btnNr*_wBtn , _yBtn);
}
void display_weekdays(uint8_t ad, boolean showall=false){
    uint8_t i=0;
    String str="";
    static uint8_t d, old_d;
    d=ad; //alarmday
    for(i=0;i<7;i++){
        if((d & (1<<i))==(old_d & (1<<i))&&!showall) continue;  //icon is alread displayed
        str="/day/"+String(i);
        if(d & (1<<i))  str+="_rt_en.bmp";    // l<<i instead pow(2,i)
        else            str+="_gn_en.bmp";
        if(f_SD_okay) tft.drawBmpFile(SD, str.c_str(), 5+i*44, 0);
        mp3.loop();
    }
    old_d=ad;
}
void display_alarmtime(int8_t xy=0, int8_t ud=0, boolean showall=false){
    uint8_t i=0, j[4]={5,77,173,245}, k[4]={0,1,3,4}, ch=0;
    String str="";
    static int8_t pos=0, oldpos=0;;
    static String oldt="";
    if(ud==1){
        ch=_alarmtime[k[pos]]; ch++;
        if(pos==0){if(_alarmtime[1]>51){if(ch==50) ch=48; _alarmtime[k[pos]]=ch;} //hour 0...1
                   else{if(ch==51) ch=48; _alarmtime[k[pos]]=ch;} //hour 0...2
        }
        if(pos==1){if(_alarmtime[0]=='2'){if(ch==52) ch=48; _alarmtime[k[pos]]=ch;} //hour*10 0...3
                   else{if(ch==58) ch=48; _alarmtime[k[pos]]=ch;} //hour*10 0...9
        }
        if(pos==2){if(ch==54) ch=48; _alarmtime[k[pos]]=ch;} //min 0...5
        if(pos==3){if(ch==58) ch=48; _alarmtime[k[pos]]=ch;} //min*10 0...9
    }
    if(ud==-1){
        ch=_alarmtime[k[pos]]; ch--;
        if(pos==0){if(_alarmtime[1]>51){if(ch==47) ch=49; _alarmtime[k[pos]]=ch;}//hour 1...0
                   else{if(ch==47) ch=50; _alarmtime[k[pos]]=ch;} //hour 2...0
        }
        if(pos==1){if(_alarmtime[0]=='2'){if(ch==47) ch=51; _alarmtime[k[pos]]=ch;} //hour*10 0...3
                   else{if(ch==47) ch=57; _alarmtime[k[pos]]=ch;} //hour*10 9...0
        }
        if(pos==2){if(ch==47) ch=53; _alarmtime[k[pos]]=ch;} //min 5...0
        if(pos==3){if(ch==47) ch=57; _alarmtime[k[pos]]=ch;} //min*10 9...0
        }

    if(xy==1) pos++; if(pos==4) pos=0; //pos only 0...3
    if(xy==-1)pos--; if(pos==-1)pos=3;

    if(showall==true){oldt="";
        if(f_SD_okay){tft.drawBmpFile(SD,"/digits/ert.bmp", 149, 45);mp3.loop();}}
    String at=_alarmtime;
    //log_i("at=%s",_alarmtime.c_str());
    if(pos!=oldpos){
        str="/digits/"+String(at.charAt(k[pos]))   +"or.bmp";
        if(f_SD_okay){tft.drawBmpFile(SD, str.c_str(), j[pos],    45);mp3.loop();}
        str="/digits/"+String(at.charAt(k[oldpos]))+"rt.bmp";
        if(f_SD_okay){tft.drawBmpFile(SD, str.c_str(), j[oldpos], 45);mp3.loop();}
    }
    for(i=0;i<4;i++){
        if(at[k[i]]!=oldt[k[i]]){
            str="/digits/"+String(at.charAt(k[i]));
            if(i==pos) str+="or.bmp";   //show orange number
            else       str+="rt.bmp";   //show red numbers
            if(f_SD_okay){tft.drawBmpFile(SD, str.c_str(), j[i], 45);mp3.loop();}
        }
    }
    oldt=at; oldpos=pos;
}
void display_time(boolean showall=false){ //show current time on the TFT Display
    static String t, oldt="";
    static boolean k=false;
    uint8_t i=0;
    uint16_t j=0;
    if(showall==true) oldt="";
    if((_state==CLOCK)||(_state==CLOCKico)){
        t=rtc.gettime_s();
        for(i=0;i<5;i++){
            if(t[i]==':'){if(k==false){k=true; t[i]='d';}else{t[i]='e'; k=false;}}
            if(t[i]!=oldt[i]){
                sprintf(_chbuf,"/digits/%cgn.bmp",t[i]);
                if(f_SD_okay) tft.drawBmpFile(SD, _chbuf, 5+j, 45);
                mp3.loop();
            }
            if((t[i]=='d')||(t[i]=='e'))j+=24; else j+=72;
        }
        oldt=t;}
}
void display_sleeptime(int8_t ud=0, boolean ready=false){  // set sleeptimer
    uint8_t p=0, ypos[4]={5,54,71,120};
    String   m[]={"0:00", "0:05", "0:10", "0:15", "0:30", "0:45", "1:00", "2:00", "3:00", "4:00", "5:00", "6:00"};
    uint16_t n[]={    0,      5,     10,     15,     30,     45,     60,    120,    180,    240,    300,    360 };
    String str="", color="rt";
    p=pref.getUInt("sleeptime");
    if(ready==true){
        _sleeptime=n[p];
        return;
    }
    if(ud==1){
        if(p<11) p++;
        pref.putUInt("sleeptime", p);
    }
    if(ud==-1){
        if(p>0) p--;
        pref.putUInt("sleeptime", p);
    }
    if(p==0) color="gn";

    String st=m[p];
    str="/digits/"+String(st.charAt(0))+"s" + color + ".bmp";
    if(f_SD_okay){tft.drawBmpFile(SD, str.c_str(), ypos[0],    48);mp3.loop();}
    str="/digits/ds" + color + ".bmp"; // colon
    if(f_SD_okay){tft.drawBmpFile(SD, str.c_str(), ypos[1],    48);mp3.loop();}
    str="/digits/"+String(st.charAt(2))+"s" + color + ".bmp";
    if(f_SD_okay){tft.drawBmpFile(SD, str.c_str(), ypos[2],    48);mp3.loop();}
    str="/digits/"+String(st.charAt(3))+"s" + color + ".bmp";
    if(f_SD_okay){tft.drawBmpFile(SD, str.c_str(), ypos[3],    48);mp3.loop();}
}
//**************************************************************************************************
//                                           L O O P                                               *
//**************************************************************************************************
void loop() {
    static uint8_t sec=0;
    if(web.loop()) return; // if true: ignore all other for faster response to web
    mp3.loop();
    ir.loop();
    tp.loop();
    if(f_1sec==true){
        if(f_rtc==true){ // true -> rtc has the current time
            int8_t h=0;
            _time_s=rtc.gettime_s();
            if((f_mute==false)&&(!f_sleeping)){
                if(_time_s.endsWith("59:51")) { // speech the time 9 sec before a new hour is arrived
                    _hour=_time_s.substring(0,2); // extract the hour
                    h=_hour.toInt();
                    h++;
                    if(h==24) h=0;
                    _timefile=3;
                    sprintf (_chbuf, "/voice_time/%03d.mp3", h);
                    mp3.connecttoSD(_chbuf);
                }
            }
            showHeadlineTime();
            if(wifiMulti.run()!=WL_CONNECTED) {
                Serial.printf("WiFi not connected! run returned");
            }
            else{
                if(WiFi.localIP().toString()!=_myIP){
                    _myIP=WiFi.localIP().toString();
                    _SSID=WiFi.SSID();
                    Serial.printf("Connected to %s\n", _SSID.c_str());
                    Serial.printf("IP is %s\n", _myIP.c_str());
                    showFooter();
                }
            }
        }
        display_time();
        if((f_has_ST==false)&&(_state==RADIO)) sec++; else sec=0; // Streamtitle==""?
        if(sec>6){
            sec=0;
            ST_rep();
        }
        if(_commercial_dur>0){
            _commercial_dur--;
            if((_commercial_dur==2)&&(_state==RADIO))showTitle("");// end of commercial? clear streamtitle
        }
        f_1sec=false;
    }
    if(f_1min==true){
        updateSleepTime();
        f_1min=false;
    }
    if(_alarmtime==rtc.gettime_xs()){ //is alarmtime
        if((_alarmdays>>rtc.getweekday())&1){ //is alarmday
            if(!semaphore) {f_alarm=true; f_mute=false; semaphore=true;} //set alarmflag
        }
    }
    else semaphore=false;

    if(_millis+5000<millis()){  //5sec no touch?
        if(_state==RADIOico)  {_state=RADIO; showTitle(_title); showFooter();      }
        if(_state==RADIOmenue){_state=RADIO; showTitle(_title); showFooter();      }
        if(_state==CLOCKico)  {display_info("",160,79, TFT_BLACK, 0); _state=CLOCK;}
    }

    if(f_alarm){
        // log_i("Alarm");
        f_alarm=false;
        mp3.connecttoSD("/ring/alarm_clock.mp3");
        mp3.setVolume(21);
        setTFTbrightness(pref.getUInt("brightness"));
    }

    if(f_mp3eof){
        if(_timefile>0){
            if(_timefile==1){mp3.connecttoSD("/voice_time/080.mp3"); _timefile--;}  // stroke
            if(_timefile==2){mp3.connecttoSD("/voice_time/200.mp3"); _timefile--;} // precisely
            if(_timefile==3){mp3.connecttoSD("/voice_time/O'clock.mp3"); _timefile--;}
        }
        else {
            _title="";
            changeState(RADIO);
            mp3.connecttohost(_lastconnectedhost);
            mp3.setVolume(pref.getUInt("volume"));
        }
        f_mp3eof=false;
    }
}
//**************************************************************************************************
//                                            E V E N T S                                          *
//**************************************************************************************************
//Events from vs1053_ext library
void vs1053_showstation(const char *info){              // called from vs1053
    //log_i("_station %s",info);
    if((_station==String(info))&&((String(info)!=""))) return;
    _station=info;
    _title="";
    //avoid double logo paint, show it when last connected host is received
}
void vs1053_showstreamtitle(const char *info){          // called from vs1053
    _title=info;
    showTitle(info);
}
void vs1053_showstreaminfo(const char *info){           // called from vs1053
//    s_info=info;
//    tft.setTextSize(1);                               // host and port
//    displayinfo(s_info.c_str(), 167, 55, TFT_YELLOW); // show info at position 167
}
void vs1053_eof_mp3(const char *info){                  // end of mp3 file (filename)
    f_mp3eof=true;
    Serial.printf("vs1053_eof : %s\n", info);
}
void vs1053_bitrate(const char *br){
    _bitrate=br;
    if(_state==RADIO)showFooter();
}
void vs1053_info(const char *info) {                    // called from vs1053
    String str=info;
    Serial.print("vs1053_info: ");
    if((str.startsWith("Stream lost"))&&(f_rtc)) Serial.print(String(rtc.gettime())+" ");
    Serial.println(info);                                 // all infos
}
void vs1053_commercial(const char *info){               // called from vs1053
    String str=info;                                    // info is the duration of advertising
    _commercial_dur=str.toInt();
    showTitle("Advertising "+str+"s");
}
void vs1053_icyurl(const char *info){                   // if the Radio has a homepage, this event is calling
    String str=info;
    if(str.length()>5){
        _homepage=String(info);
        if(!_homepage.startsWith("http")) _homepage="http://"+_homepage;
        Serial.printf("Homepage   : %s\n", info);
    }
}
void vs1053_lasthost(const char *info){                 // really connected URL
    _lastconnectedhost=String(info);
    showStation();
    Serial.printf("lastURL    : %s\n", info);
}
void RTIME_info(const char *info){
    Serial.printf("rtime_info : %s\n", info);
}

//Events from tft library
void tft_info(const char *info){
    Serial.printf("tft_info   : %s\n", info);
}
//Events from html library
void HTML_command(const String cmd){                    // called from html
    uint8_t vol;
    String  str;
    if(cmd=="homepage"){(web.reply(_homepage)); return;}
    if(cmd=="to_listen"){web.reply(StationsItems()); return;} // return the name and number of the current station
    if(cmd=="gettone"){ web.reply(tone()); return;}
    if(cmd=="getmute"){ web.reply(String(int(f_mute))); return;}
    if(cmd=="test") {sprintf(_chbuf, "free memory: %u, buffer filled: %d, available stream: %d\n", ESP.getFreeHeap(),mp3.ringused(), mp3.streamavail()); web.reply(_chbuf); return;}
    if(cmd=="mute") {mute();if(f_mute==true) web.reply("Mute on\n"); else web.reply("Mute off\n"); return;}
    if(cmd.startsWith("toneha=")){pref.putUInt("toneha",(cmd.substring(cmd.indexOf("=")+1).toInt()));web.reply("Treble Gain set"); tone(); return;}
    if(cmd.startsWith("tonehf=")){pref.putUInt("tonehf",(cmd.substring(cmd.indexOf("=")+1).toInt()));web.reply("Treble Freq set"); tone(); return;}
    if(cmd.startsWith("tonela=")){pref.putUInt("tonela",(cmd.substring(cmd.indexOf("=")+1).toInt()));web.reply("Bass Gain set"); tone(); return;}
    if(cmd.startsWith("tonelf=")){pref.putUInt("tonelf",(cmd.substring(cmd.indexOf("=")+1).toInt()));web.reply("Bass Freq set"); tone(); return;}
    if(cmd.startsWith("mp3list")){web.reply(listmp3file()); return;}
    if(cmd.startsWith("mp3track=")){str=cmd; str.replace("mp3track=", "/"); mp3.connecttoSD(str); web.reply("OK\n"); return;}
    if(cmd.startsWith("saveprefs")) {_req=saveprefs; web.reply("");  return;} // after that starts POST Event "HTML_request"
    if(cmd.startsWith("uploadfile")){_req=savefiles;  _filename=cmd.substring(11);  return;}
    if(cmd.startsWith("upvolume")){ str="Volume is now "; str.concat(vol=upvolume()); web.reply(str); return;}
    if(cmd.startsWith("downvolume")){ str="Volume is now "; str.concat(downvolume()); web.reply(str); return;}
    if(cmd.startsWith("prev_station")){str=setStation(-1); mp3.connecttohost(str); web.reply(StationsItems()); return;}
    if(cmd.startsWith("next_station")){str=setStation(0); mp3.connecttohost(str); web.reply(StationsItems()); return;}
    if(cmd.startsWith("set_station=")){ mp3.connecttohost(str=setStation(cmd.substring(cmd.indexOf("=")+1).toInt())); web.reply(StationsItems()); return;}
    if(cmd.startsWith("stationURL=")){_stationname=""; _title=""; mp3.connecttohost(cmd.substring(cmd.indexOf("=")+1));web.reply("OK\n"); return;}
    if(cmd.startsWith("getnetworks")){web.reply(_SSID+"\n"); return;}
    log_e("unknown HTMLcommand %s", cmd.c_str());
}
void HTML_file(String file){                  // called from html
    web.printhttpheader(file).c_str();
    if(file=="index.html") {web.show(web_html); return;}
    if(file=="favicon.ico"){web.streamfile(SD, "/favicon.ico"); return;}
    if(file.startsWith("SD")){web.streamfile(SD, (file.substring(2).c_str())); return;}
    if(file.startsWith("url=")){  // logo
        file=(file.substring(6));  // remove "url=sd"
        int idx=file.indexOf('|');
        if(idx>0) file=(file.substring(0,6)+ file.substring(idx+1)); // /logo + all after pipe
        web.streamfile(SD, UTF8toASCII(file.c_str())); return;
    }
    log_e("unknown HTMLfile %s", file.c_str());
}
void HTML_request(const String request){
    //log_i("request=%s", request.c_str());
    if(_req==savefiles){savefile(request);  return;}
    if(request=="end request\n"){           return;}
    log_e("unknown request: %s",request.c_str());
}
void HTML_info(String info){                            // called from html
    Serial.printf("HTML_info  : %s\n", info.c_str());    // for debug infos
}
// Events from IR Library
void ir_res(uint32_t res){
    if(_state==RADIO){
        if(res>stations.getUInt("maxstations")){
            tft.setTextSize(7);
            display_info(String(res).c_str(), _yName, _hName +_hTitle, TFT_RED, 100); //state RADIO
            return;
        }
        else{
            _station="";
            mp3.connecttohost(setStation(res));//state RADIO
        }
    }
}
void ir_number(const char* num){
    if(_state==RADIO){
        tft.setTextSize(7);
        display_info(num, _yName, _hName +_hTitle, TFT_YELLOW, 100); //state RADIO
    }
}
void ir_key(const char* key){
    switch(key[0]){
        case 'k':   if(_state==SLEEP) {display_sleeptime(0, true); changeState(RADIO);} //OK
                    break;
        case 'r':   upvolume(); if((_state==RADIOico)||(_state==MP3PLAYERico)) showVolumeBar(); // right
                    break;
        case 'l':   downvolume(); if((_state==RADIOico)||(_state==MP3PLAYERico)) showVolumeBar(); // left
                    break;
        case 'u':   if(_state==RADIO) mp3.connecttohost(setStation(0));  // up
                    if(_state==SLEEP) display_sleeptime(1);
                    break;
        case 'd':   if(_state==RADIO) mp3.connecttohost(setStation(-1)); // down
                    if(_state==SLEEP) display_sleeptime(-1);
                    break;
        case '#':   if(_state==SLEEP) changeState(RADIO); // #
                    else mute();
                    break;
        case '*':   if(_state==RADIO){tft.fillScreen(TFT_BLACK); changeState(SLEEP); showHeadlineItem();
                    tft.drawBmpFile(SD, "/Night_Gown.bmp",198, 25); display_sleeptime();}  // *
                    break;
        default:    break;
    }
}
// Event from TouchPad
void tp_pressed(uint16_t x, uint16_t y){
    uint8_t yPos=255, y1Pos=255, d=0;
    if(f_sleeping==true) return;    // sleepmode, awake in tp_released()
    _millis=millis();
    if(y<167){
        if(_state==RADIOico) changeState(RADIOmenue);
        if(_state==RADIO) changeState(RADIOico);
        if(_state==CLOCK) changeState(CLOCKico);
        if(_state==BRIGHTNESS){}
        if(y<40){
            switch(x){  //weekdays
                case   0 ...  48: y1Pos=0; break; //So
                case  49 ...  92: y1Pos=1; break; //Mon
                case  93 ... 136: y1Pos=2; break; //Tue
                case 137 ... 180: y1Pos=3; break; //We
                case 181 ... 224: y1Pos=4; break; //Th
                case 225 ... 268: y1Pos=5; break; //Fri
                case 269 ... 319: y1Pos=6; break;}//Sat
            }
    }
    else{
        switch(x){  // icons
            case   0 ...  63: yPos=0; break;
            case  64 ... 127: yPos=1; break;
            case 128 ... 191: yPos=2; break;
            case 192 ... 255: yPos=3; break;
            case 256 ... 319: yPos=4; break;
        }
        changeBtn_pressed(yPos);
    }
    if(_state==RADIOico){
        if(yPos==0){mute(); if(f_mute==false) changeBtn_released(yPos);}
        if(yPos==1){_releaseNr= 1; downvolume(); showVolumeBar();} // Vol-
        if(yPos==2){_releaseNr= 2; upvolume(); showVolumeBar();}   // Vol+
        if(yPos==3){_releaseNr= 3; mp3.connecttohost(setStation(-1));} // station--
        if(yPos==4){_releaseNr= 4; mp3.connecttohost(setStation(0)); } // station++
    }
    if(_state==RADIOmenue){
        if(yPos==0){_releaseNr= 5; mp3.stop_mp3client(); listmp3file();} // MP3
        if(yPos==1){_releaseNr= 6;} // Clock
        if(yPos==2){_releaseNr= 7;} // Radio
        if(yPos==3){_releaseNr= 8;} // Sleep
        if(yPos==4){_releaseNr=16;} // Brightness
    }
    if(_state==CLOCKico){
        if(yPos==0){_releaseNr= 5; listmp3file();} // MP3
        if(yPos==1){_releaseNr= 9;} // Bell
        if(yPos==2){_releaseNr= 7;} // Radio
    }
    if(_state==ALARM){
        if(yPos==0){_releaseNr=11;} // left
        if(yPos==1){_releaseNr=12;} // right
        if(yPos==2){_releaseNr=13;} // up
        if(yPos==3){_releaseNr=14;} // down
        if(yPos==4){_releaseNr=15;} // ready (return to CLOCK)

        if(y1Pos<7){d=(1<<y1Pos);
        if((_alarmdays & d))_alarmdays-=d; else _alarmdays+=d; display_weekdays(_alarmdays);}
    }
    if(_state==BRIGHTNESS){
        if(yPos==0){_releaseNr=17;} // left
        if(yPos==1){_releaseNr=18;} // right
        if(yPos==2){_releaseNr= 7;} // ready (return to RADIO)
    }
    if(_state==MP3PLAYER){
        if(yPos==0){_releaseNr=10;} // Radio
        if(yPos==1){_releaseNr=21;} // left
        if(yPos==2){_releaseNr=22;} // right
        if(yPos==3){_releaseNr=23;} // ready
    }
    if(_state==MP3PLAYERico){
        if(yPos==0){mute(); if(f_mute==false) changeBtn_released(yPos);}
        if(yPos==1){_releaseNr=1; downvolume(); showVolumeBar();} // Vol-
        if(yPos==2){_releaseNr=2; upvolume();   showVolumeBar();} // Vol+
        if(yPos==3){_releaseNr=26;} // MP3
        if(yPos==4){_releaseNr=10; _title=""; changeState(RADIO); mp3.connecttohost(_lastconnectedhost);} // Radio
    }
    if(_state==SLEEP){
        if(yPos==0){_releaseNr=19;} // sleeptime up
        if(yPos==1){_releaseNr=20;} // sleeptime down
        if(yPos==2){_releaseNr=7; display_sleeptime(0, true);} // ready, return to RADIO
        if(yPos==4){_releaseNr=7;}  // return to RADIO without saving sleeptime
    }
}
void tp_released(){
    static String str="";
    if(f_sleeping==true){ //awake
        setTFTbrightness(pref.getUInt("brightness"));   // restore brightness
        mp3.setVolume(pref.getUInt("volume"));          // restore volume
        f_sleeping=false;
        return;
    }

    switch(_releaseNr){
    case  1: changeBtn_released(1); break; // Vol-
    case  2: changeBtn_released(2); break; // Vol+
    case  3: changeBtn_released(3); break; // nextstation
    case  4: changeBtn_released(4); break; // previousstation
    case  5: tft.fillScreen(TFT_BLACK);
             changeState(MP3PLAYER); showHeadlineItem();
             tft.setTextSize(4); str=_mp3Name[_mp3Index];
             str=str.substring(str.lastIndexOf("/")+1, str.length()-5); //only filename, get rid of foldername(s) and suffix
             display_info(ASCIItoUTF8(str.c_str()), _yName, _hName, TFT_CYAN, 5); break; //MP3
    case  6: tft.fillScreen(TFT_BLACK); changeState(CLOCK);
             showHeadlineItem(); display_time(true); break;//Clock
    case  7: changeState(RADIO); break;
    case  8: tft.fillScreen(TFT_BLACK); changeState(SLEEP); showHeadlineItem();
             tft.drawBmpFile(SD, "/Night_Gown.bmp",198, 25); display_sleeptime(); break;
    case  9: changeState(ALARM); showHeadlineItem();
             display_weekdays(_alarmdays, true);
             display_alarmtime(0, 0, true); break;
    case 10: _title=""; changeState(RADIO); mp3.connecttohost(_lastconnectedhost); break;
    case 11: display_alarmtime(-1);    changeBtn_released(0);  break;
    case 12: display_alarmtime(+1);    changeBtn_released(1);  break;
    case 13: display_alarmtime(0, +1); changeBtn_released(2);  break; // alarmtime up
    case 14: display_alarmtime(0, -1); changeBtn_released(3);  break; // alarmtime down
    case 15: pref.putUInt("alarm_weekday", _alarmdays); // ready
             pref.putString("alarm_time", _alarmtime);
             tft.fillScreen(TFT_BLACK); changeState(CLOCK);
             showHeadlineItem();
             display_time(true); break;//Clock
    case 16: tft.fillScreen(TFT_BLACK); changeState(BRIGHTNESS); showHeadlineItem();
             showBrightnessBar(); mp3.loop();
             tft.drawBmpFile(SD, "/Brightness.bmp",0, 21); break;
    case 17: changeBtn_released(0); downBrightness(); showBrightnessBar(); break;
    case 18: changeBtn_released(1); upBrightness(); showBrightnessBar(); break;
    case 19: display_sleeptime(1);  changeBtn_released(0); break;
    case 20: display_sleeptime(-1); changeBtn_released(1); break;
    case 21: changeBtn_released(1); _mp3Index--; if(_mp3Index==-1) _mp3Index=9;
             str=_mp3Name[_mp3Index];
             while(str.length()==0){_mp3Index--; str=_mp3Name[_mp3Index]; if(_mp3Index==0) break;}
             str=str.substring(str.lastIndexOf("/")+1, str.length()-5); //only filename, get rid of foldername(s) and suffix
             tft.setTextSize(4);
             display_info(ASCIItoUTF8(str.c_str()), _yName, _hName, TFT_CYAN, 5);
             break; // left file--
    case 22: changeBtn_released(2); _mp3Index++; if(_mp3Index>9) _mp3Index=0;
             str=_mp3Name[_mp3Index];
             if(str.length()==0){_mp3Index=0; str=_mp3Name[_mp3Index];}
             str=str.substring(str.lastIndexOf("/")+1, str.length()-5); //only filename, get rid of foldername(s) and suffix
             tft.setTextSize(4);
             display_info(ASCIItoUTF8(str.c_str()), _yName, _hName, TFT_CYAN, 5);
             break; // right file++
    case 23: changeState(MP3PLAYERico); showVolumeBar();
             mp3.connecttoSD("/"+_mp3Name[_mp3Index]); break; // play mp3file
    case 26: clearTitle(); clearFooter(); changeState(MP3PLAYER); break;
    }
    _releaseNr=0;
}
