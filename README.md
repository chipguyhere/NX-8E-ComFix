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
* Edit the sketch to match your baud rate if needed (note reference to 9600 baud near top of .ino file).  Changing this, of course, requires reuploading the edited sketch to your board.

Generally speaking, I expect you shouldn't need to change anything about the way your application is set up.
The sketch will pass valid traffic through, and automatically detect when timing mitigations need to occur, and
transparently does them.  The sketch will automatically acknowledge messages that the panel wants acknowledged,
and will also silently discard any (unneeded) acknowledge messages that are sent by your application.

# About the issue this resolves

The NX-8E panel is programmed in such a way that when it decides to send a notification to your application,
it expects that the very next packet it will receive is an acknowledgment to that notification.  The panel
does not behave well if the next packet it receives is something different.  In my experimentation, the
panel will discard that packet, and then replay the notification within a second or two later, and
will frequently be numb to further commands (including repeats of the one it ignored) in the meantime.  Even if the
notification packet it wanted comes immediately after the command that wasn't what it wanted, the
panel is frequently pissed and isn't accepting of this, ignoring it until the notification is repeated later.

This presents a problem if a command packet was in transit when a notifiable event happens.
The panel doesn't properly consider this possibility.
Your command gets swallowed and then the panel is frequently unworkable while it pauses and waits for that
acknowledgment.  Your home automation system then has to figure out that the original command got ignored.

Enter my Arduino sketch.  It takes advantage of the fact that the NX-8E command protocol lets you abandon a
packet mid-transit just by starting a new one (which it recognizes due to the header).  The panel does not
get pissed if you abandon your packet in transit, then give it the notification it wants, and then begin your
command packet again.

A dedicated Arduino microcontroller has the timing precision to make this "abandon" decision at the last possible
microsecond in a way that your home automation controller, which runs a full-size multi-tasking OS, can't consistently
match.  Correction -- it probably could, if it had a "kernel driver" for the NX-8E interface.  But that's
not how it works on multi-tasking OS's on home automation controllers, where kernel drivers are limited to native
PC or USB or similar hardware that are directly part of the machine itself.

Since your home automation system is unlikely to offer kernel drivers for home security panels, everything that
pauses or slows down the home automation application introduces timing irregularities (sometimes very large
ones) that make it practically impossible to mitigate this issue -- hence clunky solutions that involve
using only polling, as a way to sidestep it.

Simply plug this Arduino Mega in between the home automation system and the NX-8E panel.  It will detect
the impending collision before it happens, and then silently abandon the outgoing request, queuing it in memory until the
required acknowledgment's out of the way.  Whew!  Other than a minimal delay, your app won't know anything happened.
