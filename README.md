# SAINTSays Mini-Badge


<span style="font-size:0.8em;font-style:italic;">This badge is not made in affiliation with or endorced by the SAINTCON conference or the Utah SAINT organization</span>


This was my first time designing and producing a minibadge for SAINTCON (2024), so it may be a bit rough around the edges.

This repo contains the Code and KiCad file for the SAINTSays Mini-Badge.



## Programming the Badge

Compiling the code in the Arduino IDE for an ATTINY requires a package that supports the `Wire.h` library.

I found that the [ATTINYCore](https://github.com/SpenceKonde/ATTinyCore/blob/v2.0.0-devThis-is-the-head-submit-PRs-against-this/Installation.md) library works well for this. 
If you don't plan on using the I2C features, The [attiny](https://github.com/damellis/attiny) package can also be used for programming the badge. 

The badge has the AREF pins connected to the Mini-Badge headers using the [suggested connection method](https://github.com/lukejenkins/minibadge#prog): 

| Header| Pin |
|-------|-------|
| MISO  | Pin 3 |
| CLK   | Pin 4 |
| MOSI  | Pin 5 |
| RESET | Pin 6 |

The power pin is wired to the 3.3v supply on Pin 7 and ground on Pin 8.

The Clock and SDA/SCL pins are also connected to the ATTiny. 
Be sure these Headers are not connected to external things when programming the badge. 

## Disabling I2C
If you don't plan on using the I2C features of the badbge, the code can be removed by commenting out the `#define USEI2C true` line near the top of the .ino file. 
This has the added benefit of making the compiled code a fair bit smaller too.

## Documentation

[How to solder the badge](https://zanedurk.in/soldering-the-saintsays-mini-badge/)

[How it's made](https://zanedurk.in/saintsays-mini-badge/)

