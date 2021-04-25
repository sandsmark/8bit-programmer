Very simple assembler for a [SAP-1](https://www.amazon.com/Digital-Computer-Electronics-Albert-Malvino/dp/0028005945) CPU, both 4 bit and 8 bit memory address width.

Also includes very rudimentary modem support, to upload via a soundcard instead
of cheating by relying on an e. g. an Arduino that is a gazillion times as
powerful as our own system. So far only parts of the mo part has been
implemented.

![screenshot](/screenshot-2021-04-25.png)

I've never written an assembler and this is a quick 30 minutes hack, so YMMV.

It should in theory also be able to work with an Arduino or similar connected
to the serial port to automatically write stuff to memory. The "protocol" is
newline separated pairs of hex-encoded bytes separated by spaces defining
address and value. E. g. `\n0x00 0xFF\n` should write 255 to address 0.

The idea is to eventually replace the serial port stuff with sound, and have a
quasi-modem (i. e. a bell 103 modem without the mo part) so I can program
without cheating. USB is too complex to implement on a breadboard, and the only
other output I have on my laptop is the headphone jack.


Downloads
---------

Windows builds can be found here, just download the zip file with the highest number:

https://github.com/sandsmark/8bit-programmer/releases/tag/1

Opcodes
-------

Basically just these:
https://github.com/beneater/eeprom-programmer/blob/master/microcode-eeprom-with-flags/microcode-eeprom-with-flags.ino#L39-L54

In addition there's `.db address value [label]`. `[label]` is an optional name
for referring to the address of the value, so you don't need to manually update
adresses as you move things around. So `.db 0x3 0xaa foo` will program in the
value `0xaa` at memory adress `0x3`, and then you can write e. g. `lda foo`
elsewhere in the code.

Modem
-----

Loosely based on [Bell 103](https://en.wikipedia.org/wiki/Bell_103_modem). As
in for now just encoding in the right frequencies at the right baud rate, but
the correct encoding is not implemented yet. And not tested yet, I'm still
working on the hardware side now that I have the software to test it with.

Some random references (that I haven't read, as I am very lazy, but the
summaries seem relevant):
 - https://vigrey.com/blog/emulating-bell-103-modem
 - http://www.whence.com/minimodem/
 - https://freecircuitdiagram.com/1013-fsk-demodulator/
 - https://www.futurlec.com/Motorola/MC145443P.shtml


Random stuff I use or have used (because bookmarks are overrated):
 - https://archive.org/details/byte-magazine-1980-08/page/n23/mode/2up
 - http://www.vk2zay.net/calculators/lm567.php
 - https://electronics.stackexchange.com/questions/441501/fsk-generation-with-timer-555


Dependencies
------------

 - Qt: GUI framework
 - Miniaudio: Single-header C++ library for audio output and input.


TODO
----

- Demodulator part of the modem
- Generate audio output up front, not on the fly. miniaudio can't keep up with our baud rate.
- Configurable baud etc?
- Better error checking (tracking where overlaps come from, missing initialization, uninitialized memory usage etc.)
- User defined operators, including names, arguments, opcodes
- Actually add buttons for saving and loading, instead of just automatic.

