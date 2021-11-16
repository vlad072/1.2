
//======================== pins ==========================
#define IGN_PIN              4
#define STARTER_PIN          5
#define SIREN_PIN            6
#define DVR_PIN              7
//#define LOCK_PIN             8 lin rx
//#define UNLOCK_PIN           9 lin tx
#define FLASH_PIN           10
//#define MODEM_TX_PIN        11  now free
//#define MODEM_RX_PIN        12  now free
#define LED_PIN             13
#define HOOD_PIN            A0
#define DOOR_PIN            A1
#define PUMP_PIN            A2
#define NEUTRAL_PIN         A3
#define STARTER_SUPPLY_PIN  A4
#define ONEWIRE_PIN         A5
//#define SHOCK1_PIN           3
//#define SHOCK2_PIN           2
#define BATT_PIN            A7
//#define CURRENT_PIN         A6
//===================== statements ======================
#define PUSH        0x01
#define SMS         0x02
#define RETAIN      0x01
#define CLOSE_SIGN  0x01
#define OPEN_SIGN   0x02
//================= moto func actions====================
enum { READ_MOTO, WRITE_MOTO, ADD_MOTO };
//======================= uart ==========================
#define modem             Serial
#define MODEM_BPS         57600
#define LIN_BPS           9600
#define DEF_TIMEOUT       1000
#define PAGESZ            0x40
//==================== temps indexes ====================
enum { PCB, ENG, OUTS, VEH }; // dallas order
enum { CUR, DIF };            // curent, difference
//======================= consts ========================
const char* cid     = "BB7";            // device signature
const char* fwver   = "7.4";            // firmware version
//===================== variables ======================
char     at[128]    =  "";      // AT buffer
char     msg[32]    =  "";      // message buffer
bool     setupmode  =  0;       // initial settings mode
bool     keepconn   =  1;       // keep alive connection
bool     btansw     =  0;       // incoming calls through HSP auto-answer
uint8_t  dvrmode    =  0x03;    // recording dvr 1 = drive, 2 = parking, 3 = both
uint32_t tsend      =  0;       // last packed sending point
uint32_t tresp      =  0;       // last broker responding point
uint32_t t20sec     =  0;       // 20 seconds interval
uint32_t t1min      =  0;       // 1 min interval
uint32_t tengrun    =  0;       // engine running last point
uint32_t tengstart  =  0;       // engine start pont
uint32_t tbtring    =  0;       // last "btring" AT receive
uint32_t tlastcall  =  0;       // last calling point
uint32_t tshockdet  =  0;       // shock detection point
int8_t   nrecall    =  0;       // recall attempts
uint8_t  callstate  =  0;       // undef=0, initial=1, alert=2, in_talk=4, speak=8 /// play_ready=0x04
int8_t   warmtimer  =  20;      // engine warming count-down timer min 
int8_t   warmtemp   =  50;      // engine warming temperature limit deg.C 
uint8_t  secure     =  0;       // bits: 7(80)=lock, 6(40) = siren active, 5(20) = fire detect, 1(2)= shock high, 0(1)=shock low
uint8_t  alarm      =  0;       // alarm flags
uint8_t  alarmcash  =  0;       // alarm cash
bool     warmup     =  0;       // now engine warmup
uint8_t  shuts      =  0;       // try connection counter
float    lowbatt    =  12.0f;   // low battery warning lvl
float    batt       =  0.0f;
int8_t   temps[2][4]  = { {-127, -127, -127, -127},   // self, engine, outside, vehacle 
                          {   0,    0,    0,    0} }; // different at last scan (t20sec)
//====================== eeprom =======================
#include <EEPROM.h>                                                       ! MUST BE INIT FILL 0x00 !
uint8_t eebuf[PAGESZ] = {0}; // eeprom map buffer
uint8_t eepage = 0;          // stored page
char* broker(char* str = NULL, uint8_t len = 0) {                            //  0..3F  ===== page 1 =====
  if (str) {
    eeprom_update_block(str, 0x00, len); eeprom_update_byte(len, 0); eepage = 0;
  } else {
    if (eepage != 1) eeprom_read_block(eebuf, 0x00, PAGESZ), eepage = 1;
    return eebuf;
  }
}
char* user(char* str = NULL, uint8_t len = 0) {                              // 40..7F  ===== page 2 =====
  if (str) {
    eeprom_update_block(str, 0x40, len); eeprom_update_byte(0x40+len, 0); eepage = 0;
  } else {
    if (eepage != 2) eeprom_read_block(eebuf, 0x40, PAGESZ), eepage = 2;
    return eebuf;
  }
}
char* pass(char* str = NULL, uint8_t len = 0) {                              // 80..BF  ===== page 3 =====
  if (str) {
    eeprom_update_block(str, 0x80, len); eeprom_update_byte(0x80+len, 0); eepage = 0;
  } else {
    if (eepage != 3) eeprom_read_block(eebuf, 0x80, PAGESZ), eepage = 3;
    return eebuf;
  }
}
char* pref(char* str = NULL, uint8_t len = 0) {                              // C0..FF  ===== page 4 =====
  if (str) {
    eeprom_update_block(str, 0xC0, len); eeprom_update_byte(0xC0+len, 0); eepage = 0;
  } else {
    if (eepage != 4) eeprom_read_block(eebuf, 0xC0, PAGESZ), eepage = 4;
    return eebuf;
  }
}
uint8_t* sid(uint8_t idx, uint8_t* id = NULL) {                              // 100..13F  ===== page 5 =====
  if (id) {
    eeprom_update_block(id, 0x100 + idx*8, 8); eepage = 0;
  } else {
    if (eepage != 5) eeprom_read_block(eebuf, 0x100, PAGESZ), eepage = 5;
    return eebuf + idx*8;
  }
}
uint8_t key(uint8_t k = 0) {                                                 // 140      ======= key =======
  if (k) eeprom_update_byte(0x140, k);
  return eeprom_read_byte(0x140);
}
uint8_t rejectkey(int8_t k = -1) {                                           // 140      === key reject ====
  if (k != -1) eeprom_update_byte(0x141, k);
  return eeprom_read_byte(0x141);
}

uint16_t moto(uint8_t act = READ_MOTO, uint16_t val = 1) {                   // 3C0..3FF
  uint8_t _offs = eeprom_read_byte(0x3FF); uint16_t _raw = eeprom_read_word(0x3C0+_offs);
  switch (act) {
    case ADD_MOTO  : _raw += val; eeprom_update_word(0x3C0+_offs, _raw);
                     return eeprom_read_word(0x3C0+_offs);
    case WRITE_MOTO: _offs = (_offs < 0x3C) ? _offs+2 : 0;
                     eeprom_write_byte(0x3FF, _offs);
                     eeprom_update_word(0x3C0+_offs, val);
                     return eeprom_read_word(0x3C0+_offs);
    default        : return _raw;
  }
}
//=================== tracks ==========================
// recov, keep(keep1), nkeep(keep0), shken(shk1), shkdis(shk0), setup(setup1), error, hello, fire, ignon, dooropen, hoodopen, shock2, dvron(dvr1), dvroff(dvr0), reboot, normal(setup0), bt1, bt0, eng1, eng0, help, noalm, almclr, shock1
// UNUSED:  jamming, bell
