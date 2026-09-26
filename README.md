# STM32 Guitar Pedal

This is a custom STM32 digital guitar pedal with firmware programmed in c++ using STM32CubeIDE. It uses a STM32H7B0 Arm Cortex-M7 microcontroller 
running at 280MHz with 1.4MB SRAM and 128kb of flash. The PCB is designed using KiCad, and the JLCpcb Gerbers are included for easy ordering. This
pedal is capable of reverbs, delays, granular, filtering, amp and cabsims, and anything else you can think up.

![app](https://github.com/GuitarML/GuitarPedalSTM32/blob/main/images/pedal_pic.jpg)

Hardware Features:
 - Mono input/output audio
 - Expression input
 - 6 knobs, 2 three-way SPDT switches (or subset of these if desired)
 - Two Footswitches
 - Optional single footswitch and 3 color RGB led (5mm) configuration
 - 9v DC center negative (standard guitar pedal power)
 - Buffered input/output when effect active
 - True Bypass Relay with active muting for quiet switching
 - 48kHz/24 bit digital audio processing (adjustable in code up to 216kHz with CS4270)
 - 4 layer pcb with internal ground planes

Dev Features:
 - 280MHz core processing speed
 - 1.4MB SRAM (1MB contiguous block available)
 - 128KB of Flash for storing programs
 - Cirrus Logic CS4270 codec (24-bit, 216 kHz, 105 dB dynamic range)
 - Code set up to use STM32CubeIde with c++
 - Program/Debug over 10 pin JTAG header using [ST-Link v3](https://www.digikey.com/en/products/detail/stmicroelectronics/STLINK-V3MINIE/16284301)


## Hardware

Typical build process:

1. Order the assembled PCB from JLCpcb (or other manufacturer), minimum 2 assembled. I use ENIG finish and keep the other pcb options as default. (Typical two week order time)
2. Order the soldered components (Six 16mm right angle potentiometers, 2 three way toggles, 2 Footswitches (and wire), Two 3mm LEDs, Two 3mm LED bezels) (Tayda, LoveMySwitches, Mouser, etc.) (Typical 1-2 week order time)
3. Order the drilled enclosure from Tayda (powder coat / UV print optional). (Varies two weeks to a month order time depending on how busy they are)
4. Solder potentiometers and switches (Ensure to insulate the toggle base using electrical tape, tight fit with potentiometers), and fit check with enclosure (use enclosure to ensure proper fit while soldering).
5. Remove pcb from enclosure, solder LEDs using enclosure as guide for height. (Double check pcb layout for correct orientation)
6. Solder wires to footswitches, solder footswitch wires to pcb.
7. Final install of pcb with all components, bolt on all components to enclosure. Install knobs on potentiometers.

IMPORTANT: When ordering the pcb using the included Gerbers, you will need to change the orientation of certain parts (most of the ICs) using the visualization tool (during JLCPcb's ordering process). This is because the part data from Kicad exported to the .pos file are different from what JLCpcb expects. Compare part orientation from their online tool to the Kicad PCB layout. Double check all ICs, relays, transistors, LDOs, and diodes.

IMPORTANT: The control layout spacing for the potentiometers and toggles is very tight. The toggle switch base must be insulated from the neighboring potentiometer legs or it could create a short. I typically wrap the base of the toggles in electrical tape twice to ensure proper insulation. 

NOTE: The power jack is intended to protrude from the pcb edge, fitting slightly into the rectangle cutout in the enclosure. May take some adjustment once potentiometers are soldered, recommended to adjust prior to soldering potentiometers/toggles, using the three 1/4" jack mounts to hold in place. 


Tayda Drill Templates:
[Two Footswitch, Two Single Color LED version]()
[Single Footswitch and RGB LED version]()

NOTE: If you want to use the Single Footswitch/RGB LED version, you will need to change the peripheral settings. One of the footswitches changes from a GPIO input to a GPIO output (use pcb layout and schematic for guidance). Alternatively, you can change the LED pins from on/off GPIO outputs to Timer based PWM for brightness control. 


![app](https://github.com/GuitarML/GuitarPedalSTM32/blob/main/images/pcb_pic.jpg)


## Software

The effect in the main branch is an example delay/reverb using [DaisySP](https://github.com/electro-smith/daisysp). Other effects may be added as separate branches in this repo. I've found this to be the cleanest way to keep different configurations separate, rather than trying to comment out certain parts or use ifdef's. By default, the right footswitch is bypass, and the left footswitch is an aux function activated by holding the footswitch. This of course can be changed to whatever you want to do.

Connecting the pedal with ST-Link v3:
![app](https://github.com/GuitarML/GuitarPedalSTM32/blob/main/images/stlink_pic.jpg)


### Importing the Project

I use STM32CubeIDE version 2.2 for Windows. If you use a different version, migrating the code or updating your IDE may be required.

1. Clone the repository 
2. Open STM32CubeIDE.
3. Go to File > Import
4. Select General > Existing Projects into Workspace and click Next.
5. Browse to the Root Directory where you cloned the project. Ensure "Copy projects into workspace" is Unchecked. 
6. Click Finish.


### Set the Boot Configuration Bytes (option bytes)

You may need to use STM32CubeProgrammer to set the boot configuration bytes, telling the MCU to boot from flash.
If this is not set properly, it will work the first time after flashing from STM32CubeIDE, but after resetting power it will boot from the bootloader and not run your compiled program from flash memory.

Note: This can also be done in hardware, by pulling the boot pin low by connecting it to ground through a resistor. This wasn't required from my previous board using a different STM32H7, so I didn't do this on this hardware design. May be worth updating on future revisions.

1. Download and install STM32CubeProgrammer.
2. Connect the pedal via the 10 pin JTAG header using ST-Linkv3 and also apply power to the pedal from a 9v DC center negative adapter (the normal way you would power the pedal).
3. Select the JTAG port from the menu in STM32CubeProgrammer and Connect.
4. Navigate to "Boot address Option Bytes" and click drop down.
5. Modify both BOOT_CM7_ADD0 and BOOT_CM7_ADD1 rows to Value: "0x800" and Address: "0x08000000"  (no quotation marks)
6. Click Apply to save settings.
7. Verify the pedal boots from flash (runs your program) when cycling power.

### Using c++ instead of c with STM32CubeIDE

This project has been set up as a c++ project, however, STM32CubeIDE still auto generates a .c when moving from the MX view to the code view. As a work-around, if changes are made to the .ioc file in MX view, you will need to rename "main.cpp" to "main.c" beforehand. Once it is renamed, make the changes in MX and save and generate code. The code will be generated in "main.c". You can then rename "main.c" to "main.cpp" to compile the program. If you don't rename, a "main.c" file will be generated with your changes, and no changes will be made to "main.cpp".

## PCB Design Choices and Trade Offs

This particular pedal is intended as a platform for myself and others to design and test new guitar effect algorithms. As someone
coming from using the [Daisy Seed](https://daisy.audio/), I wanted to dive deeper into understanding how microcontrollers work,
and how to design custom boards using STM32. I also wanted to do something different from my Daisy Seed based [SoundSketch](https://github.com/GuitarML/SoundSketch),
rather than just try and recreate that same pedal without the Daisy Seed. The result is a more limited version of that pedal, using Mono processing,
expression, no Midi, and a slower MCU. It does have more internal SRAM (1.4MB vs 1MB), with a 1MB contiguous block (vs Daisy Seed / H750 512KB contiguous block), that 
can be used for things like data arrays and audio buffers.

There were also practical considerations / trade offs such as part availability and price through JLCpcb parts library. I wanted to design 
a high quality board while maintaining a minimal cost for pcb manufacturing and assembly. If you were to design your own board for commercial use you
may want to consider these trade-offs and make changes that best fit your needs:

1. CS4270 codec (discontinued but still in circulation and available at JLCPcb) chosen for cheap price (~$2.50) vs. later CS4272 or more modern TAC5242 (used on Daisy Seed v3).
2. Class 2 MLCC capacitors used in all locations except power filtering. For the audio path it's typically recommended to use Class 1 MLCC, Tantalum, or Electrolytic, due to Class 2 MLCCs being microphonic. Due to availability/price the Class 2 MLCCs are used in my design, but I haven't noticed lower quality audio or microphonic noise from the pedal.
3. Stereo audio jacks are used even though I only use Mono here. These are the ones typically available by JLCpcb, I haven't seen the mono, 1/4" board mounted jacks. The ring connection is unused and grounded.
4. Other improvements for manufacturing I've considered (but haven't gotten around to) is clip-in footswitches (ease of removing/installing if they go bad), and board mounted LEDs with light pipes (although this would require 2 sided assembly).


It was important to me that this design was a single board, with one sided assembly, to keep the price down, but a common practice in commercial pedals is to have a separate board for controls connected via a ribbon cable. This would allow for cheap repair if a potentiometer or switch goes bad.

When I ordered two assembled boards (with 3 blank pcbs due to minimum 5 at JLCpcb), the total price with shipping to USA was $250, with $90 of that being shipping/tariffs. The base price was around $160, which I think is reasonable. As the quantity goes up, the price per board decreases. This is due to the additional $3 per unique "Extended" part (as opposed to "Basic" parts) in the JLCpcb parts library. The $3 is a one time charge per unique Extended part in the BOM no matter how many pcbs you order. This fee is for manually loading the reels into the pick and place machine. The resistors/capacitors with standard values are "Basic", while pretty much everything else in this design is "Extended". 

My personal preference as a guitarist: Stereo and Midi are cool, but as a guitar player with a single amp, I typically don't use these features that seem to be the standard for modern digital pedals these days. Removing these options also frees up my creative bandwidth to focus on the DSP, which is my main interest. Still, it would be fairly easy to modify my design and add a second audio path for stereo, or a midi circuit on the UART peripheral. For references, see the [SoundSketch](https://github.com/GuitarML/SoundSketch) pedal, which has these features using a Daisy Seed.


## Acknowledgements

The pcb design and code rely heavily on what I learned from [Phil's Lab](https://www.youtube.com/@PhilsLab) YouTube channel, as well
as his related open source repositories on [Github](https://github.com/pms67). The final MCU/codec combination I use here is the same used
in the guitar pedal shown in many of the Phil's Lab videos. 