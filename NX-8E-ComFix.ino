/*
NX-8E-ComFix Copyright 2023 Michael Caldwell-Waller, License: GPLv3

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// Runs on: Arduino Mega 2560 or compatible
//
// This sketch is middleware that brokers access to a GE NX-8E serial port.
//
// Currently: it mitigates a race condition due to GE NX-8E bug, that occurs when
//  GE wants to send a transition notification at the same time a request is being sent to it.
//  Race condition loses the request and locks up the port for a couple seconds each time it is triggered.
//  Allows use of GE notifications with minimal risk that requests will be lost/delayed due to bug.
//
// GE NX-8E needs to be on Serial3.
// Client (Crestron) needs to be on Serial2.
// Serial1 reserved for future expansion.
// Serial (USB) available for debugging.

#define NX8E_BAUDRATE  9600



// Implementation of escaped-binary packet receiver protocol used by GE.
class rx {
  public:
    byte buf[256];
    int len = 0;
    bool in7D = false;
    bool isPacket = false;
    bool gotbyte(byte b) {
      bool wasEscaped = false;
      if (in7D) in7D = false, b = b ^ 0x20, wasEscaped = true;
      else if (b == 0x7D) in7D = true;
      if (in7D == false) {
        if (b == 0x7e && wasEscaped == false) {
          len = 1, buf[0] = 0x7e, isPacket = false;
        } else if (len > 0 && len < 255 && isPacket == false) {
          buf[len++] = b;
          if (len == (buf[1] + 4)) {
            uint16_t mysum = buf[len-2];
            mysum = (mysum << 8) + buf[len-1];
            if (computeChecksum(&buf[1], buf[1]+1) == mysum) {
              isPacket=true;
            } else {
              Serial.print("(BAD)");
              isPacket=true;
            }
          }
        }
      }
    }
    static uint16_t computeChecksum(byte *data, int len) {
      byte Sum1 = 0, Sum2 = 0;
      for (int z = 0; z < len; z++) {
        if ((255 - Sum1) < data[z]) Sum1++;
        Sum1 += data[z];
        if (Sum1 == 255) Sum1 = 0;
        if ((255 - Sum2) < Sum1) Sum2++;
        Sum2 = Sum2 + Sum1;
        if (Sum2 == 255) Sum2 = 0;
      }
      return (((uint16_t)(Sum1)) << 8) + (uint16_t)Sum2;
    }

} rx3, rx2;


volatile byte Serial3ReceivingBits=false;


// Interrupt service routine for picking up bit-level changes to Serial3 before whole byte arrival.
// This gives us microsecond-level notice that something is coming from the GE.
// It's specific to the Arduino Mega, but you can do without it, if not using the Mega.
#ifdef __AVR_ATmega2560__
ISR(PCINT1_vect) { Serial3ReceivingBits=true; }
#endif


byte buftoGE[256];
int buftoGElen=0;
int buftoGEidx=0;
int initialGEserialsize=0;


void setup() {

  // Turn on the registers in the Arduino Mega that give us bit-level interrupt reporting.
#ifdef __AVR_ATmega2560__
  PCICR |= (1 << PCIE1);           // Enable PCINT1 group (covering PCINT8 to PCINT15)
  PCMSK1 |= (1 << PCINT9);         // Enable PCINT9 which corresponds to Arduino pin 15  
#endif
  Serial.begin(115200);
  Serial3.begin(NX8E_BAUDRATE);
  Serial2.begin(NX8E_BAUDRATE);
  initialGEserialsize=Serial3.availableForWrite();
  Serial.print("buf size ");
  Serial.println(initialGEserialsize);
}

void sendPacketEscaped(Stream& ser, byte *buf, int len) {
  for (int i=0; i<len; i++) {
    byte b = buf[i];
    if (i>0 && (b==0x7E || b==0x7D)) {
      ser.write((byte)0x7D);
      ser.write((byte)b^0x20);
    } else {
      ser.write(b);
    }
  }
}


char beta[] = "0123456789ABCDEF";
char betb[] = "0123456789abcdef";

void printhex(byte who, byte b) {
  char c1, c2;
  if (who == 1) c1 = beta[b >> 4], c2 = beta[b & 15];
  else c1 = betb[b >> 4], c2 = betb[b & 15];

  if (b == 0x7E) {
    if (who == 2) Serial.println();
    else Serial.write(' ');
  }
  Serial.write(c1);
  Serial.write(c2);
}

int len1 = 0;

long allowTx1aftermillis;

void loop() {

  // Serial3 is the GE alarm system interface.
  // Grab a byte if available.
  int c = Serial3.read();
  if (c != -1) {

    // (Uncomment this print to see percent signs that show whether interrupt bit detection is working)
    //if (Serial3ReceivingBits) Serial.print('%');

    // Clear the flag that gives us advance notice on the next byte's incoming bits.
    Serial3ReceivingBits=false;

    // Pass it to the parser class, and see if it completes a packet.
    rx3.gotbyte(c);
    
    if (rx3.isPacket) {
      // Packet arrived.
      // It will be one of two types:
      // 1) Response packet
      // 2) Unsolicited "transition" packet with "acknowledge required" flag
      // if it's a must-ack packet, then ack it ourselves immediately.
      if (rx3.buf[2]&0x80) {
        // Packet requiring acknowledgment just arrived
        byte bb[] = {0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x01, 0x1d, 0x1e, 0x1f};
        Serial3.write(bb, 10);

        // Enforce a moratorium on sending any new requests (give queued notifications
        // a chance to arrive), and abandon any transmission in progress,
        // marking it for later restart by moving index back to beginning.
        allowTx1aftermillis=millis()+200;
        if (buftoGEidx) {
          Serial.print('@');
          Serial.print(buftoGEidx); // disclose how many bytes of transmission were discarded
          Serial.print('@');
        }
        buftoGEidx=0;
        Serial.println(" [auAC]:");
        // Also forward it to Crestron.
        // We will swallow Crestron's ACK later
      }
      sendPacketEscaped(Serial2, rx3.buf, rx3.len);

      // Print the packet in hex to the debug window.
      for (int z = 0; z < rx3.len; z++) {
        if (z==2 && rx3.buf[z]==0x84) Serial.print('-');
        printhex(1, rx3.buf[z]);
      }

      // Packet processing complete, prepare for next packet,
      // reset "len" to zero so that we can use len
      // to immediately detect the start of an unsolicited incoming notification packet.
      rx3.isPacket = false;
      rx3.len=0;
    }
  }

  // Get bytes/packets sent by the Crestron system
  c = Serial2.read();
  if (c != -1) {

    // Handle the received byte with the packet protocol.
    rx2.gotbyte(c);

    // Check to see if it's a packet
    // If we got a packet, copy it into "buffer to GE".
    // This maintains GE's prerogative to interrupt us with a transition message at any time.
    // If GE interrupts us, it will ignore our request until we acknowledge its interruption.
    // This "buffer to GE" enables us to restart an outgoing request that got interrupted.
    if (rx2.isPacket) {

      // Is it an acknowledgment message from Crestron?  (acknowledging a notification message)
      if (rx2.len == 5 && rx2.buf[2] == 0x1d) {
        // Swallow acknowledgment message and don't pass it along since we generate acknowledgments ourselves
        Serial.print("[swal]");
      } else {

        // Any other message:
        // Put it in buffer to GE, and set the length and index so it will be sent to GE byte-by-byte when appropriate.
        for (int z=0; z<rx2.len; z++) printhex(2, rx2.buf[z]);
        memcpy(buftoGE, rx2.buf, rx2.len);
        buftoGElen=rx2.len;
        buftoGEidx=0;
        rx2.isPacket=false;
      }
    }
  }

  // This is the part that sends bytes to GE when appropriate.
  if (buftoGElen) {
    long m = millis();
    if ((m - allowTx1aftermillis) > 0) {
      if (Serial3.availableForWrite() == initialGEserialsize) {
        // Nothing is in outgoing transit, and we have data that we COULD send.  Should we?
        if (rx3.len==0 && Serial3ReceivingBits==false) {
          // yes, incoming traffic looks clear.  Send a byte (escaping it if needed)
          byte b = buftoGE[buftoGEidx++];
          if (buftoGEidx>1 && (b==0x7d || b==0x7e)) {
            Serial3.write((byte)0x7d);
            Serial3.write(b^0x20);
          } else {
            Serial3.write(b);
          }
          // Pace our sending.  Outgoing requests are very short (like 6 bytes).
          // The slower we send outgoing requests, the more we
          // maximize our chances that if a transition is incoming
          // we can abort the outgoing tx before tx is complete
          // (ideal, since we'll positively know it didn't complete)
          // A smaller +number here will increase polling throughput on the port, and also the collision risk.        
          allowTx1aftermillis = m+15;  
          
          // Did we just send the last byte we wanted to send?  If so, end efforts to transmit.
          if (buftoGEidx==buftoGElen) {
            buftoGEidx=buftoGElen=0;
            Serial.print('*');
          }
        } else {
          // We are receiving data, and should restart any attempt to send later.
          // Note that the start byte 0x7E cancels any transmission in progress, so
          // we can abandon a transmission simply by not completing it.
          if (buftoGEidx > 0) {
            Serial.print('#');
            Serial.print(buftoGEidx);
            Serial.print('#');
          }
          // Setting this to 0 ensures the message will restart when the other criteria
          // preventing transmission (i.e. an inbound notification) are no longer present.
          buftoGEidx=0;
        }
      }
    }
  }
  c = Serial.read();
}
