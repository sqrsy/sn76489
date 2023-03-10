#include "eurotools-v2.h"
#include "update_sn_code.h"
#include "write_sn_code.h"

// set up pin config
int PIN_CV_TONE = A4;
int PIN_POT_TONE = A1;
int PIN_CV_VOLUME = A3;
int PIN_POT_VOLUME = A0;
int PIN_GLITCH = 7;

// set up hardware constants
int REVERSE_GLITCH = true; // set true depends on orientation of switch
int REVERSE_POT = true; // set true if clockwise increases resistance
int R1_VALUE = 220;
int R2_VALUE = 150;

// set up to read tone CV and compute Hz
long incoming_cv_tone = 0;
int tone_history[8];
long Hz = 0;
long Hz_old = 0;
int Hz_knob_pct;
float Hz_offset;

// set up to read volume CV
int incoming_cv_volume = 0;
int volume = 0;
int volume_old = 0;
int volume_knob_pct;
int volume_offset;

// set up to read glitch switch
bool glitch_switch_active = false;
bool glitch_old = false;

// toggle debug
bool debug = false;

void setup() {

  // put your setup code here, to run once:
  if(debug) Serial.begin(9600);

  // set up all PINS for the SN audio chip
  pinMode(PIN_WE, OUTPUT);
  pinMode(PIN_CE, OUTPUT);
  pinMode(PIN_D0, OUTPUT);
  pinMode(PIN_D1, OUTPUT);
  pinMode(PIN_D2, OUTPUT);
  pinMode(PIN_D3, OUTPUT);
  pinMode(PIN_D4, OUTPUT);
  pinMode(PIN_D5, OUTPUT);
  pinMode(PIN_D6, OUTPUT);
  pinMode(PIN_D7, OUTPUT);

  // set up all PINS for the interface
  pinMode(PIN_CV_TONE, INPUT);
  pinMode(PIN_POT_TONE, INPUT);
  pinMode(PIN_CV_VOLUME, INPUT);
  pinMode(PIN_POT_VOLUME, INPUT);
  pinMode(PIN_GLITCH, INPUT_PULLUP);

  //////////////////////////////////////////////////////////////////
  ///// SEND THE INTERNAL CLOCK OUT (NANO EVERY ONLY)
  //////////////////////////////////////////////////////////////////

  // LUT0 is the D input, which is connected to the flipflop output
  CCL.LUT0CTRLB = CCL_INSEL0_FEEDBACK_gc; // Input from sequencer
  CCL.TRUTH0 = 1; // Invert the input
  CCL.SEQCTRL0 = CCL_SEQSEL0_DFF_gc; // D flipflop
  // The following line configures using the system clock, OUTEN, and ENABLE
  CCL.LUT0CTRLA = CCL_OUTEN_bm | CCL_ENABLE_bm;
  // LUT1 is the D flipflop G input, which is always high
  CCL.TRUTH1 = 0xff; 
  CCL.LUT1CTRLA = CCL_ENABLE_bm; // Turn on LUT1
  CCL.CTRLA = 1; // Enable the CCL

  //////////////////////////////////////////////////////////////////
  ///// SHUT OFF CHANNELS 2 & 3
  //////////////////////////////////////////////////////////////////
  
  update_sn_code(data, 0, true, false, debug); // force write 0 volume to tone channel
  write_sn_code(data); // clear channel 1
  data[1] = 0;
  data[2] = 1;
  write_sn_code(data); // clear channel 2
  data[1] = 1;
  data[2] = 0;
  write_sn_code(data); // clear channel 3
  data[1] = 1;
  data[2] = 1;
  write_sn_code(data); // clear channel 4
}

void loop() {

  //////////////////////////////////////////////////////////////////
  ///// READ TONE CV
  //////////////////////////////////////////////////////////////////

  // Arduino does not have enough precision to read CV for tone accurately
  // so the program will remember the last 8 CV values and use the
  // the average to make the output frequency
  // Increasing the memory bank of CV values will increase precision
  // but will increase "glide" as you change between notes
  incoming_cv_tone = read_analog_mV_smooth(PIN_CV_TONE, tone_history, R1_VALUE, R2_VALUE, debug);

  //////////////////////////////////////////////////////////////////
  ///// READ GLITCH SWITCH
  //////////////////////////////////////////////////////////////////

  // DECIDE WHETHER TO WRITE TO NOISE OR TONE REGISTER
  glitch_old = glitch_switch_active;
  glitch_switch_active = digitalRead(PIN_GLITCH);
  if(REVERSE_GLITCH) glitch_switch_active = !glitch_switch_active;

  // this keeps the glitch switch sounding funky even when no tone CV cable is plugged in
  // it forces the program to treat small voltage readings as true zero
  if(glitch_switch_active){
    if(incoming_cv_tone < 36){
      incoming_cv_tone = 0;
    }
  }

  //////////////////////////////////////////////////////////////////
  ///// PROCESS TONE CV TO HERTZ
  //////////////////////////////////////////////////////////////////

  // by default, program will have tune knob fine tune +/- about 1 semitone
  // but you can change this to coarse tune +/- 1 octave instead
  Hz_old = Hz;
  if(true){
    // READ IN CONTROL VOLTAGE FOR SYNTH PITCH -- FINE TUNE
    Hz = mV_to_Hz(incoming_cv_tone, 256); // 256 is lowest note with 8 MHz clock, for 0V and Hz_offset = 1
    Hz_knob_pct = read_analog_pct(PIN_POT_TONE, 3300, REVERSE_POT, 0, 0, debug);
    Hz_offset = pct_as_base2_offset(Hz_knob_pct, 1, 1);
    Hz = Hz * power_float(Hz_offset, .1); // moves Hz +/- less than one octave
  }else{
    // READ IN CONTROL VOLTAGE FOR SYNTH PITCH -- COARSE TUNE
    Hz = mV_to_Hz(incoming_cv_tone, 256); // 256 is lowest note with 8 MHz clock, for 0V and Hz_offset = 1
    Hz_knob_pct = read_analog_pct(PIN_POT_TONE, 3300, REVERSE_POT, 0, 0, debug);
    Hz_offset = pct_as_base2_offset(Hz_knob_pct, 1, 1);
    Hz = Hz * Hz_offset; // moves Hz +/- one octave
  }

  //////////////////////////////////////////////////////////////////
  ///// PROCESS VOLUME CV
  //////////////////////////////////////////////////////////////////

  // READ IN CONTROL VOLTAGE FOR SYNTH VOLUME
  volume_old = volume;
  incoming_cv_volume = read_analog_mV(PIN_CV_VOLUME, R1_VALUE, R2_VALUE, debug);
  volume = mV_to_integer(incoming_cv_volume, 32);
  volume_knob_pct = read_analog_pct(PIN_POT_VOLUME, 3300, REVERSE_POT, 0, 0, debug);
  volume_offset = pct_as_decimal_offset(volume_knob_pct, 32);
  volume = clip_integer(volume + volume_offset - 1, 0, 31); // ranges from 0 to 31
  
  // debug print...
  if(debug){
    Serial.println ("Current Write instructions...");
    Serial.print ("Tone: ");
    Serial.println (Hz);
    Serial.print ("Volume: ");
    Serial.println (volume);
    Serial.print ("Glitch: ");
    Serial.println (glitch_switch_active);
  }

  //////////////////////////////////////////////////////////////////
  ///// SWITCH CHANNELS BY SILENCING OTHER
  //////////////////////////////////////////////////////////////////

  // WHEN REGISTER CHANGES, SET ALL REGISTER VOLUMES TO 0
  if(glitch_switch_active != glitch_old){
    update_sn_code(data, 0, true, false, debug); // force write 0 volume to tone channel
    write_sn_code(data);
    update_sn_code(data, 0, true, true, debug); // force write 0 volume to noise channel
    write_sn_code(data);
    Hz_old = 0; // reset to force update
    volume_old = 0; // reset to force update
  }

  //////////////////////////////////////////////////////////////////
  ///// WRITE OUTPUT
  //////////////////////////////////////////////////////////////////

  // WRITE NEW TONE IF TONE HAS CHANGED
  if(Hz != Hz_old){
    if(debug) Serial.println ("Update Hz");
    update_sn_code(data, Hz, false, glitch_switch_active, debug);
    write_sn_code(data);
  }

  // WRITE NEW VOLUME IF VOLUME HAS CHANGED
  if(volume != volume_old){
    if(debug) Serial.println ("Update volume");
    update_sn_code(data, volume, true, glitch_switch_active, debug);
    write_sn_code(data);
  }

  if(debug) delay(250);
}
