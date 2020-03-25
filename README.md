# BlackBox Project
This software is distributed under the GNU GPL license https://www.gnu.org/licenses/gpl.txt

©2019 Pervuninsky Vlad http://drive2.ru/users/vlad0722/

mail to: pervuninsky.vlad@gmail.com

### Usage 
DTMF key:
|    Key     |          Descript                 |
|------------ | -------------|
 |2 | keep connection enable/disable. reconnect when connection loss
 |3 | shock sensor enable/disable
 |5 | dvr power turn on/off
 |# | restore modem defaults then reboot (BLUETOOTH PIN WILL BE RESETING TO DEFAULT!!!)
 |* | setup mode

Initial settings through BT SPP:
| Command | Descript |
|------------ | -------------|
| srv=url,port |   mqtt broker addr & port |
| usr=username |   broker login |
| pwd=password |   broker password |
| sens=0 |         teach the internal pcb temperature sensor (must be defined first! all ext sensors disconnected!) |
| sens=1 |         teach engine temperature sensor (his one connected only!) |
| sens=2 |         teach outside air temperature sensor (his one connected only!) |
| sens=3 |         teach vehicle temperature sensor (his one connected only!) |
| sens=? |         read sensors info |
| btpin=xxxx |     update bt pin & detach all paired devs |
| ? |              help |

### Tech info

##### MQTT topics:

|Root topics|Descript|
|------------ | -------------|
| cmd/#  |  commands from user to car (below remark 'c') |
| inf/#  |  feedback from car to user app (below 'i') |
| notify |  push notification to user |
| log    |  evnt log |

|Sub topics|Descript|C|I|
|---|---|---|---|
|btpwr   | on/off bt power                      | x | x |
|btcon   | number conn/paired devs via bt hsp   |   | x |
|sq      | baseband signal level                |   | x |
|warmup  | remote warmup engine                 |   | x |
|wutm    | warmup timer in min's                | x | x |
|wutp    | warmup temperature limit             | x | x |
|engtp   | current engine temper                |   | x |
|pcbtp   | current pcb temper                   |   | x |
|outtp   | outside temper                       |   | x |
|balance | sim-card balance                     |   | x |
|place   | lbs location <nn.nnnn,ee.eeee>       |   | x |
|lock    | lock/ulock doors                     | x | x |
|siren   | siren act/silent mode                | x | x |
|shock   | chock sensor en/dis                  | x | x |
|sms     | sms/push notify                      | x | x |
|alarm   | alarms byte/reset                    | x | x |
|vbatt   | battery voltage                      | x | x |
|engr    | engine running                       |   | x |
|drop    | door opened                          |   | x |
|hdop    | hood opened                          |   | x |
|start   | remote start/stop engine             | x |   |
upd'     | update info panel                    | x |   |
online'  | blackbox connected                   |   | x |
dvr'     | dvr on/off                           | x | x |
gear'    | gear state N/D                       |   | x |

##### Voice *.amr files into modem:

* 'recov'    - modem recovery
* 'keep'     - keep connect
* 'nkeep'    - don't keep
* 'shken'    - shock sensor enabled
* 'shkdis'   - ...disabled
* 'dvron'    - dvr power on
* 'dvroff'   - ..off
* 'setup'    - initial setup mode
* 'error'    - error
* 'hello'    - hello :)
* 'fire'     - fire detected
* 'ignon'    - ignition turn-on
* 'dooropen' - door openend
* 'hoodopen' - hood or trunk opened
* 'shock'    - 2nd lvl bang alarm
