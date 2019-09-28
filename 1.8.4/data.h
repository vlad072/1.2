#define DEBUG_LVL 0              // 1 = AT log, 2 = includeded dbg messages, 3 = both, 0 = nothing
//#define WD_TIMER
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
//======================= uart ==========================
#define debug           Serial
#define UART_BPS         57600
#define DEF_TIMEOUT       1000
//==================== temps indexes ====================
enum { PCB, ENG, OUTS, VEH }; // dallas order
enum { CUR, DIF };            // curent temperature, difference
//======================= consts ========================
const char* proto  = "MQIsdp";
const byte  ver    =  3;
const char* cid    = "BlackBox";
const char* willt  = "inf/online";
//===================== variables ======================
char     at[128]    =  "";
char     notify[16] =  "";
bool     setupmode  =  false;
bool     keepconn   =  true;
uint32_t tsend      =  0;
uint32_t tresp      =  0;
uint32_t t20sec     =  0;
uint32_t t1min      =  0;
uint32_t tinitlock  =  1;
byte     celstate   =  0;     // initial=0, dialing=1, on call=2, voice ready=4;
int8_t   warmtimer  = 10;
int8_t   warmtemp   = 50;
int      reconn     = -1;     // modem reconnect counter
byte     locked     =  0;     // bits: 7(h80)=lock, 0(h1)=shock sensor enabled, 1(h2)=siren active
bool     smsalert   =  false;
byte     alarm      =  0;
bool     warmup     =  false;
int8_t   temps[2][4]  = { {-127, -127, -127, -127},   // self, engine, outside, vehacle 
                          {-127, -127, -127, -127} }; // different at last scan (t20sec)

//====================== eeprom =======================
byte rombuf[0x20] = {};
byte storedpg = 0; // stored page 
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
// recov, keep, nkeep, shken, shkdis, setup, error, hello, fire, ignon, dooropen, hoodopen, shock, mute, armed, disarmed
