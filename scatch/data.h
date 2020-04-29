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
//================= moto func actions====================
enum { READ_MOTO, WRITE_MOTO, ADD_MOTO };
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
bool     setupmode  =  0;       // initial settings mode
bool     keepconn   =  1;       // keep alive connection
bool     btansw     =  0;       // incoming calls through HSP auto-answer
byte     recmode    =  0x03;    // recording dvr 1 = drive, 2 = parking, 3 = both
uint32_t tsend      =  0;       // last packed sending point
uint32_t tresp      =  0;       // last broker responding point
uint32_t t20sec     =  0;       // 20 seconds interval
uint32_t t1min      =  0;       // 1 min interval
uint32_t tengrun    =  0;       // engine running last point
uint32_t tengstart  =  0;       // engine start pont
uint32_t tlastmsg   =  0;       // last message sending pont
uint32_t tbtring    =  0;       // last "btring" AT receive
uint32_t tlastcall  =  0;       // last calling point
uint32_t tshockdet  =  0;       // shock detection point
int8_t   nrecall    =  0;       // recall attempts
byte     callstate  =  0;       // undef=0, initial=1, alert=2, in_talk=4, channel_free=8 /// play_ready=0C
int8_t   warmtimer  =  20;      // engine warming count-down timer min 
int8_t   warmtemp   =  50;      // engine warming temperature limit deg.C 
byte     secure     =  0;       // bits: 7(80)=lock, 0(1)=shock low, 1(2)= shock high, 2(4) = siren active
bool     smsnotify  =  0;   // notifications through SMS
byte     alarm      =  0;       // alarm flags
bool     warmup     =  0;   // now engine warmup
int8_t   temps[2][4]  = { {-127, -127, -127, -127},   // self, engine, outside, vehacle 
                          {   0,    0,    0,    0} }; // different at last scan (t20sec)
//====================== eeprom =======================
#include <EEPROM.h>
byte eecash[0x40];      // eeprom map buffer
byte eepage = 0;        // stored page
invertcash() { for (byte _i = 0; _i < sizeof(eecash); _i++) eecash[_i] ^= 0xFF; }
char* broker(char* str = NULL, byte len = 0) {                            //  0..3F  ===== page 1 =====
  if (str) {
    if (len == 0) len = strlen(str);
    memcpy(eecash, str, len); eecash[len] = 0; invertcash();
    eeprom_update_block(eecash, 0x00, len+1); eepage = 0;
  } else {
    if (eepage != 1) eeprom_read_block(eecash, 0x00, sizeof(eecash)), invertcash(), eepage = 1;
    return eecash;
  }
}
char* user(char* str = NULL, byte len = 0) {                              // 40..7F  ===== page 2 =====
  if (str) {
    if (len == 0) len = strlen(str);
    memcpy(eecash, str, len); eecash[len] = 0; invertcash();
    eeprom_update_block(eecash, 0x40, len+1); eepage = 0;
  } else {
    if (eepage != 2) eeprom_read_block(eecash, 0x40, sizeof(eecash)), invertcash(), eepage = 2;
    return eecash;
  }
}
char* pass(char* str = NULL, byte len = 0) {                              // 80..BF  ===== page 3 =====
  if (str) {
    if (len == 0) len = strlen(str);
    memcpy(eecash, str, len); eecash[len] = 0; invertcash();
    eeprom_update_block(eecash, 0x80, len+1); eepage = 0;
  } else {
    if (eepage != 3) eeprom_read_block(eecash, 0x80, sizeof(eecash)), invertcash(), eepage = 3;
    return eecash;
  }
}
char* pref(char* str = NULL, byte len = 0) {                              // C0..FF  ===== page 4 =====
  if (str) {
    if (len == 0) len = strlen(str);
    memcpy(eecash, str, len); eecash[len] = 0; invertcash();
    eeprom_update_block(eecash, 0xC0, len+1); eepage = 0;
  } else {
    if (eepage != 4) eeprom_read_block(eecash, 0xC0, sizeof(eecash)), invertcash(), eepage = 4;
    return eecash;
  }
}
byte* sid(byte idx, byte* arr = NULL) {                                   // 100..13F ==== page 5 =====
  if (arr) {
    eeprom_update_block(arr, 0x100 + idx*8, 8); eepage = 0;
  } else {
    if (eepage != 5) eeprom_read_block(eecash, 0x100, sizeof(eecash)), eepage = 5;
    return eecash + idx*8;
  }
}
uint16_t moto(byte act = READ_MOTO, uint16_t val = 1) {                   // 3C0..3FF ==== page 16 ====
  uint16_t _offs = ~eeprom_read_word(0x3FE); uint16_t _raw = ~eeprom_read_word(0x3C0+_offs);
  switch (act) {
    case ADD_MOTO  : _raw += val, eeprom_update_word(0x3C0+_offs, ~_raw);
                     break;
    case WRITE_MOTO: _offs = (_offs < 0x3C) ? _offs+2 : 0;
                     eeprom_update_word(0x3FE, ~_offs);
                     eeprom_update_word(0x3C0+_offs, ~val);
                     break;
    default        : return _raw;
  }
}

//=================== tracks ==========================
// recov, keep, nkeep, shken, shkdis, setup, error, hello, fire, ignon, dooropen, hoodopen, shock, dvron, dvroff, reboot, normal
// UNUSED:  jamming, bell
