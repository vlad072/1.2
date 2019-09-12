**********************************************************
*               BlackBox Progect v1.2.                   *
* This software is distributed under the GNU GPL license *
*         https://www.gnu.org/licenses/gpl.txt           *
*               ©2019 Pervuninsky Vlad,                  *
*         mail to: pervuninsky.vlad@gmail.com            *
**********************************************************

================ usage =================
      ---------- dtmf key ----------
'2' - keep connection enable/disable. reconnect in case of connection loss
'3' - shock sensor enable/disable
'6' - shut up the siren
'9' - arm/disarm
'#' - restore modem defaults then reboot
'0' - setup mode for settings
  --- initial settings through bt spp ---
srv=<url>,<port> - mqtt broker addr
usr=<username>   - login for authorization
pwd=<password>   - password
sens=0           - teach the internal pcb temperature sensor
sens=1           - ...engine
sens=2           - ...outside air
sens=3           - ...vehacle
end              - finish the settings & reboot
=============== tech info ==============
  --- topics for information transfer ---
  root topics:
'cmd/#'    - commands from user to car (below 'c')
'inf/#'    - feedback from car to user app (below 'i')
'notify'   - push notification to app
  low-level topics:
'btpow'    - on/off bt power                        (c/i)
'btpair'   - munber paired devices                  (i)
'btcon'    - number connected dev via bt hsp        (i)
'sq'       - baseband signal level                  (i)
'warmup'   - remote warmup engine                   (i)
'wutm'     - warmup timer in min's                  (c/i)
'wutp'     - warmup temperature limit               (c/i)
'trem'     - calculated time to engine temperature  (i)
'engtp'    - current engine temper                  (i)
'pcbtp'    - current pcb temper                     (i)
'outtp'    - outside temper                         (i)
'balance'  - sim-card balance                       (i)
'place'    - lbs location <nn.nnnn,ee.eeee>         (i)
'lock'     - lock/ulock doors                       (c/i)
'siren'    - siren act/silent mode                  (c/i)
'shock'    - chock sensor en/dis                    (c/i)
'sms'      - push/sms notify                        (c/i)
'keepc'    - keep connect to broker                 (c/i)
'recon'    - broker reconnect count (debug)         (c/i)
'alarm'    - alarm triggering/reset                 (c/i)
'vbatt'    - battery voltage                        (i)
'engr'     - engine running                         (i)
'drop'     - door opened                            (i)
'hdop'     - hood opened                            (i)
'start'    - start/stop engine                      (c)
'btatt'    - attach new bt dev                      (c)
'btpin'    - change bt pin (4 digit)                (c)
'upd'      - update app info (one tab)              (c)
'online'   - linked to brocker flag                 (i)
------ voice *.amr files into modem ------
'recov'    - modem recovery
'keep'     - keep connect
'nkeep'    - don't keep
'shken'    - shock sensor enabled
'shkdis'   - ...disabled
'setup'    - initial setup mode
'error'
'hello'
'fire'     - fire alarm!!
'ignon'    - ignition on trigged alarm
'dooropen'
'hoodopen'
'shock'    - bang alarm 2st
'mute'     - shutup siren
'armed'
'disarmed'
