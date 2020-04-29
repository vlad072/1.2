#include <avr/wdt.h>
#include <SoftwareSerial.h>
#include <OneWire.h>
#include "data.h"
#include "classes.h"

SoftwareSerial modem(MODEM_RX_PIN, MODEM_TX_PIN);
OneWire ds(ONEWIRE_PIN);
Input  door(DOOR_PIN), hood(HOOD_PIN), pump(PUMP_PIN);
Output ign(IGN_PIN), lock(LOCK_PIN), unlock(UNLOCK_PIN), siren(SIREN_PIN), flash(FLASH_PIN), dvr(DVR_PIN), led(LED_PIN);
InOut  starter(STARTER_SUPPLY_PIN, STARTER_PIN);
Adc    vbatt(BATT_PIN, 47.83f), ntrl(NEUTRAL_PIN, 200.48f);

void(*reboot)(void) = 0;
inline __attribute__((__always_inline__)) bool timeover(uint32_t var, uint32_t timeout) {
  return (millis() - var) >= timeout;
}
void dbg(const char* par, char* val = NULL) {
  #if DEBUG_LVL & 2
  debug.print(par);
  if (val) debug.print("="), debug.print(val);
  debug.println();
  #endif
}
void hexprint(Print& uart, byte* arr, byte len = 1) {
  for (byte _i = 0; _i < len; _i++) {
    if (arr[_i] < 0x10) uart.write('0');
    uart.print(arr[_i], HEX);
    if (_i < (len-1)) uart.write(' ');
  }
}
bool waitresp(char* str = NULL, uint32_t timeout = DEF_TIMEOUT) {
  byte _i = 0; char _c = 0; bool _ret = 0;
  for (uint32_t _t = millis(); !timeover(_t, timeout);) {
    if (!modem.available()) continue;
    _t = millis(); timeout = 10ul; _c = modem.read();
    if (!str) continue;
    _i = (str[_i] == _c) ? _i+1 : 0;
    if (_i >= strlen(str)) _ret = 1;
  }
  return _ret;
}
bool neutral() {
  return ntrl.value() < 0.35f;
}
bool engrun() {
  return pump.active() || (vbatt.value() > 13.4f);
}
void starting() {
  if ( warmup || engrun() || !neutral() || ((warmtemp - temps[CUR][ENG]) < 5) || (vbatt.value() < 10.5f) ) return;
  ign.set(1); _delay_ms(250);
  if (starter.active()) goto _fin;                         // mosfet breakdown
  _delay_ms(3000); wdt_reset();
  float _vrelease = vbatt.value() - 1.7f;
  starter.set(1); _delay_ms(500);
  for (uint32_t _t = millis(); !timeover(_t, 3000ul);) if (vbatt.value() > _vrelease) break; // spinup engine - max 3.0sec
  wdt_reset(); starter.set(0); _delay_ms(100); if (starter.active()) goto _fin;
  _delay_ms(3000); wdt_reset();
  warmup = engrun();
  _fin:
  if (warmup) warmtimer = 20; else ign.set(0);
  pump.change(); EIFR = EIFR; t1min = millis();
}

void modemreset() {
  dbg("reset modem...");
  modem.println(F("ATZ+CFUN=1,1")); waitresp();  // user profile recovery; reboot modem 
  callstate = 0; tsend = tresp = millis();
}
void modemreconf() {
  dbg("modem reconf...");
  modem.print  (F("AT&F+IPR="));  modem.println(UART_BPS, DEC);       waitresp(); // usart speed
  modem.print  (F("AT+BTHOST=")); modem.println(cid);                 waitresp(); // blue host name
  modem.println(F("AT+CMIC=0,15;+BTPAIRCFG=1;+CLCC=1;+CMGF=1;E0&W")); waitresp();
}
void modemshut() {
  modem.println(F("AT")); if (!waitresp("OK\r\n")) modem.write(0x1A), waitresp();
  modem.println(F("AT+CIPHEAD=1;+CIPQSEND=1;+CIPSHUT"));
  tsend = tresp = millis();
}
void play(const char* track) {
  if (callstate != 0x0C) return; dbg("play", track);
  modem.print(F("AT+CREC=4,C:\\User\\")); modem.print(track); modem.println(F(".amr,0,90"));
  if (waitresp("OK\r\n")) callstate = 0x04;;
}
bool sendstart() {
  modem.println(F("AT+CIPSEND"));
  return waitresp("> ", 200ul);
}
bool sendfin() {
  modem.write(0x1A);
  return waitresp("DATA ACCEPT:", 200ul);
}
void pub(const char* topic, const char* payl, byte retn = 0) { 
  modem.write(0x30 | retn);
  modem.write(2+strlen(topic)+strlen(pref()) + strlen(payl));
  modem.write((byte)0x00); modem.write(strlen(topic)+strlen(pref()));
  if (strlen(pref())) modem.write(pref());
  modem.write(topic);
  modem.write(payl);
}
void pubf(const char* topic, float val, byte dap) {
  char _st[8] = "";
  dtostrf(val, 0, dap, _st); pub(topic, _st);
}
void sub(const char* topic) {
  modem.write(0x82);
  modem.write(strlen(topic)+strlen(pref()) + 5); 
  modem.write((byte)0x00); modem.write(0x01)  ;
  modem.write((byte)0x00); modem.write(strlen(topic)+strlen(pref()));
  if (strlen(pref())) modem.write(pref());
  modem.write(topic);
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
void pubbalance(const char* inp) {
  byte _i = 0; char _bal[8] = "";
  for (; inp[_i]; _i += 4) {
    if (!strncmp(inp+_i, "003", 3) && isdigit(inp[_i+3])) break;
    if (!strncmp(inp+_i, "002D", 4)) break;
  }
  for (;strlen(_bal)+1 < sizeof(_bal); _i += 4)
    if      (!strncmp(inp+_i, "002D", 4))                       strcat (_bal, "-");
//    else if (!strncmp(inp+_i, "002E", 4))                       strcat (_bal, "."); // float value
    else if (!strncmp(inp+_i, "003" , 3) && isdigit(inp[_i+3])) strncat(_bal, inp+_i+3, 1);
    else break;
  pub("inf/balance", _bal, RETAIN);
}
void publocate(const char* inp) {
  char _link[24] = "0,0";
  if (inp[0] == '0') {
    char* _eptr = strchr(inp, ',') + 1;
    char* _nptr = strchr(_eptr, ',') + 1;
    byte  _elen = _nptr - _eptr - 1;
    byte  _nlen = strchr(_nptr, ',') - _nptr;
//  char  _link[64] = "https://www.google.com/maps/place/";
    strncpy(_link, _nptr, _nlen+1); _link[_nlen+1] = 0; strncat(_link, _eptr, _elen);
  }
  pub("inf/place", _link);
}
void notify(char* txt = NULL, byte mode = PUSH) {
  if (txt) strcpy(msg, txt);
  if (mode & PUSH) if (sendstart()) {
    pub("notify", msg);
    if (sendfin()) return;
  }
  if (mode & SMS) modem.println(F("AT+CPBF=\"admin\""));  
}
void locking(bool lk) {
  if (setupmode) return;
  if (lk) {                   // lock
    if (door.active() || hood.active()) siren.twitch(100ul, 3); else if (secure & 0x04) siren.twitch(1ul);
    lock.twitch(500ul, 2); flash.twitch(1200ul);
    secure |= 0x83;
    door.change(); hood.change(); EIFR = EIFR; alarm = 0;
  } else {                    // unlock
    if (secure & 0x04)
      if      (alarm & 0x3C)  siren.twitch(50ul, 3);
      else if (alarm & 0x02)  siren.twitch(50ul, 2);
      else if (alarm & 0x01)  siren.twitch(50ul, 1);
      else                    siren.twitch(1ul);
    unlock.twitch(300ul); flash.twitch(350ul);
    secure &= ~0x80;
  }
  if (sendstart()) pubf("inf/secure", secure, 0), sendfin();
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
    else               ign.set(0), warmup = 0, _delay_ms(250);
    if (sendstart()) pub("inf/warmup", warmup ? "1" : "0"), pub("inf/engr", pump.active() ? "1" : "0"), pubf("inf/wutm", warmtimer, 0), pubf("inf/wutp", warmtemp, 0), sendfin();
  } else if (!strcmp(topic, "lock"))  {   //*************** locking *******************
    bool _op = door.active() || hood.active(); bool _lk = payl[0] > '0';
    notify(_lk ? (_op ? "VEH ISN'T CLOSED!" : "REM ARMED") : "REM DISARM", !_op && _lk ? PUSH : PUSH+SMS);
    locking(_lk);
  } else if (!strcmp(topic, "shock")) {   //********** enable/dis shock sensor ********
    secure &= ~0x03; secure |= (payl[0]-'0') & 0x03;
    if (sendstart()) pubf("inf/secure", secure, 0), sendfin();
  } else if (!strcmp(topic, "siren")) {   //*************** siren active **************
    bitWrite(secure, 2, payl[0] > '0');
    if (sendstart()) pubf("inf/secure", secure, 0), sendfin();
  } else if (!strcmp(topic, "dvr")) {     //**************** dvr active ***************
    if (payl[0] > '0') dvr.twitch(600000ul);
    else               dvr.set(0);
    if (sendstart()) pub("inf/dvr", dvr.on() ? "1" : "0"), sendfin();
  } else if (!strcmp(topic, "sms")) {      //************** sms notify ****************
    smsnotify = payl[0] > '0';
    if (sendstart()) pub("inf/sms", smsnotify ? "1" : "0"), sendfin();
  } else if (!strcmp(topic, "recm")) {   //************ drive recording *************
    recmode = payl[0] - '0';
    if (sendstart()) pubf("inf/recm", recmode, 0), sendfin();
  } else if (!strcmp(topic, "btpwr")) {    //*************** blue power on/off ********
    bool _pw = payl[0] > '0'; wdt_reset();
    modem.print(F("AT+BTPOWER=")); modem.println(_pw ? "1" : "0"); waitresp(NULL, 3900ul);
    wdt_reset();
    if (_pw) {
      modem.println(F("AT+BTVIS=0"));
      if (!waitresp("OK\r\n")) modem.println(F("AT+BTPOWER=0")), waitresp(NULL, 2900ul);
    }
    wdt_reset();
    modem.println(F("AT+BTSTATUS?"));
  } else if (!strcmp(topic, "btansw")) {   //************ hfp auto-answer *************
    btansw = payl[0] > '0';
 //   modem.println(F("AT+BTSTATUS?"));
    if (sendstart()) pub("inf/btansw", btansw ? "1" : "0"), sendfin();
  } else if (!strcmp(topic, "alarm")) {    //***************** alarms *****************
    switch (payl[0]) {
      case '1': alarm = 0; if (sendstart()) pubf("inf/alarm" , alarm, 0), sendfin(); // clear
      case '2': siren.set(0); flash.set(0); break;                           // mute
      case '3': if (secure & 0x02) siren.twitch(500ul, 3); flash.twitch(3000ul);     // raise
    }
  } else if (!strcmp(topic, "upd"))   {    //*************** send inform to dash *******
    switch (payl[0]) {
      case '1': modem.println(F("AT+SAPBR=2,1")); break;                        // locate
      case '2': modem.print(F("AT+CUSD=1,")); modem.println(payl+1); break;     // balance
      case '3': if (sendstart()) {                                              // common                 
        pubf("inf/outtp" , temps[CUR][OUTS], 0);
        pubf("inf/engtp" , temps[CUR][ENG] , 0);
        pubf("inf/pcbtp" , temps[CUR][PCB] , 0);
        pubf("inf/vehtp" , temps[CUR][VEH] , 0);
        pubf("inf/wutp"  , warmtemp        , 0);
        pubf("inf/wutm"  , warmtimer       , 0);
        pubf("inf/vbatt" , vbatt.value()   , 2);
        pubf("inf/moto"  , moto()/60.0f    , 1);                          
        pub ("inf/engr"  , engrun()        ?  "1" : "0");       
        pub ("inf/warmup", warmup          ?  "1" : "0");
        pubf("inf/doors" , hood.active() | (door.active()<<1), 0);
        //pub ("inf/drop"  , door.active()   ?  "1" : "0");
        //pub ("inf/hdop"  , hood.active()   ?  "1" : "0");
        pubf("inf/secure", secure, 0);
        //pub ("inf/lock"  , secure & 0x80   ?  "1" : "0");
        //pub ("inf/shock" , secure & 0x01   ?  "1" : "0");
        //pub ("inf/siren" , secure & 0x02   ?  "1" : "0");
        pub ("inf/sms"   , smsnotify       ?  "1" : "0");
        pub ("inf/btansw", btansw          ?  "1" : "0");
        pub ("inf/dvr"   , dvr.on()        ?  "1" : "0");
        pub ("inf/gear"  , neutral()       ?  "0" : "1");
        pubf("inf/recm"  , recmode, 0);
        //pub ("inf/drvreg", drivereg        ?  "1" : "0");
        //pubalarm();
        pubf("inf/alarm" , alarm, 0);
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
        if (sendstart()) pub(willt, "1", RETAIN), sub("cmd/#"), sendfin();
        dbg("connakOK");// flash.twitch(350ul);
      } else {
        dbg("connak refused");
        modemshut(); return;
      }
    } else  if (frame[_ofs] == 0x90) {                                   // suback
      dbg("suback");
    } else if (frame[_ofs] == 0x30) {                                    // pub recv
      char* _tptr = (char*)(frame + _ofs + 8 + strlen(pref())); // +4 = full topic
      byte  _tlen = frame[_ofs+3] - 4 - strlen(pref());         // -0 = full topic length
      char* _pptr = (char*)(frame + _ofs + frame[_ofs+3] + 4) ;
      byte  _plen = frame[_ofs+1] - frame[_ofs+3] - 2;
      char  _topic[_tlen+1]; strncpy(_topic, _tptr, _tlen); _topic[_tlen] = 0;
      char  _payl [_plen+1]; strncpy(_payl,  _pptr, _plen); _payl [_plen] = 0;
      exec(_topic, _payl);
    } //else break; //unexpected header
    _ofs += 2 + frame[_ofs+1];
  } // cycle
}  // ipd func
void btspp(const char* inp, byte len) { //+++++++++++++++++++ BLUETOOTH SETUP THROUGH SPP  ++++++++++++++++++
  char* _paramptr = strchr(inp, '=') + 1; byte _paramlen = len - (_paramptr - inp);
  if        ( !strncmp(inp, "srv", 3) && (_paramlen < sizeof(eecash)) ) {
    broker(_paramptr, _paramlen);
    modem.println(F("AT+BTSPPSEND")); if (waitresp("> ")) modem.println(F(" ok")), modem.write(0x1A), waitresp();
  } else if ( !strncmp(inp, "usr", 3) && (_paramlen < sizeof(eecash)) ) {
    broker(_paramptr, _paramlen);
    modem.println(F("AT+BTSPPSEND")); if (waitresp("> ")) modem.println(F(" ok")), modem.write(0x1A), waitresp();
  } else if ( !strncmp(inp, "pwd", 3) && (_paramlen < sizeof(eecash)) ) {
    pass(_paramptr, _paramlen);
    modem.println(F("AT+BTSPPSEND")); if (waitresp("> ")) modem.println(F(" ok")), modem.write(0x1A), waitresp();
  } else if ( !strncmp(inp, "pref", 4) && (_paramlen < sizeof(eecash)) ) {
    pref(_paramptr, _paramlen);
    modem.println(F("AT+BTSPPSEND")); if (waitresp("> ")) modem.println(F(" ok")), modem.write(0x1A), waitresp();
  } else if (!strncmp(inp, "sens", 4)) {                          // ============ sensors ==============
    byte _sid[8]; byte _idx = _paramptr[0] - '0';
    if ( (_idx >= PCB) && (_idx <= VEH) ) {
      bool _found = 0; ds.reset_search();
      while (ds.search(_sid) && !_found) {
        _found = _idx == PCB;
        for (byte _i = 0; _i < 8; _i++) _found |= _sid[_i] != sid(PCB)[_i];
      }
      bool _crc = OneWire::crc8(_sid, 7) == _sid[7];
      if (_found) {
        sid(_idx, _sid);
        ds.reset(); ds.select(_sid); ds.write(0x4E); ds.write(0x7F), ds.write(0xFF); ds.write(0x1F);  // init dallas regs  // CHECK!
        ds.reset(); ds.select(_sid); ds.write(0x48);                                                  // save to dallas rom
      }
      modem.println(F("AT+BTSPPSEND")); if (waitresp("> ")) {
        modem.print(_found ? F(" ds[") : F(" ds not found!"));
        if (_found) {
          modem.print(_idx, HEX); modem.print(F("]="));
          hexprint(modem, _sid, 8);
          modem.print(_crc ? F(" crc ok") : F(" crc wrong!"));
        }
        modem.println(); modem.write(0x1A), waitresp();
      }
    } else {
      modem.println(F("AT+BTSPPSEND")); if (waitresp("> ")) {
        modem.println(F("\r\nstored:"));
        for (byte _i = PCB; _i <= VEH; _i++ ) hexprint(modem, sid(_i), 8), modem.println();
        modem.println(F("scan:")); ds.reset_search();
        while (ds.search(_sid)) hexprint(modem, _sid, 8), modem.println();        
        modem.write(0x1A); waitresp();
      }
    }                                                               // ============ sensors ==============
  } else if (!strncmp(inp, "btpin", 5) && (_paramlen == 4)) {
    modem.println(F("AT+BTSPPSEND")); if (waitresp("> ")) modem.println(F(" reconnect with new pin!")), modem.write(0x1A), waitresp();
    modem.println(F("AT+BTUNPAIR=0")); waitresp();
    modem.print(F("AT+BTPAIRCFG=1,")), modem.println(_paramptr); waitresp();
  } else if (!strncmp(inp, "moto", 4)) {                            // ======== running hours ============
    uint16_t _mins = atoi(_paramptr) * 60;
    modem.println(F("AT+BTSPPSEND")); if (waitresp("> ")) modem.println((moto(WRITE_MOTO, _mins) == _mins) ? F(" ok") : F(" error")), modem.write(0x1A), waitresp();
  } else if (inp[0] == '?') {
    modem.println(F("AT+BTSPPSEND")); if (waitresp("> ")) {
      modem.println();
      modem.println(F("moto=<N> : working hours"));
      modem.println(F("srv=<addr,port>")); 
      modem.println(F("usr=<username>"));
      modem.println(F("pwd=<password>"));
      modem.println(F("pref=<topics prefix>"));
      modem.println(F("sens=<N> : 0-pcb,1-eng,2-outs,3-vehcl,?-info"));
      modem.println(F("btpin=<NNNN> : bt dev's detach & upd pin"));
      modem.println(F("exit"));
      modem.println(F("-=hit the car to shock test=-"));
      modem.write(0x1A); waitresp();
    }
  } else if ( !strncmp(inp, "exit", 4) ) {
    modem.println(F("AT+BTVIS=0")); waitresp();
    modem.println(F("AT+BTSPPSEND")); if (waitresp("> ")) modem.println(F(" setup over!")), modem.write(0x1A), waitresp();
    eepage = 0; setupmode = 0; led.set(0);
  } else {
    modem.println(F("AT+BTSPPSEND")); if (waitresp("> ")) modem.println(F(" error!")), modem.write(0x1A), waitresp();
  }
}
void dtmf(const char key, bool lon) { //+++++++++++++++++++ ONCALL KEYPRESS HANDLING  ++++++++++++++++++
  dbg("dtmf", key);
  if ( ((key == '#') || (key == '0')) && lon ) {                                                    // emergency recovery
    wdt_disable(); modem.setTimeout(5000);
    if (callstate == 0x0C) play((key == '#') ? "recov" : "reboot"), modem.find("+CREC: 0");
    modem.println(F("ATH")); waitresp();
    if (sendstart()) pub(willt, "0", RETAIN), sendfin();
    if (key == '#') modemreconf();
    reboot();
  } else if (key == '2') {                                                      // keep broker connection
    keepconn = !keepconn; play(keepconn ? "keep" : "nkeep");
  } else if (key == '3') {                                                      // shock sensor enble
    secure ^= 0x01; play(secure & 0x01 ? "shken" : "shkdis");
  } else if (key == '5') {                                                      // dvr run to 1min
    if (dvr.on()) dvr.set(0); else dvr.twitch(600000ul);
    play(dvr.on() ? "dvron" : "dvroff");
  } else if ((key == '*') && lon) { dbg("setup");                               // setup mode
    if (setupmode) {
      modem.println(F("AT+BTVIS=0")); waitresp();
      eepage = 0; setupmode = 0;
    } else {
      modem.println(F("AT+BTPOWER=1")); wdt_reset();
      waitresp(NULL, 3900ul);    wdt_reset();
      modem.println(F("AT+BTVIS=1"));
      if (waitresp("OK\r\n")) setupmode = 1;
    }
    led.set(setupmode); play(setupmode ? "setup" : "normal");
  } else {
    play("error"); // unknown command
  }    
}
void athandling() { //++++++++++++++++++ AT RESPONSES HANDLING ++++++++++++++++++++
//  while (modem.available()) if (modem.peek() < '+') modem.read(); else break;                
//  if (!modem.available()) return;
  byte _atlen = 0; char* _ptr = NULL;
  for (uint32_t _trecv = millis(); !timeover(_trecv, 10ul);) {
    if (!modem.available()) continue;
    char _ch = modem.read(); _trecv = millis();
    if ((_atlen+1) < sizeof(at)) at[_atlen++] = _ch;
  } at[_atlen] = 0;
  #if DEBUG_LVL & 1
  for (byte _i = 0; _i < _atlen; _i++) debug.write(at[_i]);
  #endif
  if (strstr(at, "SHUT OK")) if (keepconn && !setupmode) modem.print(F("AT+CIPMUX=0;+CIPSTART=TCP,")), modem.println(broker());
  if (strstr(at, "CONNECT OK")) brokercon();
  if (strstr(at, "Call Ready")) callstate = 1;
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
      case '0': siren.set(0); flash.set(0); callstate = 0x0C; play("hello"); break;
      case '2':
      case '3': callstate = 0x02; break;
      case '6':
      case '7': callstate = 0x01; tsend = tresp = millis() - 23000ul; break;
      case '4': modem.println(strstr(_ptr+17, "\"admin\"") ? F("AT+DDET=1,500,1;A") : F("ATH"));
    } t20sec = millis();
  }
  if (strstr(at, "+CREC: 0")) callstate |= 0x08;
  if (strstr(at, "+CPAS: 0")) callstate  = 0x01;
  _ptr = strstr(at, "+DTMF:"); if (_ptr) dtmf(_ptr[7], atoi(_ptr+9) > 700);
  _ptr = strstr(at, "+BTCONNECTING:"); if (_ptr) if (setupmode && strstr(_ptr+15, "\"SPP\"")) modem.println(F("AT+BTACPT=1"));
  _ptr = strstr(at, "+BTCONNECT:"); if (_ptr)
    if  (setupmode && strstr(_ptr+12, "\"SPP\"")) { 
      modem.println(F("AT+BTSPPGET=0")); waitresp();
      modem.println(F("AT+BTSPPSEND"));
      if (waitresp("> ")) modem.println(F("Welcome to BlackBox setup!")), modem.write(0x1A), waitresp();
    } else {
      if ( (secure & 0x80) && (_ptr[12] > '0') && (!engrun() || warmup) ) {
        strcpy(msg, "BT DISARM ");
        char* _nm = strchr(_ptr+10, '"'); byte _ln = constrain( strchr(_nm+1, '"') - _nm + 1, 2, 20 );
        strncat(msg, _nm, _ln);
        notify(NULL, PUSH+SMS); locking(0);
      }
      modem.println(F("AT+BTSTATUS?"));
    }
  if (strstr(at, "+BTDISCONN:")) {
    if (!(secure & 0x80) && !pump.active() && !setupmode) {
      bool _op = door.active() || hood.active();      
      notify(_op ? "VEH ISN'T CLOSED!" : "BT ARMED", _op ? PUSH+SMS : PUSH), locking(1);
    }
    modem.println(F("AT+BTSTATUS?"));
  }
  _ptr = strstr(at, "+BTPAIR:");  if (_ptr) {
    strcpy(msg, "BT ADD ");
    char* _nm = strchr(_ptr+7, '"'); byte _ln = constrain( strchr(_nm+1, '"') - _nm + 1, 2, 23 );
    strncat(msg, _nm, _ln);
    notify(NULL, PUSH+SMS);
  }
  _ptr = strstr(at, "+BTSTATUS:");  if (_ptr) {
    byte _pair = 0; byte _conn = 0; char* _bptr = _ptr+12;
    while(1) {
      _bptr = strstr(_bptr, "\r\nP: "); if (!_bptr) break; _pair++; _bptr++;
    } _bptr = _ptr+12; while(1) {
      _bptr = strstr(_bptr, "\r\nC: "); if (!_bptr) break; _conn++; _bptr++;
    }
    byte _st = (_pair & 0x0F) | ((_conn & 0x07) << 4) | ((_ptr[11] > '0') << 7); //bits: WCCCPPPP (W=power, C=connected, P=paired)
    if (sendstart()) pubf("inf/btst", _st, 0), pub(willt, "1"), sendfin();
//    char _constr[4] = { '0'+_conn, '/', '0'+_pair, 0};
//    if (sendstart()) pub("inf/btpwr", _ptr[11] > '0' ? "1" : "0"), pub("inf/btcon", _constr), pub(willt, "1"), sendfin();
  }
  if (strstr(at, "BTRING") && btansw) if (timeover(tbtring, 8000ul)) tbtring = millis(); else tbtring = 0, modem.println(F("AT+BTATA")); // with 2nd ring answer
//  if (strstr(at, "BTRING") && btansw) if ((millis() - tbtring) > 13000ul) tbtring = millis(); else if ((millis() - tbtring) > 8000ul) tbtring = 0, modem.println(F("AT+BTATA")); // with 3rd ring answer
  if    (strstr(at, "+CMTI:")) modem.println(F("AT+CMGDA=6"));//, modem.find("OK\r\n"); // wipe sms
  _ptr = strstr(at, "+CPBF:");  if (_ptr) {                                             // recv ph.num "admin" then send sms CHECK!
    char* _nm = strchr(_ptr+6, '"'); if (_nm) {
      byte _ln = strchr(_nm+1, '"') - _nm + 1;
      modem.print(F("AT+CMGS=")); modem.write(_nm, _ln); modem.println();
      if (waitresp("> ")) modem.write(msg), modem.write(0x1A);
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
//void dsinit() {
//  ds.reset(); ds.write(0xCC); ds.write(0xB8); // resore into rom
//  ds.reset(); ds.write(0xCC); ds.write(0x4E); ds.write(0x7F), ds.write(0xFF); ds.write(0x1F);
//}
void dsupdate() {
  ds.reset(); ds.write(0xCC); ds.write(0x44);                                    // total conversion start
  for (uint32_t _t = millis(); !timeover(_t, 200ul);) if (ds.read_bit()) break;  // wait for complete conversion (200ms max)
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
  if (secure & 0x80) {                                              // overheats:
    int8_t _dtemp = constrain(3 - (millis()-tengrun)/600000ul, 1, 3);
    if ( (temps[DIF][PCB]  > 1     ) || (temps[CUR][PCB]  > 60) ||  //   pcb
         (temps[DIF][OUTS] > _dtemp) || (temps[CUR][OUTS] > 60) ||  //   outside
         (temps[DIF][VEH]  > _dtemp) || (temps[CUR][VEH]  > 75) )   //   vehicle
        alarm |= 0xA0, ign.set(0);
  }
}

void setup() {
  wdt_enable(WDTO_4S);
  analogReference(DEFAULT); pinMode(NEUTRAL_PIN, INPUT_PULLUP);
  DDRD &= ~0x0C; PORTD |= 0x0C; EICRA = 0x0A; EIFR = EIFR;    // D2,D3: input, pullup, falling, clear
  modem.begin(UART_BPS); modem.setTimeout(DEF_TIMEOUT);
  if (neutral()) secure = 0x87;
  #if DEBUG_LVL
  debug.begin(UART_BPS); dbg("< DEBUG MODE >");
  #endif
  modemreset();
}

void loop() {
  wdt_reset();
  #if DEBUG_LVL & 1
  while (debug.available()) modem.write(debug.read());
  #endif
  if (modem.available()) athandling();
  if (starter.active()) ign.set(0);
  if (setupmode) {
    if (EIFR) {
      _delay_ms(100);
      modem.println(F("AT+BTSPPSEND"));
      if (waitresp("> ")) modem.println(EIFR & 1 ? F(" ! (>_<) !") : F(" (-_-)")), modem.write(0x1A), waitresp();
      EIFR = EIFR;
    }
    return;
  }
  led.set( (secure & 0x80) && ((millis() & 0x3FFul) > 0x370ul) );
  if (engrun()) {
    tengrun = millis();
    if (tengstart == 0ul) tengstart = millis();
  } else {
    if (tengstart != 0ul) moto( ADD_MOTO, (millis()-tengstart)/60000 ), tengstart = 0ul;  
  }
  if ( pump.active() && !neutral() && (recmode & 0x01) ) dvr.twitch(120000ul);
  if (keepconn && (callstate < 2)) {
    if (timeover(tsend, 27000ul)) if (sendstart()) { modem.write(0xC0); modem.write(byte(0x00)); if (sendfin()) tsend = millis(); } // ping
    if (timeover(tresp, 60000ul))  modemshut();  // broker isn't responding!
  }
  if (timeover(t20sec, 20000ul)) { t20sec = millis();
    dsupdate();
    if ((callstate != 1) && !modem.available()) modem.println(F("AT+CPAS")); // query cellular status - freez calling state fix
  }
  if (timeover(t1min, 60000ul)) { t1min = millis();
    if (warmup) warmtimer = (warmtimer > 0) ? warmtimer-1 : 0;
  }
  
  if (warmup) {
    warmup = pump.active() && neutral() && (warmtemp > temps[CUR][ENG]) && (warmtimer > 0);   
    if (!warmup) ign.set(0);
  }
  if (secure & 0x80) {
    if (EIFR) {
      if (tshockdet) {
        if (timeover(tshockdet, 100ul)) alarm |= ((EIFR & 0x01) && (secure & 0x02)) ? 0x82 : (((secure & 0x01) && !engrun()) ? 0x41 : 0), tshockdet = 0ul, EIFR = EIFR;  // CHECK
      } else {
        tshockdet = millis();
      }
    }
    if (hood.change() == 1) alarm |= 0x84;
    if (door.change() == 1) alarm |= 0x88;
    if (pump.change() == 1) alarm |= 0x90;
  }
  if (alarm & 0x80) {
    nrecall = 10;
    if (recmode & 0x02) dvr.twitch(60000ul);
    if (callstate < 0x04) {
      if (secure & 0x04) siren.twitch(15000ul);
      flash.twitch(20000ul);
    }
  }
  if ( (alarm & 0x3E) && (callstate == 1) )
    if ( ((nrecall > 0) && timeover(tlastcall, 120000ul)) || (alarm & 0x80) )
      modem.println(F("AT+DDET=1,500,1;D>\"admin\";")), tlastcall = millis(), nrecall--, callstate = 0;
  if (alarm & 0x40) {
    if (recmode & 0x02) dvr.twitch(30000ul);
    if (!smsnotify || timeover(tlastmsg, 5000ul)) notify("BANG! (>_<)", smsnotify ? SMS : PUSH+SMS), tlastmsg = millis();
//    if (smsalert) alarm &= ~0x01;
    if (!siren.on() && (secure & 0x04)) siren.twitch(50ul, 5);
    if (!flash.on()) flash.twitch(2000ul);
  }
  if ( (alarm & 0x3E) && (callstate == 0x0C) )
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
