#define DEBUG_LVL 0              // 1 = AT log, 2 = includeded dbg messages, 3 = both, 0 = nothing
//======================== pins ==========================
#define IGN_PIN              4
#define STARTER_PIN          5
#define SIREN_PIN            6
#define DVR_PIN              7
#define LOCK_PIN             8
#define UNLOCK_PIN           9
#define FLASH_PIN           10
#define MODEM_TX_PIN        11
#define MODEM_RX_PIN        12
#define LED_PIN             13
#define HOOD_PIN            A0
#define DOOR_PIN            A1
#define PUMP_PIN            A2
#define NEUTRAL_PIN         A3
#define STARTER_SUPPLY_PIN  A4
#define ONEWIRE_PIN         A5
#define SHOCK1_PIN           3
#define SHOCK2_PIN           2
#define BATT_PIN            A7
#define CURRENT_PIN         A6
//===================== statements ======================
#define PUSH        0x01
#define SMS         0x02
#define RETAIN      0x01
#define INC_MOTO    0xFFFE
#define READ_MOTO   0xFFFF
//======================= uart ==========================
#define debug           Serial
#define UART_BPS         57600
#define DEF_TIMEOUT       1000
//==================== temps indexes ====================
enum { PCB, ENG, OUTS, VEH }; // dallas order
enum { CUR, DIF };            // curent, difference
//======================= consts ========================
const char* proto  = "MQIsdp";      // mqtt protocol
const byte  ver    =  3;            // version
const char* cid    = "BlackBox";    // device signature
const char* willt  = "inf/online";  // will topic
//===================== variables ======================
char     at[128]    =  "";      // AT buffer
char     msg[32]    =  "";      // message buffer
bool     setupmode  =  false;   // initial settings mode
bool     keepconn   =  true;    // keep alive connection
bool     btansw     =  false;   // incoming calls through HSP auto-answer
byte     recmode    =  0x03;    // recording dvr 1 = drive, 2 = parking, 3 = both
uint32_t tsend      =  0;       // last packed sending point
uint32_t tresp      =  0;       // last broker responding point
uint32_t t20sec     =  0;       // 20 seconds interval
uint32_t t1min      =  0;       // 1 min interval
uint32_t tengrun    =  0;       // engine running last point
uint32_t tlastmsg   =  0;       // last message sending pont
uint32_t tbtring    =  0;       // last "btring" AT receive
uint32_t tlastcall  =  0;       // last calling point
uint32_t tshockdet  =  0;       // shock detection point
int8_t   nrecall    =  0;       // recall attempts
byte     callstate  =  0;       // undef=0, initial=1, alert=2, in_talk=4, channel_free=8 /// play_ready=0C
int8_t   warmtimer  =  20;      // engine warming count-down timer min 
int8_t   warmtemp   =  50;      // engine warming temperature limit deg.C 
byte     secure     =  0;       // bits: 7(80)=lock, 0(1)=shock low, 1(2)= shock high, 2(4) = siren active
bool     smsnotify  =  false;   // notifications through SMS
byte     alarm      =  0;       // alarm flags
bool     warmup     =  false;   // now engine warmup
int8_t   temps[2][4]  = { {-127, -127, -127, -127},   // self, engine, outside, vehacle 
                          {   0,    0,    0,    0} }; // different at last scan (t20sec)
//====================== eeprom =======================
#include <EEPROM.h>
byte rombuf[0x20] = {}; // eeprom map buffer
byte eepage = 0;        // stored page
char* broker(char* val = NULL, byte len = 0) {                            //  0..1F  ===== page 1 =====
  if (val && len) {
    eeprom_update_block(val, 0x00, len); eeprom_update_byte(len, 0); eepage = 0;
  } else {
    if (eepage != 1) eeprom_read_block(rombuf, 0x00, 0x20), eepage = 1;
    return rombuf;
  }
}
char* user(char* val = NULL, byte len = 0) {                              // 20..2F  ===== page 2 =====
  if (val && len) {
    eeprom_update_block(val, 0x20, len); eeprom_update_byte(0x20+len, 0); eepage = 0;
  } else {
    if (eepage != 2) eeprom_read_block(rombuf, 0x20, 0x20), eepage = 2;
    return rombuf;
  }
}
char* pass(char* val = NULL, byte len = 0) {                              // 30..3F
  if (val && len) {
    eeprom_update_block(val, 0x30, len); eeprom_update_byte(0x30+len, 0); eepage = 0;
  } else {  
    return user() + 0x10;
  } 
}
byte* sid(byte idx, byte* val = NULL) {                                    // 40..5F  ===== page 3 =====
  if (val) {
    eeprom_update_block(val, 0x40 + idx*8, 8); eepage = 0;
  } else {
    if (eepage != 3) eeprom_read_block(rombuf, 0x40, 0x20), eepage = 3;
    return rombuf + idx*8;
  }
}
uint16_t moto(uint16_t val = READ_MOTO) {                                  // 80..9F  ==== moto min ==== (invert!)
  byte _offs = ~eeprom_read_byte(0x9F); uint16_t _raw = ~eeprom_read_word(0x80+_offs);
  switch (val) {
    case INC_MOTO : if (_raw < 0xFFFF) _raw++, eeprom_write_word(0x80+_offs, ~_raw);
    case READ_MOTO: return _raw;
    default       : _offs = (_offs < 0x1C) ? _offs+2 : 0;
                    eeprom_write_byte(0x9F, ~_offs);
                    eeprom_update_word(0x80+_offs, ~val);
                    return ~eeprom_read_word(0x80+_offs);
  }
}

//=================== tracks ==========================
// recov, keep, nkeep, shken, shkdis, setup, error, hello, fire, ignon, dooropen, hoodopen, shock, dvron, dvroff
