#define DEBUG_LVL 0              // 1 = AT log, 2 = includeded dbg messages, 3 = both, 0 = nothing
//======================== pins ==========================
#define IGN_PIN              4
#define STARTER_PIN          5
#define SIREN_PIN            6
#define DVR_PIN              7
#define LOCK_PIN             8
#define UNLOCK_PIN           9
#define FLASH_PIN           10
#define SIM800RX_PIN        11
#define SIM800TX_PIN        12
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
//====================== msg type =======================
#define PUSH        0x01
#define SMS         0x02
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
char     at[128]    =  "";        // AT buffer
char     msg[16]    =  "";        // messages buffer
bool     setupmode  =  false;     // initial settings mode
bool     keepconn   =  true;      // keep alive connection
bool     btansw     =  false;     // incoming calls through HSP auto-answer 
uint32_t tsend      =  0;         // last packed sending point
uint32_t tresp      =  0;         // last broker responding point
uint32_t t20sec     =  0;         // 20 seconds interval
uint32_t t1min      =  0;         // 1 min interval
uint32_t tengrun    =  0;         // engine running last point
uint32_t tlastmsg   =  0;         // last message sending pont
uint32_t tbtring    =  0;         // last "btring" AT receive
uint32_t tlastcall  =  0;         // last calling point
int8_t   nrecall    =  0;         // recall attempts
uint8_t  celstate   =  0;         // initial=0, dialing=1, on call=2, voice ready=4;
int8_t   warmtimer  =  20;        // engine warming count-down timer min 
int8_t   warmtemp   =  50;        // engine warming temperature limit deg.C 
uint8_t  secure     =  0;         // bits: 7(h80)=lock, 0(h1)=shock sensor enabled, 1(h2)=siren active
bool     smsnotify  =  false;     // notifications through SMS
uint8_t  alarm      =  0;         // alarm flags
bool     warmup     =  false;     // now engine warmup
int8_t   temps[2][4]  = { {-127, -127, -127, -127},   // self, engine, outside, vehacle 
                          {   0,    0,    0,    0} }; // different at last scan (t20sec)
//====================== eeprom =======================
byte rombuf[0x20] = {}; // eeprom map buffer
byte storedpg = 0;      // stored page 
char* broker() {                        //  0..1F  ===== page 1 =====
  if (storedpg != 1) EEPROM.get(0x00, rombuf), storedpg = 1;
  return rombuf;
}
char* user() {                          // 20..2F  ===== page 2 =====
  if (storedpg != 2) EEPROM.get(0x20, rombuf), storedpg = 2;
  return rombuf;
}
char* pass() {                          // 30..3F
  return user() + 0x10; 
}
byte* sid(byte idx) {                   // 40..5F  ===== page 3 =====
  if (storedpg != 3) EEPROM.get(0x40, rombuf), storedpg = 3;
  return rombuf + idx*8;
}
//=================== tracks ==========================
// recov, keep, nkeep, shken, shkdis, setup, error, hello, fire, ignon, dooropen, hoodopen, shock, dvron, dvroff
