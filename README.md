this is firmware for a one-off solar-powered e-paper display I'm building
as a side project.  my company's office overlooks a pier that a lot of cruise
ships dock at and I wanted to build a little guy that sits in the window and
tells people facts about the ship outside (or when to expect the next one).
the device itself just connects to wi-fi and reads a message off a web
server; once the API it uses is finished I'll upload that to a separate repo
and link it here.

## stuff to buy

this is a 1/1 so I don't have gerbers or a real bill of materials or anything.
to build something like it, you'll need:

- [Adafruit's BQ25185 li-po charge controller breakout][bq25185]
- [this SSD1680-based 250x122 display][e-paper]
- a Raspberry Pi Pico W or Pico 2W (I used the 2W)
- a solar panel; I used this ["5V" 0.6W part][solar-panel]
- a 1S li-po battery; I used [this one][li-po]
- a cheap electronic components kit, for voltage dividers

I used one of [Adafruit's half-sized "Perma-Proto" breadboard PCBs][perma-proto]
to assemble the project on; any other substrate you like should work.  (I like
building things on perma-proto with 28AWG magnet wire, which makes it a lot
easier to work in tight spaces than traditional hook-up wire.)

[bq25185]: https://www.adafruit.com/product/6091
[e-paper]: https://www.adafruit.com/product/4197
[solar-panel]: https://www.adafruit.com/product/5856
[li-po]: https://www.adafruit.com/product/258
[perma-proto]: https://www.adafruit.com/product/571

## stuff to connect

connect the "load" output of the BQ25185 breakout to the VSYS and GND pins of the
Pico; the Pico bucks VSYS down to 3.3 and 1.8 volts, which will get along fine
with the BQ25185's ~3.6-4.5 volt output.  connect the power input of the BQ25185
breakout to your solar panel (some cheap 0.1" screw terminals are useful here.)

you'll need to connect the SCK, MISO, MOSI, and (E)CS pins of the e-paper breakout
to one of the Pico's hardware SPI devices.  I used GPIO 14, 12, 15, and 13
respectively, which correspond to SPI1.

in addition to however you connected the BQ25185 breakout to the main board,
connect some fly wires to the VIN and VBAT pads on the side for voltage monitoring.
you'll need to build two [resistive voltage dividers][resistive-divider] to divide
the panel voltage and battery voltage down to something (< 3.3 volts) that the
Pico's ADCs can handle. the battery will never get over 4.2 volts, so a 1 
megohm/1 megohm divider to divide it down to ~2.1 volts is fine. the panel has a
maximum power voltage of 6.11 volts but an open circuit voltage of 7.25 volts,
so out of an abundance of caution I divided it down with a 1M/680K divider to
~2.93 volts.  to reduce the amount the ADC loads the high impedance of the
divider, I also put one 1uf capacitor between each divided voltage and ground.
connect the divided voltages to GPIOs 32 (ADC1) for the panel voltage and 34 (ADC2)
for the battery voltage.

[resistive-divider]: https://en.wikipedia.org/wiki/Voltage_divider#Resistive_divider
