| Supported Targets | ESP32-C6 |
| ----------------- | -------- |

# Thread CoAP - underfloor heating

## Features

- Compatible with the Thread Border Router: [ESP32-thread-br](https://github.com/mcuw/esp32-thread-br)

- CoAP-Server is running (/heat-Ressource)

- WS2812 is supported

## How to use example

### Hardware Required

To run this example, a board with IEEE 802.15.4 module (for example ESP32-C6) is required.

### Configure the project


```sh
idf.py set-target esp32c6
```

```sh
idf.py menuconfig
```

The example can run with the default configuration. OpenThread Command Line is enabled with UART as the default interface. Additionally, USB JTAG is also supported and can be activated through the menuconfig:

```
Component config → ESP System Settings → Channel for console output → USB Serial/JTAG Controller
```

### Build, Flash, and Run

Build the project and flash it to the board, then run monitor tool to view serial output:

```sh
idf.py -p PORT build flash monitor
```
Change the PORT to your device or leave the "-p PORT" away to use auto-connect.

Now you'll get an OpenThread command line shell.

### Example Output

The `help` command will print all of the supported commands.
```bash
esp32c6> ot help
I(7058) OPENTHREAD:[INFO]-CLI-----: execute command: help
bbr
bufferinfo
ccathreshold
channel
child
childip
childmax
childsupervision
childtimeout
coap
contextreusedelay
counters
dataset
delaytimermin
diag
discover
dns
domainname
eidcache
eui64
extaddr
extpanid
factoryreset
...
```

## Set Up Network

To run this example, at least two ESP32-C6 boards flashed with this ot_cli example are required.

On the first device, run the following commands:
```sh
esp32c6> ot factoryreset
... # the device will reboot
```

After some seconds

```sh
esp32c6> ot state
leader
Done
```
Now the first device has formed a Thread network as a leader. Follow the open commissioning with the [esp32-thread-br](https://github.com/mcuw/esp32-thread-br). Get some information which will be used in next steps:
```sh
esp32c6> ot ipaddr
fdde:...:fc42
fdde:...:8042
fdde:...:2742
fe80:..:c842

# Get the Active Dataset
esp32c6> ot dataset active -x
0e...
```

After some seconds

```sh
esp32c6> ot state
router  # child is also a valid state
Done
```
This device has joined the Thread network as a router (or a child).

## Extension commands

You can refer to the [extension command](https://github.com/espressif/esp-thread-br/blob/main/components/esp_ot_cli_extension/README.md) about the extension commands.

The following examples are supported by `ot_cli`:

* TCP and UDP Example

## Using iPerf to measure bandwidth

iPerf is a tool used to obtain TCP or UDP throughput on the Thread network. To run iPerf, you need to have two Thread devices on the same network.

Refer to [the iperf-cmd component](https://components.espressif.com/components/espressif/iperf-cmd) for details on specific configurations.

### Typical usage on a thread network

For measuring the TCP throughput, first create an iperf service on one node:
```bash
> iperf -V -s -t 20 -i 3 -p 5001 -f k
Done
```

Then create an iperf client connecting to the service on another node. Note that the [ML-EID](https://openthread.io/guides/thread-primer/ipv6-addressing#unicast_address_types) address is used for iperf.

```bash
> ot ipaddr mleid
fdde:...:271b
Done

> iperf -V -c fdde:...:271b -t 20 -i 1 -p 5001 -l 85 -f k
Done
[ ID] Interval		Transfer	Bandwidth
[  1]  0.0- 1.0 sec	3.15 KBytes	25.16 Kbits/sec
[  1]  1.0- 2.0 sec	2.89 KBytes	23.12 Kbits/sec
[  1]  2.0- 3.0 sec	2.98 KBytes	23.80 Kbits/sec
...
[  1]  9.0-10.0 sec	2.55 KBytes	20.40 Kbits/sec
[  1]  0.0-10.0 sec	27.80 KBytes	22.24 Kbits/sec
```

For measuring the UDP throughput, first create an iperf service similarly:

```bash
> iperf -V -u -s -t 20 -i 3 -p 5001 -f k
Done
```

Then create an iperf client:

```bash
> iperf -V -u -c fdde:...:2742 -t 20 -i 1 -p 5001 -l 85 -f k
Done
```

```sh
aiocoap-client "coap://[Adresse]/light"
```

```sh
aiocoap-client -m PUT "coap://[Adresse]/light" \
  --payload '{"on":true,"r":255,"g":0,"b":0}'
```

---

Why there are multiple address

Each Thread-device get Automatic Ally several IPv6-Adress for different usecases.

```
Adress	Type	Property
fd36:122:1738:1:...	OMR (Off-Mesh-Routable)	From your BR-prefix, for access outside the mesh
fd51:...:0:ff:fe00:e801	RLOC (Routing Locator)	coded current RLOC16 direct to an address – changed after a role switch (e.g. Child → Router)
fd51:...:63fc:...	ML-EID (Mesh-Local EID)	stable, random identity inside the Mesh-prefixes – does not change after a role switch
fe80:...	Link-Local	Only useable for direct wireless connection, is not routeable over multiple ranges
```

---

## Troubleshooting

### No serial port output

- use the reset button after a firmware flash

### Failed to flash

- disconnect the USB-C cable from native port and connect to the serial port, if the development board has second USB-C port.

### ESP-IDF not found

In case your ESP-IDF path has changed
```sh
rm dependencies.lock
```

```sh
idf.py set-target esp32c6
```

## Test CoAP with simulated hardware

Test the CoAP service on ESP32-C6 w/o a connected external device (INA219, Motor, ...)

1. activate simulation configuration
```sh
idf.py menuconfig
```
select "Underfloor Heating/ Simulate hardware devices"

2. build firmware
```sh
idf.py build flash monitor
```

3. get dataset from "underfloor heating" ot cli

```sh
ot dataset active -x
```

4. flash and monitor another ESP32-c6 with the ESP-IDF ot_cli example

5. use hex from step 2
```sh
ot dataset set active <hex-string>
ot ifconfig up
ot thread start
```

6. wait 10-20s for an output then check role state

```sh
ot state
```

7. when the role changed to `child`, `router` or `leader` then continue

```sh
ot coap start
ot coap get <IP> heating
```

8. decode HEX response
```sh
echo <hex-string> | xxd -r -p | jq
```

outputs
```json
{
  "zones": [
    {
      "zone": 1,
      "t": null,
      "sp": 21,
      "valid": false,
      "open": false,
      "pos": 0
    },
    {
      "zone": 2,
      "t": null,
      "sp": 21,
      "valid": false,
      "open": false,
      "pos": 0
    },
    {
      "zone": 3,
      "t": null,
      "sp": 21,
      "valid": false,
      "open": false,
      "pos": 0
    },
    {
      "zone": 4,
      "t": null,
      "sp": 21,
      "valid": false,
      "open": false,
      "pos": 0
    }
  ]
}
```
This is the start state:
- multiple zones exists
- `t`: null
- `valid`: false, weil noch kein Sensor gemeldet hat.
- `sp`: 21 ist der Standardsollwert.
- `open`: false
- `pos`: 0, also ist die Referenzfahrt durch und alle Ventile sind zu (Failsafe)

### Test send temperature change message

#### call on ot_cli node: set cold to open valve

```sh
ot coap put <IP> heating/temp con {"zone":1,"t":18.0}
```

output on underfloor heating node

```sh
I (1734269) valve_control: Zone 1 (Kanal 0): Ventil -> AUF (T=18.0, Soll=21.0)
I (1734269) coap_underfloor_heating: Temp zone=1 t=18.00 -> ok
I (1734499) valve_control: Zone 1: fahre 0% -> 100% (max 9000 ms)
I (1734549) hw_sim: Kanal 0: OEFFNEN ab 0%
I (1734549) ws2812: LED On (R=80 G=0 B=0)
I (1740629) hw_sim: Kanal 0: Stopp bei 100% (Anschlag)
I (1740629) ws2812: LED On (R=80 G=80 B=80)
I (1740779) ws2812: LED Off (R=80 G=80 B=80)
I (1740779) valve_control: Zone 1: fertig nach 6079 ms, Anschlag=ja, Fahrstrom=119 mA, Spitze=256 mA -> pos=100%
```

call on ot_cli node to get the `heating` state

```sh
ot coap get <IP> heating
```
outputs ot_cli node

```json
{
  "zones": [
    {
      "zone": 1,
      "t": 18,
      "sp": 21,
      "valid": true,
      "open": true,
      "pos": 100
    },
    ...
```
  
#### set too warm there for close valve

call on ot_cli node

```sh
ot coap put <IP> heating/temp con {"zone":1,"t":23.0}
```

get the `heating` state

```sh
ot coap get <IP> heating
```

outputs
```json
{
  "zones": [
    {
      "zone": 1,
      "t": 23,
      "sp": 21,
      "valid": true,
      "open": false,
      "pos": 0
    },
```

other test

#### Hysterese: 21.0 liegt zwischen 20.7 und 21.3, es darf nichts passieren
```sh
ot coap put <IP> heating/temp con {"zone":1,"t":21.0}
```

#### Sollwert ändern: bei t=23 und sp=25 muss das Ventil wieder öffnen

```sh
ot coap put <IP> heating/setpoint con {"zone":1,"sp":25.0}
```

#### negative paths

unknown zone
```sh
ot coap put <IP> heating/temp con {"zone":9,"t":20.0}
```

invalid setpoint value
```sh
ot coap put <IP> heating/setpoint con {"zone":1,"sp":50.0}
```

#### Test sensor timeout -> go to failsafe position

- comment out default value to 30s
```c
// #define SENSOR_TIMEOUT_S        (15 * 60)  // keine Messung -> Failsafe
#define SENSOR_TIMEOUT_S        (30)  // keine Messung -> Failsafe
```
- send value 18°C
- after 40s because check is every 10s
- expect Sensor-Timeout output and move to safe position
- if you send a value again then zone should be valid again and continue to work regular
- if you want to fail safe to bo open because of winter cold protection then set `FAILSAFE_VALVE_OPEN` to `1`