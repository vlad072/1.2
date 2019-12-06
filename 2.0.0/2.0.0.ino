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
Adc    vbatt(BATT_PIN, /*1024/5.11*4.7/(4.7+15)*/47.83f), ntrl(NEUTRAL_PIN, 200.48f);

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
  if ( engrun() || !neutral() || ((warmtemp - temps[CUR][ENG]) < 5) || (vbatt.value() < 10.5f) ) return;
  if (sendstart()) pubf("inf/wutm", warmtimer, 0), pubf("inf/wutp", warmtemp, 0), sendfin();
  ign.set(true); delay(250);
  if (starter.active()) goto _err;                          // mosfet breakdown
  delay(3000);
  #ifdef WD_TIMER
  wdt_reset();
  #endif
  float _vinit = vbatt.value(); uint32_t _tinit = millis(); //_tmout = temps[CUR][ENG] == -127 ? 1500 : constrain( 1200 - temps[CUR][ENG] * 16, 700, 2000 );
  starter.set(true); delay(500); if (!starter.active()) goto _err;
  while ( ((millis() - _tinit) < 3000ul) && (vbatt.value() < (_vinit - 1.7f)) );  // spinup engine - max 3.0sec
  starter.set(false); delay(100); if (!starter.active()) goto _ok;
  _err: ign.set(false);
  _ok:
  #ifdef WD_TIMER
  wdt_reset();
  #endif
  delay(3000);
  #ifdef WD_TIMER
  wdt_reset();
  #endif  
  warmup = engrun();
  if (warmup) warmtimer = 20; else ign.set(false);
  pump.change(); EIFR = EIFR; t1min = millis();
}

void modemreset() {
  dbg("reset modem...");
  modem.println(F("ATZ+CFUN=1,1")); modem.find("OK\r\n");  // user profile recovery; reboot modem 
  celstate = 0; tsend = millis(); tresp = millis();
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
  tsend = millis(); tresp = millis();
}
void play(const char* track) {
  if (celstate != (INCALL + PLAYRDY)) return; dbg("play", track);
  celstate &= ~PLAYRDY;
  modem.print(F("AT+CREC=4,C:\\User\\")); modem.print(track); modem.println(F(".amr,0,90")); modem.find("OK\r\n");
}
bool sendstart() {
  bool _ret = false; modem.setTimeout(200);
  modem.println(F("AT+CIPSEND")); _ret = modem.find("> ");
  modem.setTimeout(DEF_TIMEOUT);
  return _ret; 
}
bool sendfin() {
  bool _ret = false; modem.setTimeout(200);
  modem.write(0x1A); _ret = modem.find("DATA ACCEPT:");
  if (_ret) tsend = millis();
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
  modem.write(0xE6); modem.write((byte)0x00); modem.write(0x3C);             // flags, ka timout | E/C -retain/not
  modem.write((byte)0x00); modem.write(strlen(cid));    modem.write(cid);
  modem.write((byte)0x00); modem.write(strlen(willt));  modem.write(willt);  // will topic
  modem.write((byte)0x00); modem.write(0x01);           modem.write('0');    // will message
  modem.write((byte)0x00); modem.write(strlen(user())); modem.write(user());
  modem.write((byte)0x00); modem.write(strlen(pass())); modem.write(pass());
  sendfin();
}
void ping() {
  if (sendstart()) modem.write(0xC0), modem.write(byte(0x00)), sendfin();
}
void pubbalance(const char* msg) {
  if (!msg) return; byte _i = 0; char _bal[8] = "";
  for (;; _i += 4) {
    if (!msg[_i]) return;
    if (!strncmp(msg+_i, "003", 3) && isdigit(msg[_i+3])) break;
    if (!strncmp(msg+_i, "002D", 4)) break;
  }
  for (;strlen(_bal)+1 < sizeof(_bal); _i += 4)
    if      (!strncmp(msg+_i, "002D", 4))                       strcat (_bal, "-");
//    else if (!strncmp(msg+_i, "002E", 4))                       strcat (_bal, "."); // float value
    else if (!strncmp(msg+_i, "003" , 3) && isdigit(msg[_i+3])) strncat(_bal, msg+_i+3, 1);
    else break;
  pub("inf/balance", _bal, true);
}
void publocate(const char* msg) {
  if (!msg) return;
  char* _eptr = strchr(msg, ',') + 1;
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
  if (alarm & (SHOCK1 | SHOCK2)) _sign[8] = alarm & SHOCK2 ? '2' : '1';
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
    notify(_opened ? "VEHICLE ISN'T CLOSED!" : "LOCK", _opened ? PUSH+SMS : PUSH);
    lock.twitch(500ul, 2); flash.twitch(1200ul);
    secure |= ARMED | SHOCK_ON;
    door.change(); hood.change(); EIFR = EIFR; alarm = 0;
  } else {                    // unlock
    if (secure & SIREN_ON)
      if      (alarm & (HOOD_OPEN | DOOR_OPEN | IGN_ON | FIRE_DET)) siren.twitch(100, 3);
      else if (alarm & SHOCK2)                                      siren.twitch(100, 2);
      else if (alarm & SHOCK1)                                      siren.twitch(100, 1);
    notify("UNLOCK");
    unlock.twitch(300ul); flash.twitch(350ul);
    secure &= ~ARMED;
  }
  if (sendstart()) pub("inf/lock", secure & ARMED ? "1" : "0"), pub("inf/shock", secure & SHOCK_ON ? "1" : "0"), sendfin();
}
void cmd(const char* topic, const char* payl) { //++++++++++++++  USER COMMAND IMPLEMENTAION  ++++++++++++++++++
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
    if (payl[0] > '0') secure |=  SHOCK_ON, EIFR = EIFR;
    else               secure &= ~SHOCK_ON;
    if (sendstart()) pub("inf/shock", secure & SHOCK_ON ? "1" : "0"), sendfin();
  } else if (!strcmp(topic, "siren")) {   //*************** siren active **************
    if (payl[0] > '0') secure |=  SIREN_ON;
    else               secure &= ~SIREN_ON;
    if (sendstart()) pub("inf/siren", secure & SIREN_ON ? "1" : "0"), sendfin();
  } else if (!strcmp(topic, "sms")) {      //************** sms notify bang warning ***
    smsalert = payl[0] > '0';
    if (sendstart()) pub("inf/sms", smsalert ? "1" : "0"), sendfin();
//  } else if (!strcmp(topic, "keepc")) {    //*************** keep connect **************
//    keepconn = payl[0] > '0';
//    if (sendstart()) pub("inf/keepc", keepconn ? "1" : "0"), sendfin();
  } else if (!strcmp(topic, "btpwr")) {    //*************** blue power on/off ********
    modem.setTimeout(5000);
    modem.print(F("AT+BTPOWER=")); modem.println(payl[0] > '0' ? "1" : "0"); modem.find("OK\r\n");
    modem.setTimeout(DEF_TIMEOUT);
    if (payl[0] > '0') modem.println(F("AT+BTVIS=0")), modem.find("OK\r\n");
    modem.println(F("AT+BTSTATUS?"));
  } else if (!strcmp(topic, "btansw")) {   //************ hfp auto-answer *************
    btansw = payl[0] > '0';
    if (sendstart()) pub("inf/btansw", btansw ? "1" : "0"), sendfin();
  } else if (!strcmp(topic, "alarm")) {    //***************** alarms *****************
    switch (payl[0]) {
      case '1': alarm = 0; if (sendstart()) pubalarm(), sendfin();    // clear
      case '2': siren.set(false); flash.set(false); break;            // mute
      case '3': if (secure & SIREN_ON) siren.twitch(500ul, 3); flash.twitch(3000ul); // raise
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
        pub ("inf/engr"  , engrun() ?  "1" : "0");       
        pub ("inf/warmup", warmup   ?  "1" : "0");
        pubf("inf/wutp"  , warmtemp,  0);
        pubf("inf/wutm"  , warmtimer, 0);
        pub ("inf/drop"  , door.active()     ?  "1" : "0");
        pub ("inf/hdop"  , hood.active()     ?  "1" : "0");
        pub ("inf/lock"  , secure & ARMED    ?  "1" : "0");
        pub ("inf/shock" , secure & SHOCK_ON ?  "1" : "0");
        pub ("inf/siren" , secure & SIREN_ON ?  "1" : "0");
        pub ("inf/sms"   , smsalert          ?  "1" : "0");
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
      cmd(_topic, _payl);
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
    if (_found && _crc) EEPROM.put(0x40 + 8*_idx, _sid);
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
      modem.println(F("-=hit the car to sh-sens test=-"));
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
    if (celstate == (INCALL + PLAYRDY)) modem.setTimeout(5000), play("recov"), modem.find("+CREC: 0");
    modem.println(F("ATH")); modem.find("OK\r\n");
    if (sendstart()) pub(willt, "0", true), sendfin();
    modemreconf(); reboot();
  } else if (cmd == '2') {                                                      // keep broker connection
    keepconn = !keepconn; play(keepconn ? "keep" : "nkeep");
  } else if (cmd == '3') {                                                      // shock sensor enble
    secure ^= SHOCK_ON; play(secure & SHOCK_ON ? "shken" : "shkdis");
  } else if (cmd == '6') {                                                      // mute siren
    siren.set(false); flash.set(false); //play("mute");
  } else if (cmd == '0') { dbg("setup");                                        // setup mode
    modem.setTimeout(3000);
    modem.println(F("AT+BTPOWER=1")); modem.find("OK\r\n");
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
  } at[_atlen] = '\0';
  #if DEBUG_LVL & 1
  for (byte _i = 0; _i < _atlen; _i++) debug.write(at[_i]);
  #endif
  if (strstr(at, "SHUT OK")) if (keepconn && !setupmode) modem.print(F("AT+CIPMUX=0;+CIPSTART=TCP,")), modem.println(broker());
  //_ptr = strstr(at, "+CGATT:"); if (_ptr) 
  //  if (_ptr[8] == '1') modem.print(F(" AT+CIPMUX=0;+CIPSTART=TCP,")), modem.println(broker());
  //  else                modem.println(F("AT+CGATT?"));
  if (strstr(at, "CONNECT OK")) brokercon();
  if (strstr(at, "SMS Ready")
   || strstr(at, "DEACT")
   || strstr(at, "CLOSED")
   || strstr(at, "CONNECT FAIL")) dbg("reconn!"), modemshut();
  _ptr = strstr(at, "+SAPBR:"); if (_ptr) modem.println( _ptr[10] == '3' ? F("AT+SAPBR=1,1") : F("AT+CIPGSMLOC=1,1") );
  _ptr = strstr(at, "+CLCC: 1,"); if (_ptr) {
    switch (_ptr[11]) {
      case '0': siren.set(false); flash.set(false); celstate = INCALL + PLAYRDY; play("hello"); break;
      case '2':
      case '3': celstate = ALERT; break;
      case '6':
      case '7': celstate = 0; break;
      case '4': modem.println(strstr(_ptr+17, "\"admin\"") ? F("AT+DDET=1,500,0;A") : F("AT+CHUP"));
    } tresp = millis(); t20sec = millis();
  }
  if (strstr(at, "+CREC: 0")) celstate |= PLAYRDY;
  if (strstr(at, "+CPAS: 0")) celstate  = 0;// tresp = millis();
  _ptr = strstr(at, "+DTMF:"); if (_ptr) dtmf(_ptr[7]);
  _ptr = strstr(at, "+BTCONNECTING:"); if (_ptr) if (setupmode && strstr(_ptr+15, "\"SPP\"")) modem.println(F("AT+BTACPT=1"));
  _ptr = strstr(at, "+BTCONNECT:"); if (_ptr)
    if  (setupmode && strstr(_ptr+12, "\"SPP\"")) { 
      modem.println(F("AT+BTSPPGET=0")); modem.find("OK\r\n");
      modem.println(F("AT+BTSPPSEND"));
      if (modem.find("> ")) modem.println(F("Welcome to the BlackBox setup!")), modem.write(0x1A), modem.find("SEND OK\r\n");
    } else {
      if ( (secure & ARMED) && (_ptr[12] > '0') && (!engrun() || warmup) ) locking(false);
      if (sendstart()) {
        pub("inf/lock", secure & ARMED ? "1" : "0"); pub("inf/shock", secure & SHOCK_ON ? "1" : "0");
        if (sendfin()) modem.println(F("AT+BTSTATUS?"));
      }
    }
  if    (strstr(at, "+BTDISCONN:")) {
    if (!(secure & ARMED) && !pump.active() && !setupmode) locking(true);
    if (sendstart()) {
      pub("inf/lock", secure & ARMED ? "1" : "0"); pub("inf/shock", secure & SHOCK_ON ? "1" : "0");
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
    char* _qptr = _ptr+6;
    char _sq[3] = ""; strncat(_sq, _qptr, strchr(_qptr, ',') - _qptr);
    if (sendstart()) {
      pub("inf/sq", _sq);
      if (sendfin()) modem.println(F("AT+BTSTATUS?"));
    }
  }
  _ptr = strstr(at, "+CIPGSMLOC:"); if (_ptr) if (_ptr[12] == '0') if (sendstart()) publocate(_ptr+12), sendfin();
  _ptr = strstr(at, "+IPD");        if (_ptr) tresp = millis(), ipd( strchr(_ptr+6, ':')+1, atoi(_ptr+5) );
  _ptr = strstr(at, "+BTSPPDATA:"); if (_ptr) btspp( strchr(strchr(_ptr+11, ',')+1, ',') + 1, atoi(strchr(_ptr+11, ',') + 1) );
}
void dsinit() {
  for( byte _i = 0; _i < 4; _i++ ) {
    ds.reset(); ds.select(sid(_i)); ds.write(0x4E); ds.write(0x7F), ds.write(0xFF); ds.write(0x1F);
    ds.reset(); ds.select(sid(_i)); ds.write(0x48); // to rom
  }  
}
void dsupdate() {
//  for( byte _i = 0; _i < 4; _i++ ) ds.reset(), ds.select(sid(_i)), ds.write(0x44); _delay_ms(150);
  ds.reset(); ds.write(0xCC); ds.write(0x44); _delay_ms(100);
  for( byte _i = 0; _i < 4; _i++ ) {
    ds.reset(); ds.select(sid(_i)); ds.write(0xBE);
    int8_t _raw = ds.read() >> 4; _raw |= ds.read() << 4;
    ds.read(); ds.read();                                                                 // th, tl
    if (ds.read() == 0x1F) temps[DIF][_i] = (temps[CUR][_i] == -127) ? 0 : _raw - temps[CUR][_i], temps[CUR][_i] = _raw; // 9-bit conf?
    else                   temps[DIF][_i] = 0, temps[CUR][_i] = -127;
  }// ds.reset();
}

void setup() {
  #ifdef WD_TIMER
  wdt_reset(); wdt_disable(); wdt_enable(WDTO_4S);
  #endif
  analogReference(DEFAULT); pinMode(NEUTRAL_PIN, INPUT_PULLUP);
  DDRD &= ~0x0C; PORTD |= 0x0C; EICRA = 0x0A; EIFR = EIFR;    // D2,D3: input, pullup, falling, clear
  modem.begin(UART_BPS); modem.setTimeout(DEF_TIMEOUT);
  dsinit(); dsupdate();
  locking(true);
  #if DEBUG_LVL
  debug.begin(UART_BPS); dbg("< DEBUG MODE >");
  modem.println(F("ATE1")); modem.find("OK\r\n");
  #endif
  modemreset();
}

void loop() {
  #ifdef WD_TIMER
  wdt_reset();
  #endif
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
  led.set( (secure & ARMED) && ((millis() & 0x3FFul) > 0x370ul) );
  if (drive()) dvr.twitch(120000ul);
  if (engrun()) tengheat = millis();
  if ((celstate == 0) && keepconn) if ((millis() - tresp) > 60000ul) {
    dbg("broker isn't responding!");
//    if (sendstart()) pub("notify", "broker isn't responding!"), sendfin();
    modemshut();
  }
  if ((millis() - tsend) > 27333ul) tsend = millis(), ping();
  if ((millis() - t20sec) > 20000ul) { t20sec = millis();
    dsupdate();
    if (celstate && !modem.available()) modem.println(F("AT+CPAS")); // query cellular status - freez calling state fix
  }
//  if (initlock) if (millis() > 30000ul) initlock = false, locking(true);
  if (warmup) {
    if ((millis() - t1min) > 60000ul) t1min = millis(), warmtimer = warmtimer > 0 ? warmtimer - 1 : 0;
    warmup = pump.active() && neutral() && (warmtemp > temps[CUR][ENG]) && (warmtimer > 0);   
    if (!warmup) {
      ign.set(false);
      if (sendstart()) pub("inf/warmup", "0"), pubf("inf/wutm", warmtimer, 0), pubf("inf/wutp", warmtemp, 0), pubf("inf/engtp", temps[CUR][ENG], 0), sendfin();
    }
  }
  if (secure & ARMED) {
    if (secure & SHOCK_ON) if (EIFR) _delay_ms(100), alarm |= EIFR & 0x01 ? 0x82 : (engrun() ? 0x00 : 0x41), EIFR = EIFR;
    if (hood.change() == 1) alarm |= NEW_ALARM | HOOD_OPEN;
    if (door.change() == 1) alarm |= NEW_ALARM | DOOR_OPEN;
    if (pump.change() == 1) alarm |= NEW_ALARM | IGN_ON;                                                                         // overheats:
    if (!(alarm & FIRE_DET)) if ( (temps[DIF][PCB]  > 2) || (temps[CUR][PCB]  > 60) ||                                           //  pcb
                                  (temps[DIF][OUTS] > ((millis() - tengheat) < 180000ul ? 6 : 2)) || (temps[CUR][OUTS] > 60) ||  //  outside
                                  (temps[DIF][VEH]  > ((millis() - tengheat) < 300000ul ? 4 : 2)) || (temps[CUR][VEH]  > 75) )   //  vehicle
                            alarm |= NEW_ALARM | FIRE_DET, ign.set(false);
  }
  if (alarm & NEW_ALARM) {
    alarm &= ~NEW_ALARM; dvr.twitch(60000ul);
    if (celstate == 0) modem.println(F("ATD>\"admin\";")), celstate = ALERT;
    if (celstate <  INCALL) {
      if (secure & SIREN_ON) siren.twitch(15000ul);
      flash.twitch(20000ul);
    }
  }
  if (alarm & NEW_WARN) {
    alarm &= ~NEW_WARN; dvr.twitch(30000ul);
    if ((millis() - tlastmsg) > 2000ul) notify("BANG! (>_<)", smsalert ? SMS : PUSH), tlastmsg = millis();
    if (smsalert) alarm &= ~SHOCK1;
    if (!siren.on() && (secure & SIREN_ON)) siren.twitch(20ul, 15);
    if (!flash.on()) flash.twitch(2000ul);
  }
  if ( (alarm > SHOCK1) && (celstate == (INCALL + PLAYRDY)) )
    for ( byte _i = 5; _i > 0; _i-- ) {         //  0( h01 ) = shock1 
      if (!bitRead(alarm, _i)) continue;        //  1( h02 ) = shock2                                                     
      if (_i == 5) { play("fire");     break; } //  2( h04 ) = hood open  
      bitClear(alarm, _i);                      //  3( h08 ) = door open
      if (_i == 4) { play("ignon");    break; } //  4( h10 ) = ignition on
      if (_i == 3) { play("dooropen"); break; } //  5( h20 ) = fire (manual reseting)
      if (_i == 2) { play("hoodopen"); break; } //  6( h40 ) = NEW WARNING
      if (_i == 1) { play("shock");    break; } //  7( h80 ) = NEW ALARM
  }
  lock.processing(); unlock.processing(); dvr.processing(); siren.processing(); flash.processing();
} // ************* LOOP +++++++++++++++++++++++
