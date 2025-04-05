# LCC Turnout Controller Board

The LCC Turnout Controller Board is a basic OpenLCB (LCC) node that grants full control
to up to 8 servo-based turnouts, with automatic frog polarity switching. This is a
specialized design meant for my application (n scale servo based turnouts) where I
didn't have enough room for a mechanical switch to change frog polarity, but still need the unitnerrupted power delivery to the locomotive afforded by powered frogs.

![PCB Photo](Doc/pcb_photo.jpg)

## Powering the LCC Turnout Controller Board

The LCC Turnout Controller Board can be powered from an external power supply (12V) on
J1 connector, or using the OpenLCB (LCC) Bus power via the RJ45 connector. To enable powering
from the external supply, bridge pins 1 and 2 on JP1. To power from the OpenLCB
(LCC) Bus, bridge pins 2 and 3. Note that when using the USB connector on the
ESP32 module JP1 should not have any pins bridged.

### OpenLCB (LCC) Power requirements

The LCC Turnout Controller Board will draw around 100mA from the OpenLCB (LCC) Bus when
the PWR_POS pin provides 15VDC. If the PWR_POS pin provides less, the node may
draw more current from the bus.

### External Power requirements

The external power supply should be rated for at least 12VDC 500mA to ensure
sufficient current is available for the node to operate as intended.

### Node Brownout detection

If the ESP32 detects a brownout condition it will attempt to produce the
well-known event `01.00.00.00.00.00.FF.F1` soon after startup. This delivery
is not guaranteed.

## LCC Turnout Controller PCB

The LCC Turnout Controller Board PCB can be found under the PCB directory and is
provided as both KiCad files and generated Gerber files.

![PCB Render](Doc/pcb.jpg)

### PCBWay project

The LCC Turnout Controller PCB has been shared via PCBWay 

TODO: Add PCBWay Project link

### Mouser BOM

The ESP32 OpenLCB IO Board components have been entered on Mouser for easy ordering
via the following shared carts:

TODO: Add shared carts

Ordering in larger quantities can decrease the per-PCB cost considerably. In
the case of resistors and capacitors it is recommended to order 100 of each
as they are tiny and easily lost. Other parts can often be substituted as long
as they have the same footprint and pinout. The pin headers will need to be cut
to length in both of the carts above, in the case of the five PCBs option there
should be enough to populate all five PCBs for the ESP32 *AND* the extension
board (when available).

## LCC Turnout Controller PCB Mount

You can find the fusion 360 archive and step files for a 3d-printable stand-off / spacer type mount in the Mount directory. This will allow for easy mounting of the Turnout Controller using self-tapping screws to the underside of any layout or a dedicated plywood electronics board. The Fusion360 files can also be modified for your application, or used to create printable paper templates to transfer the mounting hole spacing of the board.

![PCB Mount Render](Doc/mount_render.png)

## JMRI Configuration

The Turnout Controller must be plugged into an active LCC bus in order to be configured. I have successfully tested the boards with a TCS CS-105 base station, connected to a Raspberry Pi 5. The Pi had JMRI installed and was connected to the CS-105 via a shared WiFi network.

Upon plugging in the Turnout Controller, I was able to see the entry for that device's address pop up:
![JMRI Discovery](Doc/jmri_discovery.png)

Clicking on the Board in the OpenLCB Network list expanded the entry to show a "Open Configuration Dialog" option. Clicking that opened a separate window where I was able to adjust the name and description of the Turnout Controller, along with the different servo and frog attributes of the 8 turnouts controlled by the board:
![JMRI Configuration](Doc/jmri_config.png)

## Acknowledgements

This project borrows heavily from Mike Dunston's ESP32 OpenLCB IO Board for the PCB design:
https://github.com/atanisoft/Esp32OlcbIO

A lot of the code is based on Robert Heller's ESP32 LCC examples:
https://github.com/RobertPHeller/ESP32-LCC
