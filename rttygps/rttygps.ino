// Arduino RTTY Modulator
// Uses Fast PWM to produce ~8kHz 8bit audio

#include "baudot.h"
#include "pwmsine.h"

// Yeah, I really need to get rid of these globals.
// Thats next on the to-do list

unsigned int sampleRate = 7750;
unsigned int tableSize = sizeof(sine)/sizeof(char);
unsigned int pstn = 0;
int sign = -1;
unsigned int change = 0;
unsigned int count = 0;

int fmark = 870;
int fspac = 700;
int baud = 45;
int bits = 8;
char lsbf = 0;

const int maxmsg = 128;
char msg[maxmsg] = "";

unsigned char bitPstn = 0;
int bytePstn = 0;
unsigned char tx = 0;

unsigned char charbuf = 0;
unsigned char shiftToNum = 0;
unsigned char justshifted = 0;

// compute values for tones.
unsigned int dmark = (unsigned int)((2*(long)tableSize*(long)fmark)/((long)sampleRate));
unsigned int dspac = (unsigned int)((2*(long)tableSize*(long)fspac)/((long)sampleRate));
int msgSize;
unsigned int sampPerSymb = (unsigned int)(sampleRate/baud);

char delim[] = ":";
char nl[] = "\n";
char call[] = "W8UPD";
char nulls[] = "RRRRR";

char lat_deg = 0;
char lon_deg = 0;
float lat_f = 0;
float lon_f = 0;

void setup() {

  // stop interrupts for setup
  cli();

  // setup pins for output
  pinMode(3, OUTPUT);
  pinMode(13, OUTPUT);

  // setup counter 2 for fast PWM output on pin 3 (arduino)
  TCCR2A = _BV(COM2B1) | _BV(WGM21) | _BV(WGM20);
  // TCCR2B = _BV(CS20); // TinyDuino at 8MHz
  TCCR2B = _BV(CS21); // Arduino at 16MHz
  TIMSK2 = _BV(TOIE2);
  
  count = sampPerSymb;

  // begin serial communication
  Serial.begin(9600);
  Serial.println("Started");

  // re-enable interrupts
  sei();
}

int getgps(){

   if (tx == 1) return 0;
  
  // GPS Data
  const char buflen = 16;
  char time[buflen] = "";
  char longitude[buflen] = "";
  char NS[1] = "";
  char latitude[buflen] = "";
  char EW[1] = "";
  char altitude[buflen] = "";
  char alt_units[1] = "";
  char strbuf[2] = "";

  // intialize Finite fsm_state Machine
  char buffer[16] = { 0 }; 
  char fsm_state = 0;
  char commas = 0;
  char lastcomma = 0;

  while(tx == 0){
    
    if (Serial.available()) {

      // move buffer down, make way for new char
      for(int i = 0; i <buflen; i++){
        buffer[i] = buffer[i+1];      
      }

      // read new char into buffer
      buffer[buflen-1] = Serial.read();
      //Serial.print(buffer[15]);

      if(buffer[buflen-1] == ',') commas++;

      // reset FSM on new data frame
      if(buffer[buflen-1] == '$') {
        commas = 0;
        fsm_state = 0;
      }

      // Finite fsm_state Machine
      if (fsm_state == 0) {

        if (buffer[buflen-6] == '$' &&
            buffer[buflen-5] == 'G' &&
            buffer[buflen-4] == 'P' &&
            buffer[buflen-3] == 'G' &&
            buffer[buflen-2] == 'G' &&
            buffer[buflen-1] == 'A'){
          fsm_state = 1;
        }

      } else if (fsm_state == 1) {

        // Grab time info
        // If this is the second comma in the last element of the buffer array
        if (buffer[buflen-1] == ',' && commas == 2){

          strncpy(time, "", buflen);
          for (unsigned char i = lastcomma; i < buflen-1; i++) {

            strbuf[0] = buffer[i];
            strncat(time, strbuf, (buflen-1)-i);
          }

          //next fsm_state
          fsm_state = 2;
        }

      } else if (fsm_state == 2) { 

        // Grab latitude
        if (buffer[buflen-1] == ',' && commas == 3){

          strncpy(latitude, "", buflen);
          for (unsigned char i = lastcomma; i < buflen-1; i++) {
            strbuf[0] = buffer[i];
            strncat(latitude, strbuf, (buflen-1)-i);
          }

          fsm_state = 3;
        }

      } else if (fsm_state == 3) {

        // Grab N or S reading
        if (buffer[buflen-1] == ',' && commas == 4){
          strncpy(NS, &buffer[buflen-2], 1);
          fsm_state = 4;
        }

      } else if (fsm_state == 4) {

        // Grab longitude
        if (buffer[buflen-1] == ',' && commas == 5){

          strncpy(longitude, "", buflen);
          for (unsigned char i = lastcomma; i < buflen-1; i++) {
            strbuf[0] = buffer[i];
            strncat(longitude, strbuf, (buflen-1)-i);
          }

          fsm_state = 5;
        }

      } else if (fsm_state == 5) {

        // Grab E or W reading
        if (buffer[buflen-1] == ',' && commas == 6){
          strncpy(EW, &buffer[buflen-2], 1);
          fsm_state = 6;
        }

      } else if (fsm_state == 6) {

        // Grab altitude
        if (buffer[buflen-1] == ',' && commas == 10){

          strncpy(altitude, "", buflen);
          for (unsigned char i = lastcomma; i < buflen-1; i++) {
            strbuf[0] = buffer[i];
            strncat(altitude, strbuf, (buflen-1)-i);
          }

          fsm_state = 7;
        }

      } else if (fsm_state == 7) {

        // Grab altitude units
        if (buffer[buflen-1] == ',' && commas == 11){
          strncpy(alt_units, &buffer[buflen-2], 1);
          fsm_state = 8;
        }

      } else if (fsm_state == 8) {
        
        // convert lat and lon from deg decimal minutes to decimal degrees
        lat_deg = ((latitude[0]-'0')*10)+(latitude[1]-'0');
        lat_f = lat_deg + ((float)atof(&latitude[2]))/60;
        lon_deg = ((longitude[0]-'0')*100)+((longitude[1]-'0')*10)+(longitude[2]-'0');
        lon_f = lon_deg + ((float)atof(&longitude[3]))/60;

        // make negative if needed
        if (NS[0] == 'S') lat_f = -lat_f;     
        if (EW[0] == 'W') lon_f = -lon_f;   
        
        // convert back to strings
        dtostrf(lat_f, 8, 5, latitude);
        dtostrf(lon_f, 8, 5, longitude);        
        
        // make the message
        strncpy(msg, nulls, maxmsg);
        strncat(msg, nl, maxmsg);
        strncat(msg, delim, maxmsg);
        strncat(msg, call, maxmsg);
        strncat(msg, delim, maxmsg);
        strncat(msg, latitude, maxmsg);
        strncat(msg, delim, maxmsg);
        strncat(msg, longitude, maxmsg);
        strncat(msg, delim, maxmsg);
        strncat(msg, altitude, maxmsg);
        strncat(msg, delim, maxmsg);
        strncat(msg, time, 6);
        strncat(msg, delim, maxmsg);
        strncat(msg, nl, maxmsg);
        strncat(msg, nl, maxmsg);
        msgSize = strlen(msg);
        tx = 1;
        return msgSize;
      }
      
      if(buffer[buflen-1] == ',') {
        lastcomma = buflen-1;
      } else {
        lastcomma--;
      }
      
    }       
  }
  return 0;
}

char calcAmp(){
  pstn += change;

  // if the position rolls off, change sign and start where you left off
  if(pstn >= tableSize) {
    pstn = pstn%tableSize;
    sign *= -1;
  }

  // return the pwm value
  return (char)(128+(sign*sine[pstn]));
}

// sets the character buffer, the current character being sent
void setCbuff(){
    unsigned char i = 0;

    // Note: the <<2)+3 is how we put the start and stop bits in
    // the baudot table is MSB on right in the form 000xxxxx
    // so when we shift two and add three it becomes 0xxxxx11 which is
    // exactly the form we need for one start bit and two stop bits when read
    // from left to right

    // try to find a match of the current character in baudot
    for(i = 0; i < (sizeof(baudot_letters)/sizeof(char)); i++){

      // look in letters
      if(msg[bytePstn] == baudot_letters[i]) {

        // if coming from numbers, send shift to letters
        if(shiftToNum == 1){
          shiftToNum = 0;
          //bytePstn++;
          charbuf = ((baudot[31])<<2)+3;
          justshifted = 1;
        } else {
          charbuf = ((baudot[i])<<2)+3;
        }
      }

      //look in numbers
      if(msg[bytePstn] != ' ' && msg[bytePstn] != 10
		&& msg[bytePstn] == baudot_figures[i]) {
        if(shiftToNum == 0){
          shiftToNum = 1;
          //bytePstn++;
          charbuf = ((baudot[30])<<2)+3;
          justshifted = 1;
        } else {
          charbuf = ((baudot[i])<<2)+3;
        }
      }      
      

    }
    
  // dont increment bytePstn if were transmitting a shift-to character
  if(justshifted != 1) {
    //print letter you're transmitting
    Serial.write(msg[bytePstn]);
    bytePstn++;
  } else {
    justshifted = 0;
  }
}

void setSymb(char mve){

    // if its a 1 set change to dmark other wise set change to dspace
    if((charbuf&(0x01<<mve))>>mve) {
      change = dmark;
    } else {
      change = dspac;
    }
}

// int main
void loop() {
  getgps();
}

// This interrupt run on every sample (7128.5 times a second)
// though it should be every 7128.5 the sample rate had to be set differently
// to produce the correct baud rate and frequencies (that agree with the math)
// Why?! I'm finisheding to figure that one out
ISR(TIMER2_OVF_vect) {
  if (tx == 0) return;
  count--;

  // if we've played enough samples for the symbol were transmitting, get next symbol
  if (count <= 0){
    bitPstn++;
    count = sampPerSymb;
    
    // if were transmitting a stop bit make it 1.5x as long
    if (bitPstn == (bits-1)) count += count/2;

    // if were at the end of the character return to the begining of a
    // character and grab the next one
    if(bitPstn > (bits-1)) {
      bitPstn = 0;
      
      setCbuff();
      // if were at the end of the message, return to the begining
      if (bytePstn == msgSize){
        // clear variables used here
        bitPstn = 0;
        bytePstn = 0;
        count = 1;
        tx = 0;
        return;
      }
    }

    unsigned char mve = 0;
    // endianness thing
    if (lsbf != 1) {
      // MSB first
      mve = (bits-1)-bitPstn;
    } else {
      // LSB first
      mve = bitPstn;
    }

    // get if 0 or 1 that were transmitting
    setSymb(mve);
  }

  // set PWM duty cycle
  OCR2B = calcAmp();
}
