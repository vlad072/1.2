#include <avr/wdt.h>
#include <AltSoftSerial.h>
#include <OneWire.h>
#include "data.h"
#include "pins.h"

AltSoftSerial lin;
OneWire ds(ONEWIRE_PIN);
Input  door(DOOR_PIN), hood(HOOD_PIN), pump(PUMP_PIN);
Output ign(IGN_PIN), siren(SIREN_PIN), flash(FLASH_PIN), dvr(DVR_PIN), led(LED_PIN);
InOut  starter(STARTER_SUPPLY_PIN, STARTER_PIN);
Adc    vbatt(BATT_PIN, 48.54f), ntrl(NEUTRAL_PIN, 200.48f);

void(*reboot)(void) = 0;
#define timeover(var, timeout) ( (millis() - var) >= timeout )
void hexprint(Print& uart, uint8_t* arr, uint8_t len = 1) {
  for (uint8_t _i = 0; _i < len; _i++) {
    if (arr[_i] < 0x10) uart.write('0');
    uart.print(arr[_i], HEX);
    if (_i < (len-1)) uart.write(' ');
  }
}
bool waitresp(char* str = NULL, uint32_t timeout = DEF_TIMEOUT) {
  uint8_t _i = 0; char _c = 0; bool _ret = 0;
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
void starting() {
  if ( pump.active() || !neutral() || ((warmtemp - temps[CUR][ENG]) < 5) || (vbatt.value() < 11.0f) ) return;
  ign.set(1); delay(3000ul); wdt_reset();
  uint32_t _rotate = 800ul; int8_t _tc = 0;
  if (temps[CUR][ENG] != -127)
    _tc = temps[CUR][ENG];
  else if (temps[CUR][PCB] != -127)
    _tc = temps[CUR][PCB];
  if (_tc < 0) _rotate -= _tc*10;
  starter.set(1); delay(_rotate); starter.set(0); wdt_reset();
  delay(100); if (starter.active()) ign.set(0);
  delay(3000); wdt_reset();
  warmup = pump.active();
  if (warmup) warmtimer = 20; else ign.set(0), delay(250);
  pump.change(); EIFR = EIFR; t1min = millis();
}

void modemreset() {
  modem.println(F("ATZ+CFUN=1,1")); waitresp();  // user profile recovery; reboot modem 
  callstate = 0; tsend = tresp = millis();
  shuts = 0;
}
void modemreconf() {
  modem.print  (F("AT&F+IPR="));  modem.println(MODEM_BPS, DEC);       waitresp(); // usart speed
  modem.print  (F("AT+BTHOST=")); modem.println(cid);                 waitresp(); // blue host name
  modem.println(F("AT+CMIC=0,15;+BTPAIRCFG=1;+CLCC=1;E0&W")); waitresp();
}
void modemshut() {
  if (shuts < 3) {
    modem.println(F("AT")); if (!waitresp("OK\r\n")) modem.write(0x1A), waitresp();
    modem.println(F("AT+CIPHEAD=1;+CIPQSEND=1;+CIPSHUT"));
    tsend = tresp = millis();
    shuts++;
  } else
    modemreset();
}
void stoptrack() {                                 // stop curr track
  if (callstate & 0x08) {
    modem.println(F("AT+CREC=5"));
    if (waitresp("OK\r\n")) callstate &= ~0x08;
  }  
}
void play(const char* track) {
  if (callstate < 0x04) return;
  stoptrack();
  modem.print(F("AT+CREC=4,C:\\User\\")); modem.print(track); modem.println(F(".amr,0,100"));
  if (waitresp("OK\r\n")) callstate = 0x0C;
}
bool sendstart() {
  modem.println(F("AT+CIPSEND"));
  return waitresp("> ", 500ul);
}
bool sendfin() {
  modem.write(0x1A);
  return waitresp("DATA ACCEPT:");
}
void pub(const char* topic, const char* payl, uint8_t retn = 0) {
  uint8_t _tlen = strlen(pref()) + 4 + strlen(topic);
  modem.write(0x30 | retn);
  modem.write(2+ _tlen + strlen(payl));
  modem.write((byte)0x00); modem.write(_tlen);
  modem.print(pref()); modem.print("inf/"); modem.print(topic);
  modem.print(payl);
}
void pubn(const char* topic, float val, uint8_t dap = 0) {
  char _st[8] = "";
  dtostrf(val, 0, dap, _st); pub(topic, _st);
}
void sub() {
  uint8_t _tlen = strlen(pref()) + 5;             //pref+cmd/#
  modem.write(0x82);                              //type=8, qos=1
  modem.write(_tlen + 5);                         //remaining length
  modem.write((byte)0x00); modem.write(0x01);     //message id
  modem.write((byte)0x00); modem.write(_tlen);    //topic length
  modem.print(pref()); modem.print("cmd/#");      //topic
  modem.write((byte)0x00);                        //qos
}
void brokercon () {
  if (!sendstart()) return;
  modem.write(0x10);
  modem.write( 4 + 2+6 + 2+strlen(cid) + 2+strlen(pref())+10 + 2+1 + 2+strlen(user()) + 2+strlen(pass()) );
  modem.write((byte)0x00); modem.write(0x06);  modem.print("MQIsdp"); modem.write(3);
  modem.write(0xE6); modem.write((byte)0x00); modem.write(60);               // flags, ka timout | E/C -retain/not
  modem.write((byte)0x00); modem.write(strlen(cid));    modem.print(cid);
  modem.write((byte)0x00); modem.write(strlen(pref())+10); modem.print(pref()); modem.print("inf/online");  // will topic
  modem.write((byte)0x00); modem.write(0x01);           modem.write('0');    // will message
  modem.write((byte)0x00); modem.write(strlen(user())); modem.print(user());
  modem.write((byte)0x00); modem.write(strlen(pass())); modem.print(pass());
  sendfin();
}
void pubbalance(const char* inp) {
  uint8_t _i = 0; char _bal[8] = "";
  for (; inp[_i]; _i += 4) {
    if (!strncmp(inp+_i, "003", 3) && isdigit(inp[_i+3])) break;
    if (!strncmp(inp+_i, "002D", 4)) break;
  }
  for (;strlen(_bal)+1 < sizeof(_bal); _i += 4)
    if      (!strncmp(inp+_i, "002D", 4))                       strcat (_bal, "-");
//    else if (!strncmp(inp+_i, "002E", 4))                       strcat (_bal, "."); // float value
    else if (!strncmp(inp+_i, "003" , 3) && isdigit(inp[_i+3])) strncat(_bal, inp+_i+3, 1);
    else break;
  pub("balance", _bal, RETAIN);
}
void publocate(const char* inp) {
  char _link[24] = "0,0";
  if (inp[0] == '0') {
    char* _eptr = strchr(inp, ',') + 1;
    char* _nptr = strchr(_eptr, ',') + 1;
    uint8_t  _elen = _nptr - _eptr - 1;
    uint8_t  _nlen = strchr(_nptr, ',') - _nptr;
//  char  _link[64] = "https://www.google.com/maps/place/";
    strncpy(_link, _nptr, _nlen+1); _link[_nlen+1] = 0; strncat(_link, _eptr, _elen);
  }
  pub("place", _link);
}
void notify(char* txt = NULL, uint8_t mode = PUSH) {
  if (txt) strcpy(msg, txt);
  if (mode & PUSH) if (sendstart()) {
    pub("notify", msg);
    if (sendfin()) return;
  }
  if (mode & SMS) modem.println(F("AT+CPBF=\"admin\""));  
}
uint8_t hash(uint8_t key, uint8_t sign) {
  uint16_t _ret = (key ^ sign) & 0x0F; // xor 1=close 2=open
  for (uint8_t _i = (key & 7) == 7 ? 5 : 4; _i < 8; _i++) {
    bitSet(_ret, _i);
    if ( !bitRead(key, _i) ) break;
  }
  _ret <<= (key & 7);
  return lowByte(_ret) | highByte(_ret);
}
void arming(bool arm) {
  if (setupmode) return;
  bool _op = door.active() || hood.active();
  if (arm) {                   // ================== lock ======================
    if (_op) siren.twitch(100ul, 3); else if (secure & 0x40) siren.set(1), _delay_ms(10), siren.set(0);
    secure |= 0xFF;
    door.change(); hood.change(); EIFR = EIFR; alarm = alarmcash = 0;
  } else {                    // ================== unlock =====================
    if (secure & 0x40)
      if      (alarm & 0x3E)  siren.twitch(100ul, 3);
      else if (alarm & 0x01)  siren.twitch(30ul,  2);
      else                    siren.set(1), _delay_ms(10), siren.set(0);
    secure &= ~0x80;
  }                           // ===============================================
  if (sendstart()) pubn("secure", secure), sendfin();
  if ( _op && arm) {
    notify("VEH ISN'T CLOSED!", PUSH+SMS);
  } else {
    strcat(msg, arm ? " CLOSE":" OPEN");
    notify();
  }
}
void linsend(uint8_t sign, uint8_t tail = DOOR_TAIL) {
  uint8_t _key[3] = {0, 0, 0};
  while ( (_key[0] = random(0xFF)) & 7 == 0 );
  _key[1] = hash(_key[0], sign);
  _key[2] = _key[0] ^ _key[1];
  lin.write(0x20); _delay_us(39110);
  lin.write(_key, sizeof(_key));
  lin.write(tail);
  lin.flushOutput();
  _delay_ms(10); lin.flushInput();
}
void exec(const char* topic, const char* payl) { //++++++++++++++  USER COMMAND IMPLEMENTAION  ++++++++++++++++++
  if        (!strcmp(topic, "wutm"))  {   //*************** warmup timer **************
    warmtimer = atoi(payl);
  } else if (!strcmp(topic, "wutp"))  {   //*************** warmup temperature ********
    warmtemp = atoi(payl);
  } else if (!strcmp(topic, "engr")) {    //*************** starting engine ***********
    if (payl[0] > '0') starting();
    else               ign.set(0), warmup = 0, _delay_ms(250), EIFR = EIFR;
  } else if (!strcmp(topic, "lock"))  {   //*************** locking *******************
    bool _op = door.active() || hood.active(); bool _lk = !bitRead(secure, 7);
    linsend(_lk ? CLOSE_SIGN:OPEN_SIGN);
    strcpy(msg, "REM"); arming(_lk);
  } else if (!strcmp(topic, "shock")) {   //********** enable/dis shock sensor ********
    secure &= ~0x03; secure |= (payl[0]-'0') & 0x03;
  } else if (!strcmp(topic, "fire")) {   //*********** enable/dis fire detect *********
    bitWrite(secure, 5, payl[0] > '0');
  } else if (!strcmp(topic, "siren")) {   //*************** siren active **************
    bitWrite(secure, 6, payl[0] > '0');
  } else if (!strcmp(topic, "sms")) {     //************** sms notify ****************
    smsnotify = bool(atoi(payl));
  } else if (!strcmp(topic, "dvr")) {     //************ drive recording *************
    dvrmode = atoi(payl);
  } else if (!strcmp(topic, "btpwr")) {   //*************** blue power on/off ********
    bool _pw = bool(atoi(payl)); wdt_reset();
    modem.print(F("AT+BTPOWER=")); modem.println(_pw ? "1" : "0"); waitresp(NULL, 3900ul);
    wdt_reset();
    if (_pw) {
      modem.println(F("AT+BTVIS=0"));
      if (!waitresp("OK\r\n")) modem.println(F("AT+BTPOWER=0")), waitresp(NULL, 2900ul);
      wdt_reset();
    }
  } else if (!strcmp(topic, "btansw")) {   //************ hfp auto-answer *************
    btansw = bool(atoi(payl));
  } else if (!strcmp(topic, "setup")) {    //************** enter setup ***************
    modem.println(F("AT+BTPOWER=1")); wdt_reset();
    waitresp(NULL, 3900ul);    wdt_reset();
    modem.println(F("AT+BTVIS=1"));
    if (waitresp("OK\r\n")) led.set(setupmode = 1);    
  } else if (!strcmp(topic, "alarm")) {    //***************** alarms *****************
    switch (payl[0]) {
      case '1': alarm = alarmcash = 0;                                                    // clear
      case '2': siren.set(0); flash.set(0); break;                                        // mute
      case '3': if (secure & 0x40) siren.twitch(500ul, 3); flash.twitch(3000ul); return;  // raise
    }
  } else if (!strcmp(topic, "upd"))   {    //*************** send inform to dash *******
    switch (payl[0]) {
      case '1': modem.println(F("AT+SAPBR=2,1")); return;                       // locate
      case '2': modem.print(F("AT+CUSD=1,")); modem.println(payl+1); return;    // balance
//    case '3': refresh();                                                      // common                 
    } // swith update tabs
  } else { /*unkn cmd!*/ }
  if (sendstart()) {                        // update dash                
    pubn("outtp" , temps[CUR][OUTS]);
    pubn("engtp" , temps[CUR][ENG]);
    pubn("pcbtp" , temps[CUR][PCB]);
    pubn("vehtp" , temps[CUR][VEH]);
    pubn("wutp"  , warmtemp);
    pubn("wutm"  , warmtimer);
    pubn("vbatt" , vbatt.value(), 2);
    pubn("moto"  , moto()/60.0f , 1);                          
    pub ("engr"  , pump.active() ? "1" : "0");       
    pubn("doors" , hood.active() | (door.active()<<1));
    pubn("secure", secure);
    pub ("sms"   , smsnotify ? "1" : "0");
    pub ("btansw", btansw    ? "1" : "0");
    pubn("dvr"   , dvrmode);
    pubn("alarm" , alarmcash);
    pub ("fwv"   , fwver);
    if (sendfin()) modem.println(F("AT+CSQ"));
  }
}
void ipd( const uint8_t* frame, uint8_t len = 1 ) { //++++++++++++++  PROCESSING DATA FROM BROKER  ++++++++++++++++++
  for (uint8_t _ofs = 0; _ofs < len;) {
    if ( (frame[_ofs] == 0xD0) && (frame[_ofs+1] == 0x00) ) {            // pingresp
    } else  if (frame[_ofs] == 0x20) {                                   // connak
      if (frame[_ofs+3] == 0x00) {
        //char _sub[strlen(pref())+4+2]; strcpy(_sub, pref()); strcat(_sub, "cmd/#");
        if (sendstart()) sub(), sendfin();
      } else {
        modemshut(); return;
      }
    } else  if (frame[_ofs] == 0x90) {                                   // suback
      if (sendstart()) pub("online", "1", RETAIN), sendfin();
    } else if (frame[_ofs] == 0x30) {                                    // pub recv
      char*   _tptr = (char*)(frame + _ofs + 4 + strlen(pref())+4);
      uint8_t _tlen = frame[_ofs+3] - strlen(pref())-4;
      char*   _pptr = (char*)(frame + _ofs + 4 + frame[_ofs+3]) ;
      uint8_t _plen = frame[_ofs+1] - frame[_ofs+3] - 2;
      char  _topic[_tlen+1]; strncpy(_topic, _tptr, _tlen); _topic[_tlen] = 0;
      char  _payl [_plen+1]; strncpy(_payl,  _pptr, _plen); _payl [_plen] = 0;
      exec(_topic, _payl);
    } //else break; //unexpected header
    _ofs += 2 + frame[_ofs+1];
  } // cycle
}  // ipd func
#define btprintln(content) modem.println(F("AT+BTSPPSEND")); if (waitresp("> ")) modem.println(content), modem.write(0x1A), waitresp()
void btspp(const char* frame, uint8_t len) { //+++++++++++++++++++ BLUETOOTH SETUP THROUGH SPP  ++++++++++++++++++
  char* _paramptr = strchr(frame, '=') + 1; uint8_t _paramlen = constrain( len-(_paramptr-frame), 0, PAGESZ-1 );
  if        (!strncmp(frame, "srv", 3)) {
    if (_paramptr[0] == '?') {
      modem.println(F("AT+BTSPPSEND")); if (waitresp("> ")) {
        modem.println();
        modem.print("srv="); modem.println(broker());
        modem.print("usr="); modem.println(user());
        modem.print("pwd="); modem.println(pass());
        modem.print("pref="); modem.println(pref());
        modem.write(0x1A); waitresp();
      }
    } else {
      broker(_paramptr, _paramlen);
      btprintln(strncmp(broker(), _paramptr, _paramlen) ? F(" fail!") : F(" ok"));
      //modem.println(F("AT+BTSPPSEND")); if (waitresp("> ")) modem.println(strncmp(broker(), _paramptr, _paramlen) ? F(" fail!") : F(" ok")), modem.write(0x1A), waitresp();
    }
  } else if (!strncmp(frame, "usr", 3)) {
    user(_paramptr, _paramlen);
    btprintln(strncmp(user(), _paramptr, _paramlen) ? F(" fail!") : F(" ok"));
  } else if (!strncmp(frame, "pwd", 3)) {
    pass(_paramptr, _paramlen);
    btprintln(strncmp(pass(), _paramptr, _paramlen) ? F(" fail!") : F(" ok"));
  } else if (!strncmp(frame, "pref", 4)) {
    pref(_paramptr, _paramlen);
   btprintln(strncmp(pref(), _paramptr, _paramlen) ? F(" fail!") : F(" ok"));
  } else if (!strncmp(frame, "sens", 4)) {                          // ============ sensors ==============
    uint8_t _sid[8] = {0};  uint8_t _idx = _paramptr[0] - '0';
    if ( (_idx >= PCB) && (_idx <= VEH) ) {
      ds.reset_search(); bool _found = ds.search(_sid);
      if (_idx != PCB) {
        _found = 0;
        for (uint8_t _i = 0; _i < 8; _i++) _found |= _sid[_i] != sid(PCB)[_i];
        if (!_found) _found = ds.search(_sid);
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
        modem.println(); modem.write(0x1A); waitresp();
      }
    } else {
      modem.println(F("AT+BTSPPSEND")); if (waitresp("> ")) {
        modem.println(F("\r\nstored:"));
        for (uint8_t _i = PCB; _i <= VEH; _i++ ) hexprint(modem, sid(_i), 8), modem.println();
        modem.println(F("scan:")); ds.reset_search();
        while (ds.search(_sid)) hexprint(modem, _sid, 8), modem.println();        
        modem.write(0x1A); waitresp();
      }
    }                                                               // ============ sensors ==============
  } else if (!strncmp(frame, "btpin", 5) && (_paramlen == 4)) {
    btprintln(F(" reconnect with new pin!"));
    modem.println(F("AT+BTUNPAIR=0")); waitresp();
    modem.print(F("AT+BTPAIRCFG=1,")); modem.write(_paramptr, _paramlen); modem.println(); waitresp();
  } else if (!strncmp(frame, "moto", 4)) {                            // ======== running hours ============
    char _m[_paramlen + 1]; strncpy(_m, _paramptr, _paramlen); _m[_paramlen] = 0;
    moto(WRITE_MOTO, atoi(_m)*60);
    btprintln( (moto() == atoi(_m)*60) ? F(" ok") : F(" fail!") );
  } else if (frame[0] == '?') {
    modem.println(F("AT+BTSPPSEND")); if (waitresp("> ")) {
      modem.println();
      modem.println(F("moto=<N> : working hours"));
      modem.println(F("srv=<addr,port> | <?>-info")); 
      modem.println(F("usr=<username>"));
      modem.println(F("pwd=<password>"));
      modem.println(F("pref=<topics prefix>"));
      modem.println(F("sens=<N> : 0-pcb,1-eng,2-outs,3-vehcl,?-info"));
      modem.println(F("btpin=<NNNN> : bt devices detach & upd pin"));
      modem.println(F("exit"));
      modem.println(F("-=hit the car to shock test=-"));
      modem.write(0x1A); waitresp();
    }
  } else if ( !strncmp(frame, "exit", 4) ) {
    modem.println(F("AT+BTVIS=0")); waitresp();
    btprintln(F(" setup over!"));
    eepage = 0; setupmode = 0; led.set(0);
    if (keepconn) modemshut();
  } else {
    btprintln(F(" error!"));
  }
}
void dtmf(const char key, bool lon) { //+++++++++++++++++++ ONCALL KEYPRESS HANDLING  ++++++++++++++++++
  stoptrack();
  if ( ((key == '#') || (key == '0')) && lon ) {                               // emergency recovery
    wdt_disable(); modem.setTimeout(5000);
    play((key == '#') ? "recov" : "reboot"), modem.find("+CREC: 0");
    modem.println(F("ATH")); waitresp();
    if (sendstart()) modem.write(0xE0), modem.write((byte)0x00), sendfin(); // disconnect
    if (key == '#') modemreconf();
    reboot();
  } else if (key == '1') {                                                      // help notice
    play("help");
  } else if (key == '2') {                                                      // keep broker connection
    keepconn = !keepconn; play(keepconn ? "keep1" : "keep0");
  } else if (key == '3') {                                                      // shock sensor enble
    secure ^= 0x03; play(secure & 0x03 ? "shk1" : "shk0");
  } else if (key == '4') {                                                      // bt on/off
    modem.println(F("AT+BTPOWER=0")); wdt_reset();
    bool _btpw = !waitresp("OK\r\n", 3000ul);
    if (_btpw) {
      modem.println(F("AT+BTPOWER=1")); wdt_reset();
      _btpw &= waitresp("OK\r\n", 3800ul);
    }
    play(_btpw ? "bt1" : "bt0");
  } else if (key == '5') {                                                      // dvr run to 1min
    if (dvr.on()) dvr.set(0); else dvr.twitch(600000ul);
    play(dvr.on() ? "dvr1" : "dvr0");
  } else if ((key == '9') && lon) {
    if (pump.active()) ign.set(0), _delay_ms(250), EIFR = EIFR; else starting();
    _delay_ms(500);
    play(pump.active() ? "eng1" : "eng0");
  } else if (key == '*') {
    if (lon) {                                                                  // setup mode
      if (setupmode) {
        modem.println(F("AT+BTVIS=0")); waitresp();
        eepage = 0; setupmode = 0;
      } else {
        modem.println(F("AT+BTPOWER=1")); wdt_reset();
        waitresp(NULL, 3900ul);    wdt_reset();
        modem.println(F("AT+BTVIS=1"));
        if (waitresp("OK\r\n")) setupmode = 1;
      }
      led.set(setupmode); play(setupmode ? "setup1" : "setup0");
    } else {                                                                    // replay alarms
      if (alarmcash) alarm = alarmcash; else play("noalm");
    }
  } else if (key == '#') {                                                      // clear alarms
    alarm = alarmcash = 0; play("almclr");
  } else {
    play("error"); // unknown command
  }    
}
void athandling() { //++++++++++++++++++ AT RESPONSES HANDLING ++++++++++++++++++++
//  while (modem.available()) if (modem.peek() < '+') modem.read(); else break;                
//  if (!modem.available()) return;
  uint8_t _atlen = 0; char* _ptr = NULL;
  for (uint32_t _trecv = millis(); !timeover(_trecv, 10ul);) {
    if (!modem.available()) continue;
    char _ch = modem.read(); _trecv = millis();
    if ((_atlen+1) < sizeof(at)) at[_atlen++] = _ch;
  } at[_atlen] = 0;
  if (strstr(at, "SHUT OK")) if (keepconn && !setupmode) modem.print(F("AT+CIPMUX=0;+CIPSTART=TCP,")), modem.println(broker());
  if (strstr(at, "CONNECT OK")) brokercon();
  if (strstr(at, "Call Ready")) callstate = 0x01;
  if (strstr(at, "SMS Ready")
   || strstr(at, "DEACT")
   || strstr(at, "CLOSED")
   || strstr(at, "CONNECT FAIL")) modemshut();
  _ptr = strstr(at, "+SAPBR:"); if (_ptr) switch (_ptr[10]) {
    case '1': modem.println(F("AT+CIPGSMLOC=1,1")); break;
    case '3': modem.println(F("AT+SAPBR=1,1"));
  }
  _ptr = strstr(at, "+CLCC: 1,"); if (_ptr) {
    switch (_ptr[11]) {
      case '0': callstate = 0x04; siren.set(0); flash.set(0); play("hello"); break;
      case '2':
      case '3': callstate = 0x02; break;
      case '6':
      case '7': callstate = 0x01; tsend = tresp = millis() - 23000ul; break;
      case '4': modem.println(strstr(_ptr+17, "\"admin\"") ? F("AT+DDET=1,500,1;A") : F("ATH"));
    } t20sec = millis();
  }
  if (strstr(at, "+CREC: 0")) callstate &= ~0x08;
  if (strstr(at, "+CPAS: 0")) callstate  = 0x01;
  _ptr = strstr(at, "+DTMF:"); if (_ptr) dtmf(_ptr[7], atoi(_ptr+9) > 1000);
  _ptr = strstr(at, "+BTCONNECTING:"); if (_ptr) if (setupmode && strstr(_ptr+15, "\"SPP\"")) modem.println(F("AT+BTACPT=1"));
  _ptr = strstr(at, "+BTCONNECT:"); if (_ptr)
    if  (setupmode && strstr(_ptr+12, "\"SPP\"")) { 
      modem.println(F("AT+BTSPPGET=0")); waitresp();
      btprintln(F("Welcome to BlackBox setup!"));
    } else {
      if ( (secure & 0x80) && (_ptr[12] > '0') && (!pump.active() || warmup) ) {
        linsend(OPEN_SIGN);
        strcpy(msg, "BT ");
        char* _nm = strchr(_ptr+10, '"'); uint8_t _ln = constrain( strchr(_nm+1, '"') - _nm + 1, 2, 20 );
        strncat(msg, _nm, _ln);
        arming(0);      
      }
      modem.println(F("AT+BTSTATUS?"));
    }
  if (strstr(at, "+BTDISCONN:")) {
    if (!(secure & 0x80) && !pump.active() && !setupmode) {
      linsend(CLOSE_SIGN);
      strcpy(msg, "BT");
      arming(1);
    }
    modem.println(F("AT+BTSTATUS?"));
  }
  _ptr = strstr(at, "+BTPAIR:");  if (_ptr) {
    strcpy(msg, "BT ADD ");
    char* _nm = strchr(_ptr+7, '"'); uint8_t _ln = constrain( strchr(_nm+1, '"') - _nm + 1, 2, 23 );
    strncat(msg, _nm, _ln);
    notify(NULL, PUSH+SMS);
  }
  _ptr = strstr(at, "+BTSTATUS:");  if (_ptr) {
    uint8_t _pair = 0; uint8_t _conn = 0; char* _bptr = _ptr+12;
    while(1) {
      _bptr = strstr(_bptr, "\r\nP: "); if (!_bptr) break; _pair++; _bptr++;
    } _bptr = _ptr+12; while(1) {
      _bptr = strstr(_bptr, "\r\nC: "); if (!_bptr) break; _conn++; _bptr++;
    }
    uint8_t _st = (_pair & 0x0F) | ((_conn & 0x07) << 4) | ((_ptr[11] > '0') << 7); //bits: WCCCPPPP (W=power, C=connected, P=paired)
    if (sendstart()) pubn("btst", _st), pub("upd", "1"), sendfin();
  }
  if (strstr(at, "BTRING") && btansw) if (timeover(tbtring, 8000ul)) tbtring = millis(); else tbtring = 0, modem.println(F("AT+BTATA")); // with 2nd ring answer
//  if (strstr(at, "BTRING") && btansw) if ((millis() - tbtring) > 13000ul) tbtring = millis(); else if ((millis() - tbtring) > 8000ul) tbtring = 0, modem.println(F("AT+BTATA")); // with 3rd ring answer
  if    (strstr(at, "+CMTI:")) modem.println(F("AT+CMGDA=6"));//, modem.find("OK\r\n"); // wipe sms
  _ptr = strstr(at, "+CPBF:");  if (_ptr) {                                             // recv ph.num "admin" then send sms
    char* _nm = strchr(_ptr+6, '"'); if (_nm) {
      uint8_t _ln = strchr(_nm+1, '"') - _nm + 1;
      modem.print(F("AT+CMGS=")); modem.write(_nm, _ln); modem.println();
      if (waitresp("> ")) modem.write(msg), modem.write(0x1A);
    }
  }
  _ptr = strstr(at, "+CUSD:"); if (_ptr) if (sendstart()) pubbalance(_ptr+11), sendfin();
  _ptr = strstr(at, "+CSQ:");  if (_ptr) {
    char* _qptr = _ptr + 6;
    char _sq[3] = ""; strncat(_sq, _qptr, strchr(_qptr, ',') - _qptr);
    if (sendstart()) {
      pub("sq", _sq);
      if (sendfin()) modem.println(F("AT+BTSTATUS?"));
    }
  }
  _ptr = strstr(at, "+CIPGSMLOC:"); if (_ptr) if (sendstart()) publocate(_ptr+12), sendfin();
  _ptr = strstr(at, "+IPD");        if (_ptr) tresp = millis(), shuts = 0, ipd( strchr(_ptr + 6, ':') + 1, atoi(_ptr + 5) );
  _ptr = strstr(at, "+BTSPPDATA:"); if (_ptr) btspp( strchr(strchr(_ptr + 11, ',') + 1, ',') + 1, atoi(strchr(_ptr + 11, ',') + 1) );
}
void dsupdate() {
  ds.reset(); ds.write(0xCC); ds.write(0x44);                                    // total conversion start
  for (uint32_t _t = millis(); !timeover(_t, 200ul);) if (ds.read_bit()) break;  // wait for complete conversion (200ms max)
  for( uint8_t _i = PCB; _i <= VEH; _i++ ) {
    ds.reset(); ds.select(sid(_i)); ds.write(0xBE);
    int8_t _raw = ds.read() >> 4; _raw |= ds.read() << 4;
    ds.read(); ds.read();            // th, tl
    if (ds.read() == 0x1F)           // 9-bit conf?
      temps[DIF][_i] = ((temps[CUR][_i] == -127) || (_raw == -127)) ? 0 : _raw - temps[CUR][_i],
      temps[CUR][_i] = _raw;
    else
      temps[DIF][_i] = 0, temps[CUR][_i] = -127;
  }// ds.reset();
  if (secure & 0xA0) {                                              // overheats:
    int8_t _dtemp = constrain(3 - (millis()-tengrun)/600000ul, 1, 3);
    if ( (temps[DIF][PCB]  > 1     ) || (temps[CUR][PCB]  > 70) ||  //   pcb
         (temps[DIF][OUTS] > _dtemp) || (temps[CUR][OUTS] > 70) ||  //   outside
         (temps[DIF][VEH]  > _dtemp) || (temps[CUR][VEH]  > 80) )   //   vehicle
        alarm |= 0xA0, ign.set(0);
  }
}
void linhandling() {
  uint8_t _buf[5]; uint8_t _sz = 0; uint8_t _in = 0;
  for ( uint32_t _t = millis(); !timeover(_t, 50ul); ) if (lin.available() > 0) {
    _in = lin.read();
    if (_sz < 5) _buf[_sz] = _in;
    _sz++; _t = millis();
  }
  if (_sz != 5) return;
  if (_buf[0] != 0x02) return;
  if (_buf[4] != DOOR_TAIL) return;
  if (_buf[1] & 7 == 0) return;
  if (_buf[1] ^ _buf[2] ^ _buf[3]) return;
  if (_buf[2] == hash(_buf[1], OPEN_SIGN))  strcpy(msg, "KEY"), arming(0);
  if (_buf[2] == hash(_buf[1], CLOSE_SIGN)) strcpy(msg, "KEY"), arming(1);
}

void setup() {
  wdt_enable(WDTO_4S);
  analogReference(DEFAULT); pinMode(NEUTRAL_PIN, INPUT_PULLUP);
  DDRD &= ~0x0C; PORTD |= 0x0C; EICRA = 0x0A; EIFR = EIFR;    // D2,D3: input, pullup, falling, clear
//  ds.reset(); ds.write(0xCC); ds.write(0x4E); ds.write(0x7F), ds.write(0xFF); ds.write(0x1F); //dsinit
  lin.begin(LIN_BPS, SERIAL_8E1); _delay_ms(100);
  modem.begin(MODEM_BPS); _delay_ms(100); modem.setTimeout(DEF_TIMEOUT);
  if (neutral()) secure = 0xFF;
  modemreset();
  led.twitch(500ul, 3);
}

void loop() {
  batt = vbatt.value();
  wdt_reset();
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
  if (lin.available() > 0) linhandling();
  led.set( (secure & 0x80) && ((millis() & 0x3FFul) > 0x370ul) );
  if (dvrmode & 0x08) if (batt > 11.7f) dvr.twitch(5000ul);
  if (pump.active()) {
    tengrun = millis();
    if (tengstart == 0ul) tengstart = millis();
  } else {
    if (tengstart != 0ul) moto( ADD_MOTO, (millis()-tengstart)/60000 ), tengstart = 0ul;  
  }
  if (keepconn) {
    if (timeover(tsend, 27000ul)) if (sendstart()) { modem.write(0xC0); modem.write((byte)0x00); if (sendfin()) tsend = millis(); } // ping
    if (timeover(tresp, 60000ul) && (callstate < 0x02))  modemshut();  // broker isn't responding!
  }
  if (timeover(t20sec, 20000ul)) { t20sec = millis();
    dsupdate();
    if ((callstate != 0x01) && !modem.available()) modem.println(F("AT+CPAS")); // query cellular status - freez calling state fix
  }
  if (timeover(t1min, 60000ul)) { t1min = millis();
    if (warmup) warmtimer = (warmtimer > 0) ? warmtimer-1 : 0;
  }
  
  if (warmup) {
    warmup = pump.active() && neutral() && (warmtemp > temps[CUR][ENG]) && (warmtimer > 0);   
    if (!warmup) ign.set(0), _delay_ms(250), EIFR = EIFR;
  }
  if (secure & 0x80) {
    if (EIFR && !warmup) {
      if (tshockdet) {
     // if (timeover(tshockdet, 100ul)) alarm |= ((EIFR & 0x01) && (secure & 0x02)) ? 0x82 : ((secure & 0x01) ? 0x41 : 0), tshockdet = 0ul, EIFR = EIFR;  // CHECK
        if (timeover(tshockdet, 100ul)) alarm |= ((EIFR & 0x01) && (secure & 0x02)) ? 0x82 : (((secure & 0x01) && !pump.active()) ? 0x41 : 0), tshockdet = 0ul, EIFR = EIFR;  // CHECK
      } else {
        tshockdet = millis();
      }
    }
    if (hood.change() == 1) alarm |= 0x84;
    if (door.change() == 1) alarm |= 0x88;
    if (pump.change() == 1) alarm |= 0x90;
    if (batt < lowbatt) {
      strcpy(msg, "LOW BATT: "); dtostrf(batt, 5, 2, msg+10); notify();
      lowbatt -= 0.25f;
    } else {
      if (batt > 12.1f) lowbatt = 12.0f;
    }
  } else {
    if ((dvrmode & 0x01) && pump.active()) dvr.twitch(5000ul);
  }
  alarmcash |= alarm & 0x3F;
  if (alarm & 0x80) {
    nrecall = 10;
    if (dvrmode & 0x02) dvr.twitch(60000ul);
    if (callstate < 0x04) {
      if (secure & 0x40) siren.twitch(15000ul);
      flash.twitch(20000ul);
    }
  }
  if ( (alarm & 0x3E) && (callstate == 1) )
    if ( ((nrecall > 0) && timeover(tlastcall, 120000ul)) || (alarm & 0x80) )
      modem.println(F("AT+DDET=1,500,1;D>\"admin\";")), tlastcall = millis(), nrecall--, callstate = 2;
  if (alarm & 0x40) {
    if (dvrmode & 0x02) dvr.twitch(30000ul);
    if (callstate < 0x04){
      if (!smsnotify || timeover(tlastmsg, 5000ul)) notify("BANG! (>_<)", smsnotify ? SMS : PUSH+SMS), tlastmsg = millis(), alarm &= 0xFE;
      if (!siren.on() && (secure & 0x40)) siren.twitch(50ul, 5);
      if (!flash.on()) flash.twitch(2000ul);
    }
  }
  if ( (alarm & 0x3F) && (callstate == 0x04) )
    for ( uint8_t _i = 5; _i >= 0; _i-- ) {
      if (!bitRead(alarm, _i)) continue;         //  0( h01 ) = shock1 
      bitClear(alarm, _i);                       //  1( h02 ) = shock2
      if (_i == 5)  { play("fire");     break; } //  2( h04 ) = hood open
      if (_i == 4)  { play("ignon");    break; } //  3( h08 ) = door open
      if (_i == 3)  { play("dooropen"); break; } //  4( h10 ) = ignition on 
      if (_i == 2)  { play("hoodopen"); break; } //  5( h20 ) = fire detect
      if (_i == 1)  { play("shock2");   break; } //  6( h40 ) = NEW WARNING
      if (_i == 0)  { play("shock1");   break; } //  7( h80 ) = NEW ALARM
  }
  alarm &= ~0xC0;
  dvr.processing(); siren.processing(); flash.processing(); led.processing();
} // ************* LOOP +++++++++++++++++++++++
