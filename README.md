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
 |# | (long press) restore modem defaults then reboot (BLUETOOTH PIN WILL BE RESETING TO DEFAULT!!!)
 |* | (long press) setup mode
 |0 | (long press) reset

Initial settings through BT SPP:
| Command | Descript |
|------------ | -------------|
| moto=x       |   set engine working hours |
| srv=url,port |   mqtt broker addr & port |
| srv=?        |   print connection settings |
| usr=username |   broker login |
| pwd=password |   broker password |
| pref=prefix  |   prefix topics |
| sens=0       |   teach the internal pcb temperature sensor (must be defined first! all ext sensors disconnected!) |
| sens=1       |   teach engine temperature sensor (his one connected only!) |
| sens=2       |   teach outside air temperature sensor (his one connected only!) |
| sens=3       |   teach vehicle temperature sensor (his one connected only!) |
| sens=?       |   read sensors info |
| btpin=xxxx   |   change bt pin & detach all paired devs |
| ?            |   help |
| exit         |   exit setup mode |

### Tech info

##### MQTT topics:

|Root topics|Descript|
|------------ | -------------|
| cmd/#  |  commands from user to car (below remark 'C') |
| inf/#  |  feedback from car to user app (below 'I') |

|Sub topics|Descript|C|I|
|---|---|---|---|
|btpwr   | on/off bt power                                                           | x |   |
|btst    | bt status bits: WCCCPPPP (W=power, C=connected, P=paired)                 |   | x |
|sq      | baseband signal level                                                     |   | x |
|wutm    | warmup timer in min's                                                     | x | x |
|wutp    | warmup temperature limit                                                  | x | x |
|engtp   | engine temperature                                                        |   | x |
|vehtp   | vehacle temperature                                                       |   | x |
|pcbtp   | pcb (internal) temperature                                                |   | x |
|outtp   | outside temperature                                                       |   | x |
|balance | sim-card balance                                                          |   | x |
|place   | lbs location <nn.nnnn,ee.eeee>                                            |   | x |
|lock    | lock/ulock doors                                                          | x | x |
|siren   | siren act/silent mode                                                     | x |   |
|shock   | chock sensor en/dis                                                       | x |   |
|secure  | secure bits: LxxxxSAW (L=locked, S=siren active, A=shock hi, W=shock low) | x |   |
|sms     | sms/push notify                                                           | x | x |
|alarm   | alarms, reset                                                             | x | x |
|vbatt   | battery voltage                                                           |   | x |
|engr    | engine running                                                            | x | x |
|doors   | door opened bits: xxxxxxDH (D=doors, H=hood,trunk)                        |   | x |
|start   | remote start/stop engine                                                  | x |   |
|upd     | update info                                                               | x |   |
|online  | blackbox connected                                                        |   | x |
|dvr     | dvr on/off                                                                | x | x |
|moto    | engine working hours counter                                              |   | x |
|notify  | push notifications                                                        |   | x |
|fwv     | firmware version                                                          |   | x |

##### Voice *.amr files into modem:

| File name | Descript |
|---|---|
|recov    | modem recovery |
|keep     | keep connect |
|nkeep    | don't keep |
|shken    | shock sensor enabled |
|shkdis   | ...disabled |
|dvron    | dvr power on |
|dvroff   | ..off |
|setup    | initial setup mode |
|error    | error |
|hello    | hello :) |
|fire     | fire detected |
|ignon    | ignition turn-on |
|dooropen | door openend |
|hoodopen | hood or trunk opened |
|shock    | 2nd lvl bang alarm |
|reboot   | reset |
|normal   | return from setup mode |
