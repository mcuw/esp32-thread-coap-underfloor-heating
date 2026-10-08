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
```bash
esp32c6> ot factoryreset
... # the device will reboot


# After some seconds

esp32c6> ot state
leader
Done
```
Now the first device has formed a Thread network as a leader. Follow the open commissioning with the [esp32-thread-br](https://github.com/mcuw/esp32-thread-br). Get some information which will be used in next steps:
```bash
esp32c6> ot ipaddr
fdde:...:fc42
fdde:...8042
fdde:...:2742
fe80:...:c842

# Get the Active Dataset
esp32c6> ot dataset active -x
***REMOVED***0e...
```

# After some seconds

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
> ipaddr mleid
fdde:...:2742
Done

> iperf -V -c fdde:...:2742 -t 20 -i 1 -p 5001 -l 85 -f k
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

### A flash is not working

- disconnect the USB-C cable from native port and connect to the serial port, if the development board has second USB-C port.

### ESP-IDF not found

In case your ESP-IDF path has changed
```sh
rm dependencies.lock
```

```sh
idf.py set-target esp32c6
```
