# NX-8E-ComFix
An Arduino-based solution for fixing a serial port race condition on the Interlogix NX-8E security panel

This Arduino sketch sits on the communications line between the GE Interlogix NX-8E security system and some other device, and mitigates
problems caused when the security system wishes to report a notification at the same time the other device (e.g. Crestron) wants to
send a command to the NX-8E panel.

# How to use

* Acquire an Arduino Mega board.
* Download the Arduino IDE from Arduino.cc and save the contents of NX-8E-ComFix.ino as a new sketch.
* Compile the code to run on Arduino Mega 2560 and upload it to your Arduino board using USB.
* Acquire two female RS232-to-TTL converters, you'll need them to translate RS232 into the 0-5V voltage domain required.  Arduino Mega cannot connect directly to RS232 voltages without this converter or it will be damaged.
* TX pins on Translator go to TX pins on Arduino.  RX pins on Translator go to RX pins on Arduino.  Do not invert.
* VCC and GND on Translators connect to 5V and GND pins on Arduino.
* The GE NX-8E needs to connect (via a translator) to Serial3 (pins 14-15)
* Connect the RS232 of the other device (e.g. Crestron, also via a translator) to Serial2 (pins 16-17)
* Edit the sketch to match your baud rate (note two occurrences of 19200 in setup... change to match)

Generally speaking, I expect you shouldn't need to change anything about the way your application is set up.
The sketch will pass valid traffic through, and automatically detect when timing mitigations need to occur, and
transparently does them.
