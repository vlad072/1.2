#include <avr/wdt.h>
#include <SoftwareSerial.h>
#include <OneWire.h>
#include <EEPROM.h>
#include "data.h"
#include "classes.h"

SoftwareSerial modem(SIM800TX_PIN, SIM800RX_PIN);
OneWire ds(ONEWIRE_PIN);
Input  door(DOOR_PIN), hood(HOOD_PIN), pump(PUMP_PIN);
Output ign(IGN_PIN), lock(LOCK_PIN), unlock(UNLOCK_PIN), siren(SIREN_PIN), flash(FLASH_PIN), dvr(DVR_PIN), led(LED_PIN);
InOut  starter(STARTER_SUPPLY_PIN, STARTER_PIN);
Adc    vbatt(BATT_PIN, 47.83f), ntrl(NEUTRAL_PIN, 200.48f);

void(*reboot)(void) = 0;

void dbg(const char* par, const char* val = NULL) {
  #if DEBUG_LVL & 2
  debug.print(par);
  if (val) debug.print("="), debug.print(val);
  debug.println();
  #endif
}

bool neutral() {
  return ntrl.value() < 0.35f;
}
bool engrun() {
  return pump.active() || (vbatt.value() > 13.4f);
}
bool drive() {
  return engrun() && !neutral();
}
void starting() {
  if ( warmup || engrun() || !neutral() || ((warmtemp - temps[CUR][ENG]) < 5) || (vbatt.value() < 10.5f) ) return;
  ign.set(true); delay(250);
  if (starter.active()) goto _fin;                         // mosfet breakdown
  delay(3000); wdt_reset();
  float _vrelease = vbatt.value() - 1.7f;
  starter.set(true); delay(500);
  for (uint32_t _t = millis(); (millis() - _t) < 3000ul;) if (vbatt.value() > _vrelease) break; // spinup engine - max 3.0sec
  wdt_reset(); starter.set(false); delay(100); if (starter.active()) goto _fin;
  delay(3000); wdt_reset();
  warmup = engrun();
  _fin:
  if (warmup) warmtimer = 20; else ign.set(false);
  pump.change(); EIFR = EIFR; t1min = millis();
}

void modemreset() {
  dbg("reset modem...");
  modem.println(F("ATZ+CFUN=1,1")); modem.find("OK\r\n");  // user profile recovery; reboot modem 
  celstate = 0; tsend = tresp = millis();
}
void modemreconf() {
  dbg("modem reconf...");
  modem.print  (F("AT&F+IPR="));  modem.println(UART_BPS, DEC); modem.find("OK\r\n"); // usart speed
  modem.print  (F("AT+BTHOST=")); modem.println(cid);           modem.find("OK\r\n"); // blue host name
  modem.println(F("AT+CMIC=0,15;+BTPAIRCFG=1;+CLCC=1;E0&W"));   modem.find("OK\r\n");
}
void modemshut() {
  modem.println(F("AT")); if (!modem.find("OK\r\n")) modem.write(0x1A), modem.find("\r\n");
  modem.println(F("AT+CIPHEAD=1;+CIPQSEND=1;+CIPSHUT"));
  tsend = tresp = millis();
}
void play(const char* track) {
  if (celstate != 0x06) return; dbg("play", track);
  celstate = 0x02;
  modem.print(F("AT+CREC=4,C:\\User\\")); modem.print(track); modem.println(F(".amr,0,90")); modem.find("OK\r\n");
}
bool sendstart() {
  modem.setTimeout(300);
  modem.println(F("AT+CIPSEND"));
  bool _ret = modem.find("> ");
  modem.setTimeout(DEF_TIMEOUT);
  return _ret; 
}
bool sendfin() {
  modem.setTimeout(300);
  modem.write(0x1A);
  bool _ret = modem.find("DATA ACCEPT:");
  modem.setTimeout(DEF_TIMEOUT);
  return _ret;
}
void pub(const char* topic, const char* msg, bool retn = false) { 
  modem.write(0x30 | (byte)retn);
  modem.write(2+strlen(topic) + strlen(msg));
  modem.write((byte)0x00), modem.write(strlen(topic)), modem.write(topic);
  modem.write(msg);
}
void pubf(const char* topic, float val, byte dap) {
  char _st[8] = "";
  dtostrf(val, 0, dap, _st); pub(topic, _st);
}
void sub(const char* topic) {
  modem.write(0x82);
  modem.write(strlen(topic) + 5); 
  modem.write((byte)0x00); modem.write(0x01)  ;
  modem.write((byte)0x00); modem.write(strlen(topic)); modem.write(topic);
  modem.write((byte)0x00);
}
void brokercon () {
  dbg("broker connect...");
  if (!sendstart()) return;
  modem.write(0x10);
  modem.write( 4 + 2+strlen(proto) + 2+strlen(cid) + 2+strlen(willt) + 2+1 + 2+strlen(user()) + 2+strlen(pass()) );
  modem.write((byte)0x00); modem.write(strlen(proto));  modem.write(proto); modem.write(ver);
  modem.write(0xE6); modem.write((byte)0x00); modem.write(60);               // flags, ka timout | E/C -retain/not
  modem.write((byte)0x00); modem.write(strlen(cid));    modem.write(cid);
  modem.write((byte)0x00); modem.write(strlen(willt));  modem.write(willt);  // will topic
  modem.write((byte)0x00); modem.write(0x01);           modem.write('0');    // will message
  modem.write((byte)0x00); modem.write(strlen(user())); modem.write(user());
  modem.write((byte)0x00); modem.write(strlen(pass())); modem.write(pass());
  sendfin();
}
//void ping() {
//  if (sendstart()) modem.write(0xC0), modem.write(byte(0x00)), sendfin();
//}
void pubbalance(const char* args) {
  if (!args) return; byte _i = 0; char _bal[8] = "";
  for (;; _i += 4) {
    if (!args[_i]) return;
    if (!strncmp(args+_i, "003", 3) && isdigit(args[_i+3])) break;
    if (!strncmp(args+_i, "002D", 4)) break;
  }
  for (;strlen(_bal)+1 < sizeof(_bal); _i += 4)
    if      (!strncmp(args+_i, "002D", 4))                       strcat (_bal, "-");
//    else if (!strncmp(args+_i, "002E", 4))                       strcat (_bal, "."); // float value
    else if (!strncmp(args+_i, "003" , 3) && isdigit(args[_i+3])) strncat(_bal, args+_i+3, 1);
    else break;
  pub("inf/balance", _bal, true);
}
void publocate(const char* args) {
  if (!args || (args[0] != '0')) return;
  char* _eptr = strchr(args, ',') + 1;
  char* _nptr = strchr(_eptr, ',') + 1;
  byte  _elen = _nptr - _eptr - 1;
  byte  _nlen = strchr(_nptr, ',') - _nptr;
//  char  _link[64] = "https://www.google.com/maps/place/";
  char _link[24] = "";
  strncat(_link, _nptr, _nlen+1); strncat(_link, _eptr, _elen);
  pub("inf/place", _link);
}
void pubalarm() {
  char _sign[10] = "F I D H -";
  if (alarm & 0x03) _sign[8] = alarm & 0x02 ? '2' : '1';
  for (byte _i = 2; _i <= 5; _i++) if (!bitRead(alarm, _i)) _sign[10-_i*2] = '-';
  pub("inf/alarm", _sign);
}
void notify(char* txt, byte mode = PUSH) {
  if (mode & PUSH) if (sendstart()) {
    pub("notify", txt);
    if (sendfin()) return;
  }
  if (mode & SMS) strcpy(msg, txt), modem.println(F("AT+CPBF=\"admin\""));  
}
void locking(bool lk) {
  if (setupmode) return;
  if (lk) {                   // lock
    bool _opened = door.active() || hood.active();
    notify(_opened ? "VEHICLE ISN'T CLOSED!" : "ARMED", _opened ? PUSH+SMS : PUSH);
    if (_opened) siren.twitch(100ul, 3); else if (secure & 0x02) siren.twitch(2ul);
    lock.twitch(500ul, 2); flash.twitch(1200ul);
    secure |= 0x81;
    door.change(); hood.change(); EIFR = EIFR; alarm = 0;
  } else {                    // unlock
    if (secure & 0x02)
      if      (alarm & 0x3C)  siren.twitch(50ul, 3);
      else if (alarm & 0x02)  siren.twitch(50ul, 2);
      else if (alarm & 0x01)  siren.twitch(50ul, 1);
      else                    siren.twitch(2ul);
    notify("DISARMED");
    unlock.twitch(300ul); flash.twitch(350ul);
    secure &= ~0x80;
  }
  if (sendstart()) pub("inf/lock", secure & 0x80 ? "1" : "0"), pub("inf/shock", secure & 0x01 ? "1" : "0"), sendfin();
}
void exec(const char* topic, const char* payl) { //++++++++++++++  USER COMMAND IMPLEMENTAION  ++++++++++++++++++
  dbg(topic, payl);
  if        (!strcmp(topic, "wutm"))  {   //*************** warmup timer **************
    warmtimer = atoi(payl);
    if (sendstart()) pubf("inf/wutm", warmtimer, 0), sendfin();
  } else if (!strcmp(topic, "wutp"))  {   //*************** warmup temperature ********
    warmtemp = atoi(payl);
    if (sendstart()) pubf("inf/wutp", warmtemp, 0), sendfin();
  } else if (!strcmp(topic, "start")) {   //*************** starting engine ***********
    if (payl[0] > '0') starting();
    else               ign.set(false), warmup = false; 
    if (sendstart()) pub("inf/warmup", warmup ? "1" : "0"), pub("inf/engr", engrun() ? "1" : "0"), pubf("inf/wutm", warmtimer, 0), pubf("inf/wutp", warmtemp, 0), sendfin();
  } else if (!strcmp(topic, "lock"))  {   //*************** locking *******************
    locking(payl[0] > '0');
  } else if (!strcmp(topic, "shock")) {   //*************** enable/dis shock sensor ***
    if (payl[0] > '0') secure |=  0x01, EIFR = EIFR;
    else               secure &= ~0x01;
    if (sendstart()) pub("inf/shock", secure & 0x01 ? "1" : "0"), sendfin();
  } else if (!strcmp(topic, "siren")) {   //*************** siren active **************
    if (payl[0] > '0') secure |=  0x02;
    else               secure &= ~0x02;
    if (sendstart()) pub("inf/siren", secure & 0x02 ? "1" : "0"), sendfin();
  } else if (!strcmp(topic, "sms")) {      //************** sms notify bang warning ***
    smsnotify = payl[0] > '0';
    if (sendstart()) pub("inf/sms", smsnotify ? "1" : "0"), sendfin();
//  } else if (!strcmp(topic, "keepc")) {    //*************** keep connect **************
//    keepconn = payl[0] > '0';
//    if (sendstart()) pub("inf/keepc", keepconn ? "1" : "0"), sendfin();
  } else if (!strcmp(topic, "btpwr")) {    //*************** blue power on/off ********
    modem.setTimeout(3900); wdt_reset();
    modem.print(F("AT+BTPOWER=")); modem.println(payl[0] > '0' ? "1" : "0"); modem.find("OK\r\n");
    modem.setTimeout(DEF_TIMEOUT); wdt_reset();
    if (payl[0] > '0') modem.println(F("AT+BTVIS=0")), modem.find("OK\r\n");
    modem.println(F("AT+BTSTATUS?"));
  } else if (!strcmp(topic, "btansw")) {   //************ hfp auto-answer *************
    btansw = payl[0] > '0';
    if (sendstart()) pub("inf/btansw", btansw ? "1" : "0"), sendfin();
  } else if (!strcmp(topic, "alarm")) {    //***************** alarms *****************
    switch (payl[0]) {
      case '1': alarm = 0; if (sendstart()) pubalarm(), sendfin();    // clear
      case '2': siren.set(false); flash.set(false); break;            // mute
      case '3': if (secure & 0x02) siren.twitch(500ul, 3); flash.twitch(3000ul); // raise
    }
  } else if (!strcmp(topic, "upd"))   {    //*************** send inform to dash *******
    switch (payl[0]) {
      case '1': modem.println(F("AT+SAPBR=2,1")); break;                        // locate
      case '2': modem.print(F("AT+CUSD=1,")); modem.println(payl+1); break;     // balance
      case '3': if (sendstart()) {                                              // common
//        pub ("inf/keepc", keepconn ?  "1" : "0");                 
        pubf("inf/outtp" , temps[CUR][OUTS], 0);
        pubf("inf/engtp" , temps[CUR][ENG] , 0);
        pubf("inf/pcbtp" , temps[CUR][PCB] , 0);
        pubf("inf/vehtp" , temps[CUR][VEH] , 0);
        pubf("inf/vbatt" , vbatt.value()   , 2);                          
        pub ("inf/engr"  , engrun()          ?  "1" : "0");       
        pub ("inf/warmup", warmup            ?  "1" : "0");
        pubf("inf/wutp"  , warmtemp,  0);
        pubf("inf/wutm"  , warmtimer, 0);
        pub ("inf/drop"  , door.active()     ?  "1" : "0");
        pub ("inf/hdop"  , hood.active()     ?  "1" : "0");
        pub ("inf/lock"  , secure & 0x80     ?  "1" : "0");
        pub ("inf/shock" , secure & 0x01     ?  "1" : "0");
        pub ("inf/siren" , secure & 0x02     ?  "1" : "0");
        pub ("inf/sms"   , smsnotify         ?  "1" : "0");
        pub ("inf/btansw", btansw            ?  "1" : "0");
        pubalarm();
        if (sendfin()) modem.println(F("AT+CSQ"));
      };
    } // swith update tabs
  } else dbg("unkn cmd!"); // update dash  
}
void ipd( const byte* frame, byte len = 1 ) { //++++++++++++++  PROCESSING DATA FROM BROKER  ++++++++++++++++++
  for (byte _ofs = 0; _ofs < len;) {
    if ( (frame[_ofs] == 0xD0) && (frame[_ofs+1] == 0x00) ) {            // pingresp
      dbg("pingresp");
    } else  if (frame[_ofs] == 0x20) {                                   // connak
      if (frame[_ofs+3] == 0x00) {
        if (sendstart()) pub(willt, "1", true), sub("cmd/#"), sendfin();
        dbg("connakOK");// flash.twitch(350ul);
      } else {
        dbg("connak refused");
        modemshut(); return;
      }
    } else  if (frame[_ofs] == 0x90) {                                   // suback
      dbg("suback");
    } else if (frame[_ofs] == 0x30) {                                    // pub recv
      char* _tptr = (char*)(frame + _ofs + 8); // +4 = full topic
      byte  _tlen = frame[_ofs+3] - 4;         // -0 = full topic length
      char* _pptr = (char*)(frame + _ofs + frame[_ofs+3] + 4) ;
      byte  _plen = frame[_ofs+1] - frame[_ofs+3] - 2;
      char  _topic[_tlen+1]; strncpy(_topic, _tptr, _tlen); _topic[_tlen] = 0;
      char  _payl [_plen+1]; strncpy(_payl,  _pptr, _plen); _payl [_plen] = 0;
      exec(_topic, _payl);
    } //else break; //unexpected header
    _ofs += 2 + frame[_ofs+1];
  } // cycle
}  // ipd func
void btspp( const char* msg, byte len ) { //+++++++++++++++++++ BLUETOOTH SETUP THROUGH SPP  ++++++++++++++++++
  char* _paramptr = strchr(msg, '=') + 1; byte _paramlen = len - (_paramptr - msg);
  if        ( !strncmp(msg, "srv", 3) && (_paramlen < 0x20) ) {
    for (byte _i = 0; _i < _paramlen; _i++) EEPROM.update(_i, _paramptr[_i]);
    EEPROM.update(_paramlen, 0);
    modem.println(F("AT+BTSPPSEND")); if (modem.find("> ")) modem.println(F(" ok")), modem.write(0x1A), modem.find("SEND OK\r\n");
  } else if ( !strncmp(msg, "usr", 3) && (_paramlen < 0x10) ) {
    for (byte _i = 0; _i < _paramlen; _i++) EEPROM.update(_i+0x20, _paramptr[_i]);
    EEPROM.update(_paramlen+0x20, 0);
    modem.println(F("AT+BTSPPSEND")); if (modem.find("> ")) modem.println(F(" ok")), modem.write(0x1A), modem.find("SEND OK\r\n");
  } else if ( !strncmp(msg, "pwd", 3) && (_paramlen < 0x10) ) {
    for (byte _i = 0; _i < _paramlen; _i++) EEPROM.update(_i+0x30, _paramptr[_i]);
    EEPROM.update(_paramlen+0x30, 0);
    modem.println(F("AT+BTSPPSEND")); if (modem.find("> ")) modem.println(F(" ok")), modem.write(0x1A), modem.find("SEND OK\r\n");
  } else if ( !strncmp(msg, "sens", 4) && isdigit(_paramptr[0]) ) {   // ============ sensors ==============
    byte _sid[8]; bool _found = false; byte _idx = _paramptr[0] - '0';
    while (ds.search(_sid) && !_found) {
      _found = _idx == PCB;
      for (byte _i = 0; _i < 8; _i++) _found |= _sid[_i] != sid(PCB)[_i];
    } ds.reset_search();
    bool _crc = true; //OneWire::crc8(_sid, 7) == _sid[7];
    if (_found && _crc) {
      EEPROM.put(0x40 + 8*_idx, _sid);                                                              // sensor id to eeprom
      ds.reset(); ds.select(_sid); ds.write(0x4E); ds.write(0x7F), ds.write(0xFF); ds.write(0x1F);  // init dallas regs  // CHECK!
      ds.reset(); ds.select(_sid); ds.write(0x48);                                                  // save to dallas rom
    }
    modem.println(F("AT+BTSPPSEND")); if (modem.find("> ")) {
      modem.print(_found ? F(" ds[") : F(" ds not found!"));
      if (_found) {
        modem.print(_idx, DEC); modem.print(F("]="));
        for (byte _i = 0; _i < 8; _i++) { if (_sid[_i] < 0x10) modem.write('0'); modem.print(_sid[_i], HEX); modem.write(' '); }
        modem.print(_crc ? F("ok") : F("crc wrong!"));
      }
      modem.println(); modem.write(0x1A), modem.find("SEND OK\r\n");
    }                                                                 // ============ sensors ==============
  } else if (!strncmp(msg, "btpin", 5) && (_paramlen == 4)) {
    modem.println(F("AT+BTSPPSEND")); if (modem.find("> ")) modem.println(F(" reconnect again...")), modem.write(0x1A), modem.find("SEND OK\r\n");
    modem.println(F("AT+BTUNPAIR=0")); modem.find("OK\r\n");
    modem.print(F("AT+BTPAIRCFG=1,")), modem.println(_paramptr); modem.find("OK\r\n");
  } else if (msg[0] == '?') {
    modem.println(F("AT+BTSPPSEND")); if (modem.find("> ")) {
      modem.println();
      modem.println(F("srv=<addr,port>")); 
      modem.println(F("usr=<username>"));
      modem.println(F("pwd=<password>"));
      modem.println(F("sens=<N> : attach 0-pcb,1-eng,2-outside,3-vehcl"));
      modem.println(F("btpin=<NNNN> : bt dev's detach & upd pin"));
      modem.println(F("exit"));
      modem.println(F("-=hit the car to shock test=-"));
      modem.write(0x1A); modem.find("SEND OK\r\n");
    }
  } else if ( !strncmp(msg, "exit", 4) ) {
    modem.println(F("AT+BTVIS=0")); modem.find("OK\r\n");
    modem.println(F("AT+BTSPPSEND")); if (modem.find("> ")) modem.println(F("setup over!")), modem.write(0x1A), modem.find("SEND OK\r\n");
    storedpg = 0; setupmode = false; led.set(false);
  } else {
    modem.println(F("AT+BTSPPSEND")); if (modem.find("> ")) modem.println(F(" error!")), modem.write(0x1A), modem.find("SEND OK\r\n");
  }
}
void dtmf(const char cmd) { //+++++++++++++++++++ ONCALL KEYPRESS HANDLING  ++++++++++++++++++
  dbg("dtmf", cmd);
  if (cmd == '#') {                                                             // emergency recovery
    wdt_disable();
    if (celstate == 0x06) modem.setTimeout(5000), play("recov"), modem.find("+CREC: 0");
    modem.println(F("ATH")); modem.find("OK\r\n");
    if (sendstart()) pub(willt, "0", true), sendfin();
    modemreconf(); reboot();
  } else if (cmd == '2') {                                                      // keep broker connection
    keepconn = !keepconn; play(keepconn ? "keep" : "nkeep");
  } else if (cmd == '3') {                                                      // shock sensor enble
    secure ^= 0x01; play(secure & 0x01 ? "shken" : "shkdis");
  } else if (cmd == '5') {                                                      // dvr run to 1min
    if (dvr.on()) dvr.set(false); else dvr.twitch(120000ul);
    play(dvr.on() ? "dvron" : "dvroff");
  } else if (cmd == '0') { dbg("setup");                                        // setup mode
    modem.setTimeout(3500);
    modem.println(F("AT+BTPOWER=1")); modem.find("OK\r\n");  wdt_reset();
    modem.println(F("AT+BTVIS=1"));
    if (modem.find("OK\r\n")) setupmode = true, led.set(true);
    play(setupmode ? "setup" : "error");
    modem.setTimeout(DEF_TIMEOUT);
  } else {
    play("error"); // unknown command
  }    
}
void athandling() { //++++++++++++++++++ AT RESPONSES HANDLING ++++++++++++++++++++
//  while (modem.available()) if (modem.peek() < '+') modem.read(); else break;                
//  if (!modem.available()) return;
  byte _atlen = 0; char* _ptr = NULL;
  for (uint32_t _trecv = millis(); (millis() - _trecv) < 10ul;) {
    if (!modem.available()) continue;
    char _ch = modem.read(); _trecv = millis();
    if ((_atlen+1) < sizeof(at)) at[_atlen++] = _ch;
  } at[_atlen] = 0;
  #if DEBUG_LVL & 1
  for (byte _i = 0; _i < _atlen; _i++) debug.write(at[_i]);
  #endif
  if (strstr(at, "SHUT OK")) if (keepconn && !setupmode) modem.print(F("AT+CIPMUX=0;+CIPSTART=TCP,")), modem.println(broker());
  if (strstr(at, "CONNECT OK")) brokercon();
  if (strstr(at, "SMS Ready")
   || strstr(at, "DEACT")
   || strstr(at, "CLOSED")
   || strstr(at, "CONNECT FAIL")) dbg("reconn!"), modemshut();
  _ptr = strstr(at, "+SAPBR:"); if (_ptr) switch (_ptr[10]) {
    case '1': modem.println(F("AT+CIPGSMLOC=1,1")); break;
    case '3': modem.println(F("AT+SAPBR=1,1"));
  }
  _ptr = strstr(at, "+CLCC: 1,"); if (_ptr) {
    switch (_ptr[11]) {
      case '0': siren.set(false); flash.set(false); celstate = 0x06; play("hello"); break;
      case '2':
      case '3': celstate = 0x01; break;
      case '6':
      case '7': celstate = 0; tsend = tresp = millis() - 23000ul; break;
      case '4': modem.println(strstr(_ptr+17, "\"admin\"") ? F("AT+DDET=1,500,0;A") : F("AT+CHUP"));
    } t20sec = millis();
  }
  if (strstr(at, "+CREC: 0")) celstate |= 0x04;
  if (strstr(at, "+CPAS: 0")) celstate  = 0;
  _ptr = strstr(at, "+DTMF:"); if (_ptr) dtmf(_ptr[7]);
  _ptr = strstr(at, "+BTCONNECTING:"); if (_ptr) if (setupmode && strstr(_ptr+15, "\"SPP\"")) modem.println(F("AT+BTACPT=1"));
  _ptr = strstr(at, "+BTCONNECT:"); if (_ptr)
    if  (setupmode && strstr(_ptr+12, "\"SPP\"")) { 
      modem.println(F("AT+BTSPPGET=0")); modem.find("OK\r\n");
      modem.println(F("AT+BTSPPSEND"));
      if (modem.find("> ")) modem.println(F("Welcome to the BlackBox setup!")), modem.write(0x1A), modem.find("SEND OK\r\n");
    } else {
      if ( (secure & 0x80) && (_ptr[12] > '0') && (!engrun() || warmup) ) locking(false);
      if (sendstart()) {
        pub("inf/lock", secure & 0x80 ? "1" : "0"); pub("inf/shock", secure & 0x01 ? "1" : "0");
        if (sendfin()) modem.println(F("AT+BTSTATUS?"));
      }
    }
  if    (strstr(at, "+BTDISCONN:")) {
    if (!(secure & 0x80) && !pump.active() && !setupmode) locking(true);
    if (sendstart()) {
      pub("inf/lock", secure & 0x80 ? "1" : "0"); pub("inf/shock", secure & 0x01 ? "1" : "0");
      if (sendfin()) modem.println(F("AT+BTSTATUS?"));
    }
  }
  _ptr = strstr(at, "+BTPAIR:");  if (_ptr) {
    char _msg[20] = "BT ADD: ";
    char* _nptr = strchr(_ptr, '"')+1; byte _nlen = constrain(strchr(_nptr, '"') - _nptr, 0, 11);
    strncat(_msg, _nptr, _nlen);
    notify(_msg);
  }
  _ptr = strstr(at, "+BTSTATUS:");  if (_ptr) {
    byte _pair = 0; byte _conn = 0; char* _bptr = _ptr+12;
    while(true) {
      _bptr = strstr(_bptr, "\r\nP: "); if (!_bptr) break; _pair++; _bptr++;
    } _bptr = _ptr+12; while(true) {
      _bptr = strstr(_bptr, "\r\nC: "); if (!_bptr) break; _conn++; _bptr++;
    }
    char _constr[4] = {_conn<10 ? _conn+48 : _conn+55, '/', _pair<10 ? _pair+48 : _pair+55, 0};
    if (sendstart()) pub("inf/btpwr", _ptr[11] > '0' ? "1" : "0"), pub("inf/btcon", _constr), pub(willt, "1"), sendfin();
  }
  if (strstr(at, "BTRING") && btansw) if ((millis() - tbtring) > 8000ul) tbtring = millis(); else tbtring = 0, modem.println(F("AT+BTATA")); // with 2nd ring answer
//  if (strstr(at, "BTRING") && btansw) if ((millis() - tbtring) > 13000ul) tbtring = millis(); else if ((millis() - tbtring) > 8000ul) tbtring = 0, modem.println(F("AT+BTATA")); // with 3rd ring answer
  if    (strstr(at, "+CMTI:")) modem.println(F("AT+CMGDA=6"));//, modem.find("OK\r\n"); // wipe sms
  _ptr = strstr(at, "+CPBF:");  if (_ptr) {                                             // recv ph.num "admin" then send sms
    modem.println("AT+CMGF=1"); if (modem.find("OK\r\n")) {
      char* _nptr = strchr(_ptr+7, '"');
      modem.print(F("AT+CMGS="));
      while (_nptr[0]) {
        modem.write(_nptr++[0]); if (_nptr[0] == '"') break;
      } modem.println('"');
      if (modem.find("> ")) modem.print(msg), modem.write(0x1A);
    }
  }
  _ptr = strstr(at, "+CUSD:"); if (_ptr) if (sendstart()) pubbalance(_ptr+11), sendfin();
  _ptr = strstr(at, "+CSQ:");  if (_ptr) {
    char* _qptr = _ptr + 6;
    char _sq[3] = ""; strncat(_sq, _qptr, strchr(_qptr, ',') - _qptr);
    if (sendstart()) {
      pub("inf/sq", _sq);
      if (sendfin()) modem.println(F("AT+BTSTATUS?"));
    }
  }
  _ptr = strstr(at, "+CIPGSMLOC:"); if (_ptr) if (sendstart()) publocate(_ptr+12), sendfin();
  _ptr = strstr(at, "+IPD");        if (_ptr) tresp = millis(), ipd( strchr(_ptr + 6, ':') + 1, atoi(_ptr + 5) );
  _ptr = strstr(at, "+BTSPPDATA:"); if (_ptr) btspp( strchr(strchr(_ptr + 11, ',') + 1, ',') + 1, atoi(strchr(_ptr + 11, ',') + 1) );
}
void dsinit() {
  ds.reset(); ds.write(0xCC); ds.write(0x4E); ds.write(0x7F), ds.write(0xFF); ds.write(0x1F);
//  ds.reset(); ds.write(0xCC); ds.write(0x48); // to rom
}
void dsupdate() {
  ds.reset(); ds.write(0xCC); ds.write(0x44); _delay_ms(100);
  for( byte _i = PCB; _i <= VEH; _i++ ) {
    ds.reset(); ds.select(sid(_i)); ds.write(0xBE);
    int8_t _raw = ds.read() >> 4; _raw |= ds.read() << 4;
    ds.read(); ds.read();            // th, tl
    if (ds.read() == 0x1F)           // 9-bit conf?
      temps[DIF][_i] = (temps[CUR][_i] == -127) ? 0 : _raw - temps[CUR][_i],
      temps[CUR][_i] = _raw;
    else
      temps[DIF][_i] = 0, temps[CUR][_i] = -127;
  }// ds.reset();
  if (secure & 0x80)                                                                                  // overheats:
    if ( (temps[DIF][PCB]  > 1) || (temps[CUR][PCB]  > 60) ||                                          //  pcb
         (temps[DIF][OUTS] > ((millis() - tengrun) < 180000ul ? 3 : 1)) || (temps[CUR][OUTS] > 60) ||  //  outside
         (temps[DIF][VEH]  > ((millis() - tengrun) < 300000ul ? 3 : 1)) || (temps[CUR][VEH]  > 75) )   //  vehicle
        alarm |= 0xA0, ign.set(false);
}

void setup() {
  wdt_enable(WDTO_4S);
  analogReference(DEFAULT); pinMode(NEUTRAL_PIN, INPUT_PULLUP);
  DDRD &= ~0x0C; PORTD |= 0x0C; EICRA = 0x0A; EIFR = EIFR;    // D2,D3: input, pullup, falling, clear
  modem.begin(UART_BPS); modem.setTimeout(DEF_TIMEOUT);
  dsinit(); dsupdate();
  if (neutral()) secure = 0x83;
  #if DEBUG_LVL
  debug.begin(UART_BPS); dbg("< DEBUG MODE >");
//  modem.println(F("ATE1")); modem.find("OK\r\n");
  #endif
  modemreset();
}

void loop() {
  wdt_reset();
  #if DEBUG_LVL & 1
  while (debug.available()) modem.write(debug.read());
  #endif
  if (modem.available()) athandling();
  if (starter.active()) ign.set(false), warmup = false;
  if (setupmode) {
    if (EIFR) {
      _delay_ms(100);
      modem.println(F("AT+BTSPPSEND"));
      if (modem.find("> ")) modem.println(EIFR & 1 ? F(" ! (>_<) !") : F(" (-_-)")), modem.write(0x1A), modem.find("SEND OK\r\n");
      EIFR = EIFR;
    }
    return;
  }
  led.set( (secure & 0x80) && ((millis() & 0x3FFul) > 0x370ul) );
  if (drive()) dvr.twitch(120000ul);
  if (engrun()) tengrun = millis();
  if (keepconn && !celstate) {
    if ((millis() - tsend) > 26666ul) if (sendstart()) { modem.write(0xC0); modem.write(byte(0x00)); if (sendfin()) tsend = millis(); } // ping
    if ((millis() - tresp) > 30000ul)  modemshut();  // broker isn't responding!
  }
  if ((millis() - t20sec) > 20000ul) {
    t20sec = millis(); dsupdate();
    if (celstate && !modem.available()) modem.println(F("AT+CPAS")); // query cellular status - freez calling state fix
  }
  if (warmup) {
    if ((millis() - t1min) >= 60000ul) t1min = millis(), warmtimer--;
    warmup = pump.active() && neutral() && (warmtemp > temps[CUR][ENG]) && (warmtimer > 0);   
    if (!warmup) ign.set(false);
  }
  if (secure & 0x80) {
    if (EIFR) _delay_ms(100), alarm |= (EIFR & 0x01) ? 0x82 : (((secure & 0x01) && !engrun()) ? 0x41 : 0), EIFR = EIFR; // CHECK
    if (hood.change() == 1) alarm |= 0x84;
    if (door.change() == 1) alarm |= 0x88;
    if (pump.change() == 1) alarm |= 0x90;
  }
  if (alarm & 0x80) {
    nrecall = 10; dvr.twitch(60000ul);
    if (celstate == 0) {
      if (secure & 0x02) siren.twitch(15000ul);
      flash.twitch(20000ul);
    }
  }
  if ( (alarm & 0x3E) && (celstate == 0) )
    if ( ((nrecall > 0) && ((millis() - tlastcall) > 120000ul)) || (alarm & 0x80) ) {
      modem.println(F("AT+DDET=1,500,0;D>\"admin\";")), tlastcall = millis(), nrecall--;
//      if (modem.find("OK\r\n")) celstate = 0x01;
  }
  if (alarm & 0x40) {
    dvr.twitch(30000ul);
    if ((millis() - tlastmsg) > (smsnotify ? 5000ul : 200ul)) notify("BANG! (>_<)", smsnotify ? SMS : PUSH+SMS), tlastmsg = millis();
//    if (smsalert) alarm &= ~0x01;
    if (!siren.on() && (secure & 0x02)) siren.twitch(50ul, 5);
    if (!flash.on()) flash.twitch(2000ul);
  }
  if ( (alarm & 0x3E) && (celstate == 0x06) )
    for ( byte _i = 5; _i > 0; _i-- ) {          //  0( h01 ) = shock1 
      if (!bitRead(alarm, _i)) continue;         //  1( h02 ) = shock2
      bitClear(alarm, _i);                       //  2( h04 ) = hood open
      if (_i == 5)  { play("fire");     break; } //  3( h08 ) = door open
      if (_i == 4)  { play("ignon");    break; } //  4( h10 ) = ignition on
      if (_i == 3)  { play("dooropen"); break; } //  5( h20 ) = fire detect
      if (_i == 2)  { play("hoodopen"); break; } //  6( h40 ) = NEW WARNING
      if (_i == 1)  { play("shock");    break; } //  7( h80 ) = NEW ALARM
  }
  alarm &= ~0xC0;
  lock.processing(); unlock.processing(); dvr.processing(); siren.processing(); flash.processing();
} // ************* LOOP +++++++++++++++++++++++
