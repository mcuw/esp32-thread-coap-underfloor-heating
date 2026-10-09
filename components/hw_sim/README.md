# Hardware simulation

## Was simuliert wird

- Mechanik: Jeder der 16 Kanäle hat eine simulierte Position, die beim Start auf 50 % steht, also unbekannt. Die Referenzfahrt beim Start läuft deshalb wirklich und stoppt am simulierten Anschlag.
- Strom: Der simulierte INA219 liefert Anlaufstoß, Fahrstrom und Anschlagstrom mit Rauschen. Deine echte Anschlagserkennung in motor_drv.c läuft damit durch, und du siehst im Log, ob die Schwellen sinnvoll sind.
- Fahrzeit: Sie beträgt 6 s für den Vollhub und ist in menuconfig einstellbar. valve_control übernimmt diesen Wert im Mock-Modus automatisch für alle Zonen, damit die Positionen zusammenpassen.
- Nicht simuliert: valve_control und die CoAP-Schicht laufen unverändert. Im Mock-Modus werden weder GPIOs noch I²C angefasst.

## LED-Anzeige
| LED | Bedeutung |
|---|---|
| rot | Ventil öffnet |
| blau | Ventil schließt |
| kurz weiß | Anschlag erreicht |
| aus | Stillstand |

## Thread state indicator

- disable `CONFIG_OPENTHREAD_STATE_INDICATOR_ENABLE` in the menuconfig otherwise the Thread-status indicator uses the LED.