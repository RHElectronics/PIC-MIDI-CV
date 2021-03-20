/*
 * File:   rhv1.c
 * RH-V1 MIDI to CV controller
 * RH Electronics 2021
 *
 * Created on 21 February 2021, 16:51
 */

#define _XTAL_FREQ 4000000
#include <xc.h>
#include "config.h"

#define GATE_OUT RC0
#define TRIG_OUT RC1
#define DAC1_CS RC3
#define DAC2_CS RC2

#define true 1
#define false 0

//Midi variables
unsigned char midiByte;
unsigned char runningStatus;
unsigned char thirdByte;
unsigned char midi_a;
unsigned char midi_b;
unsigned char status;
unsigned char channel = 0;
unsigned char triggerNote = 0;
unsigned char lastNote = 0;

//Trigger output
unsigned char trigcount = 0;                //Trigger pulse counter
unsigned char trigtime = 254;               //Trigger time
unsigned char trigenab = false;             //Trigger enabled for timer
unsigned char envtrig = true;               //Re trigger enable

//Modulation
unsigned char modLvl = 0;                   //Mod Wheel (MIDI CC 1)
unsigned char auxLevel = 0;                 //Aux Input (MIDI CC 3)
unsigned int pitchBend = 0;                 //Pitch Bend Value

//Pitch Bend
const unsigned int oneVolt = 819;           //12 bit DAC level for 1 volt
unsigned char lastBend = 0x40;              //Pitch bend previous
unsigned int bendLvl = 0;
unsigned int tempBend;

//MIDI Parameters
unsigned char LowestNote = 24;              //Start of the note range, or what represents 0
unsigned char HighestNote = 84;             //End of the note range
unsigned char midiCh = 15;                  //MIDI Channel 15

//DAC voltage table for 1 volt per octave over 5 octaves
const unsigned int VCODAC[61] = {0,68,137,205,273,341,410,478,546,614,683,751,819,887,956,1024,
                            1092,1161,1229,1297,1365,1434,1502,1570,1638,1707,1775,1843,1911,1980,2048,2116,
                            2185,2253,2321,2389,2458,2526,2594,2662,2731,2799,2867,2935,3004,3072,3140,3209,
                            3277,3345,3413,3482,3550,3618,3686,3755,3823,3891,3959,4028,4095
                            };


void DACWrite(unsigned char dac, unsigned int data);
void DAC8Write(unsigned char dac, unsigned char data);
void handleMIDI(unsigned char runningStatus, unsigned char midi_a, unsigned char midi_b); 
void NoteOff(void);

void main(void) {
    
    OSCCON1 = 0b01100000;           //Internal oscillator, no division
    OSCFRQ = 0b00000010;            //4MHz
    
    ANSELA = 0;
    ANSELC = 0;
    TRISA = 0b00110111;
    TRISC = 0;
    WPUA = 0b00000001;

    //***************************************************** PPS
    RX1DTPPS = 0x05;                //RX - RA5
    RA4PPS = 0x0F;                  //TX - RA4
    
    SSP1DATPPS = 0x02;              //SDI RA2 (unused)
    RC5PPS = 0x15;                  //SCK RC5
    RC4PPS = 0x16;                  //SDO RC4
    
    //***************************************************** UART
    RC1STA = 0b10010000;
    TX1STA = 0b00100100;
    BAUD1CONbits.BRG16 = 1;
    SP1BRG = 31;                          //32150 baud, Transmit disabled
    
    //***************************************************** SPI
    SSP1CON1 = 0b00100000;               //FOSC /4 = 1MHz SPI
    CKP = 0;
    CKE = 1;
 
    //***************************************************** Interrupts
    RC1IE = 1;
    PEIE = 1;
    GIE = 1;
    
    DAC1_CS = 1;                                            //Set DAC CS high
    DAC2_CS = 1;
    
    DACWrite(0,0);                                          //DAC outputs to 0V
    DACWrite(1,oneVolt);                                    //DAC Pitch bend voltage to 1V
    
    while(1){
        
        if(RA0 == 0){midiCh = 15;}
        else {midiCh = 14;}                                 //Set MIDI channel according to input
                                                            //Clear the trigger output on loop
        if(trigenab == true) {
            trigcount ++;                                   //Only count if the trigger is active 
            if(trigcount >= trigtime){
                trigcount = 0;                              //Reset trigger time
                trigenab = false;                           //Disable any further counts
                TRIG_OUT = 0;
            }
        }
    }
}

void __interrupt() ISR(void) 
{
    //UART - MIDI In
    if (RC1IF == 1) {
        midiByte = RCREG;
        
        if(midiByte >=0xF0){return;}
        
        if (midiByte & 0b10000000) {                        // Header byte received
            runningStatus = midiByte;
            thirdByte = false;
        }
        
        else {
         if (thirdByte == true) {                           //Second data byte received
           midi_b = midiByte; 
           handleMIDI(runningStatus, midi_a, midi_b);       //Incoming data complete
           thirdByte = false;
           return;
          } else {                                          //First data byte received
          if (!runningStatus) {return;}                     //invalid data byte
          if (runningStatus <= 0xEF) {                      //First data byte of Note Off/On, Key Pressure or Control Change
             midi_a = midiByte;
                thirdByte = true;
                return;
               }
            }
        }
        RC1IF = 0;

        return;
    }
}

//*************************************************************
//12 bit DAC
//*************************************************************
void DACWrite(unsigned char dac, unsigned int data)
{
    unsigned char dacMSB = 0;
    unsigned char dacLSB = 0;
    dacMSB = (data >> 8) & 0xFF;
  
    if(dac == 1){ dacMSB |= 0x80;}                           //DAC B
  
    dacMSB |= 0b01110000;                                    //DAC parameters 
    dacLSB = (data & 0xFF);
    
    DAC1_CS = 0;
    
    SSP1BUF = dacMSB;                                        //Transmit MSB
    while(!SSP1STATbits.BF);
    SSP1BUF = dacLSB;                                        //Transmit LSB
    while(!SSP1STATbits.BF);

    DAC1_CS = 1;
    return;
}

//*************************************************************
//8 bit DAC
//*************************************************************
void DAC8Write(unsigned char dac, unsigned char data)
{
    unsigned char dacMSB = 0;
    unsigned char dacLSB = 0;
    dacMSB = (data >> 4);
  
    if(dac == 1){ dacMSB |= 0x80;}                           //DAC B
  
    dacMSB |= 0b01110000;                                    //DAC parameters 
    dacLSB = ((data <<4) & 0x0F);
    
    DAC2_CS = 0;
    
    SSP1BUF = dacMSB;                                        //Transmit MSB
    while(!SSP1STATbits.BF);
    SSP1BUF = dacLSB;                                        //Transmit LSB
    while(!SSP1STATbits.BF);

    DAC2_CS = 1;
    return;
}

void handleMIDI(unsigned char midiByte, unsigned char midi_a, unsigned char midi_b) {
    
    status = (midiByte & 0b11110000);                           //Status value
    channel = (midiByte & 0b00001111);                          //Channel value
        
    //Modulation Wheel
    if(status == 0xB0 && midi_a == 1 && channel == midiCh){
        modLvl = midi_b;                                        //Shift up to 8 bit
        DAC8Write(0,modLvl);                                    //Write out to 8 bit DAC
        return;
    }

    //Pitch Bend
    else if(status == 0xE0 && channel == midiCh){
 
        if(midi_b < lastBend && midi_b < 0x40){                 //Bend down from centre
            bendLvl = midi_b; 
            tempBend = (oneVolt + bendLvl);
        }    
        
        else if(midi_b < lastBend || midi_b > lastBend){        //Bend down from max or up 
            bendLvl = midi_b; 
            tempBend = (oneVolt + bendLvl);
        }
    
        else if(midi_b == 0x40){tempBend = oneVolt;}            //DAC Reset
        
        DACWrite(1,tempBend);                                   //Write to DAC
        lastBend = midi_b;                                      //Save last bend value for next processing
        return;
    }
    
    else if(status == 0x90 && channel == midiCh && midi_a >0 && midi_a >=LowestNote)
    {
        triggerNote = midi_a - LowestNote;                      //Triggered note for array
        if(midi_a > HighestNote){return;}                       //Return if the note is too high
        
        if(midi_b == 0){NoteOff(); return;}                     //Call note off if note on with velocity zero is received.
        
        DACWrite(0,VCODAC[triggerNote]);                        //Write DAC for VCO
        DAC8Write(1,midi_b);                                    //Send velocity
        
        if(GATE_OUT == 0){
            LATC = (LATC & ~0b00000011) | 0b00000011;           //Gate and trigger on (RA0 and RA1)
            trigenab = true;                                    //Timer to turn off
        }
        else if(envtrig == true && GATE_OUT == 1){              //Re trigger if enabled
            TRIG_OUT = 1;
            trigenab = true;
            DAC8Write(1,midi_b);                                //Re send Velocity            
        }
        
        trigcount = 0;                                          //Reset trigger counter for timer
        lastNote = midi_a;                                      //Store this note played incase of note rollover

        return;
    }
    
    else if(status == 0x80 && channel == midiCh && midi_a >=LowestNote) //Note OFF
    {
        NoteOff();                                              //Call Note Off
        return;
    }
    
    return;
}

void NoteOff(void) {
    if(lastNote == midi_a){                                     //Will only turn off the note that was last pressed e.g rollover key is now the new OFF key
        LATC = (LATC & ~0b00000011);                            //Gate and trigger off
        trigenab = false;                                       //Clear and disable everything
        DAC8Write(1,0);                                         //Turn off velocity output
    }
    
    return;
}