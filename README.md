Very simple assembler for a SAP-1 CPU.

![screenshot](/screenshot.png)

I've never written an assembler and this is a quick 30 minutes hack, so YMMV.

It should in theory also be able to work with an Arduino or similar connected
to the serial port to automatically write stuff to memory. The "protocol" is
newline separated pairs of hex-encoded bytes separated by spaces defining
address and value. E. g. `\n0x00 0xFF\n` should write 255 to address 0.

The idea is to eventually replace the serial port stuff with sound, and have a
quasi-modem (i. e. a bell 103 modem without the mo part) so I can program
without cheating. USB is too complex to implement on a breadboard, and the only
other output I have on my laptop is the headphone jack.

TODO
----

- User defined operators, including names, arguments, opcodes
- Syntax highlighting
- Better error checking (tracking where overlaps come from, missing initialization, uninitialized memory usage etc.)
- Saving and loading.
- The arduino part

