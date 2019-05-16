

/*
   GliderScore portable time display with stopwatch
   Initial release 1.01 April 28th, 2019
   Release 1.1 May 16th, 2019 added battery level display for 1S and 2S batteries
   
   Based on O.Segouin wireless big display for GliderScore

   Because we had some conflicts on the SPI bus when both the display and the nRF24L01 module were connected
   on the same bus, we had to use a soft SPI for the 5110 display.

   ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
   This is a beerware; if you like it and if we meet some day, you can pay us a beer in return!
   ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
*/

#include "RF24.h"
#include <U8g2lib.h>    // Oled U8g2 library          from https://github.com/olikraus/U8g2_Arduino/archive/master.zip
#include <Arduino.h>

// Variables and constants
const int RADIO_SS_PIN  = 8;
const int RADIO_CSN_PIN = 9;
RF24 radio(RADIO_SS_PIN, RADIO_CSN_PIN); // CE, CSN
const byte address[6] = "00001";
String manche = "0";
String groupe = "0";
String chronoS = "00:00";
char chrono[32] = "";
uint16_t x, y;
boolean flag = false;
char sum;
float compValue = 1.083538; //correction factor to match real resistors ratio used to measure battery level

String temps;
int bp = 2, fade = 0;
int raz = 14;

int sensorValue = 0;
int sensorPin = A0;
int voltagePin = A4;

volatile unsigned long debut;
volatile byte marche = false;
volatile unsigned long le_temps = 0, le_temps1 = 0;
unsigned long tempo = 0;
boolean flip_flop = false;
bool Dot = true; //Dot state
String chronoS1 = "1";
String chronoS2 = "2";
String chronoS3 = "3";
String chronoS4 = "4";
String statutS = "NO";
float a_mini = 3.9;   //minimum battery voltage for display 1s
float a_maxi = 4.2;   //maximum battery voltage for display 1s
float tension;
float iTension = 0;
float ax, bx;

U8G2_PCD8544_84X48_F_4W_SW_SPI u8g2(U8G2_R0, /* clock=*/ 7, /* data=*/ 6, /* cs=*/ 4, /* dc=*/ 3, /* reset=*/ 5);  // Nokia 5110 Display

String lead_zero(int num) {
  String t = "";
  if (num < 10) t = "0";
  return t + String(num);
}

ISR(TIMER2_COMPA_vect) { //timer2 interrupt 2kHz
  if (marche) {
    le_temps1 = le_temps1 + 1;
    le_temps = le_temps1 / 2;
  }
}

void isr1(void) {
  detachInterrupt(digitalPinToInterrupt(bp));
  if (!marche) {
    marche = true;
  } else marche = false;
  delay(50);
  attachInterrupt(digitalPinToInterrupt(bp), isr1, FALLING);
}

void setup(void) {
  Serial.begin(9600);
  bx = 1744 / 81;
  ax = (4 - bx) / 710;
  iTension = analogRead(voltagePin);
  tension = (iTension * ax) + bx;
  if (tension > 5) { //if 2S battery then minimum and maximum voltages need to be adapted
    a_mini = 6;   //minimum battery voltage for display 2s
    a_maxi = a_maxi * 2; //maximum battery voltage for display 2s
  }
  radio.begin();
  radio.openReadingPipe(0, address);
  radio.setChannel(1);// 108 2.508 Ghz
  radio.setPALevel(RF24_PA_MAX);
  radio.setDataRate(RF24_1MBPS);
  //radio.setDataRate(RF24_250KBPS);
  radio.setAutoAck(1);                     // Ensure autoACK is enabled
  radio.setRetries(2, 15);                 // Optionally, increase the delay between retries & # of retries
  radio.setCRCLength(RF24_CRC_8);          // Use 8-bit CRC for performance
  radio.openReadingPipe(0, address);
  radio.startListening();

  cli();//stop interrupts
  TCCR2A = 0;// set entire TCCR0A register to 0
  TCCR2B = 0;// same for TCCR0B
  TCNT2  = 0;//initialize counter value to 0
  // set compare match register for 2khz increments
  OCR2A = 249;// = (16*10^6) / (400*64) - 1 (must be <256)
  // turn on CTC mode
  TCCR2A |= (1 << WGM01);
  // Set CS01 and CS00 bits for 64 prescaler
  TCCR2B |= (1 << CS01) | (1 << CS00);
  // enable timer compare interrupt
  TIMSK2 |= (1 << OCIE0A);
  sei();//allow interrupts

  u8g2.begin();

  pinMode(bp, INPUT);
  digitalWrite(bp, HIGH); //Pullup
  pinMode(raz, INPUT);
  digitalWrite(raz, HIGH); //Pullup
  attachInterrupt(digitalPinToInterrupt(bp), isr1, FALLING);
}

String cnv_temps(unsigned long t) {
  unsigned long  ce, se, mi;
  double tt;
  String l_temps;
  tt = t;
  mi = t / 60000;
  t = t - (mi * 60000);
  se = t / 1000;
  t = t - (1000 * se);
  ce = t / 10;
  l_temps = lead_zero(mi) + ":" + lead_zero(se) + ":" + lead_zero(ce);
  return l_temps;
}


String checksum(String chaine)
{
  sum = 0;
  for (byte i = 0; i < (chaine.length() - 3); i++)
  {
    sum = chaine[i] + sum;
  }
  sum = (sum & 0x3F) + 0x20;
  return (chaine.substring(0, chaine.length() - 1) + sum);
}

void loop(void) {
  iTension = analogRead(voltagePin);
  tension = (iTension * ax) + bx;
  if (tension > a_maxi) tension = a_maxi;
  if (tension < a_mini) tension = a_mini;

  String Sep = " ";

  if ((millis() - tempo) >= 500) {
    flip_flop = !flip_flop;
    tempo = millis();
  }

  if (radio.available()) {
    char text[32] = "";
    radio.read(&text, sizeof(text));
    if (text[0] == 'R') // c'est bien une trame de gliderscore R01G1 ...trame commence par R
    {
      if (strcmp(text, checksum(text) == 0)) {
        manche = String(text[1]) + String(text[2]);
        groupe = String(text[4]) + String(text[5]);
        if (flip_flop)  Sep = " "; else {
          Sep = ":";
        }
        chronoS = String(text[7]) + String(text[8]) + Sep + String(text[9]) + String(text[10]);
        statutS = String(text[11]) + String(text[12]);
      }
    }
  }

  if (!marche) {
    if (!digitalRead(raz)) {
      le_temps1 = 0; le_temps = 0;
    }
  }
  temps = cnv_temps(le_temps);

  u8g2.clearBuffer();

  u8g2.drawFrame(0, 0, 41, 14); //RND Frame
  u8g2.drawFrame(43, 0, 41, 14); //GRP Frame

  u8g2.drawFrame(0, 16, 83, 14); //Working time Frame
  u8g2.drawFrame(0, 32, 83, 14); //Flight time Frame

  u8g2.setFont(u8g2_font_7x14_tf ); //u8g2_font_8x13B_mn );
  u8g2.setCursor(2, 1 + 1 * 11);
  u8g2.print("RND");
  u8g2.setFont(u8g2_font_7x14B_tf); //u8g2_font_8x13B_mn );
  u8g2.setCursor(31 - 6, 1 + 1 * 11);
  u8g2.print(manche);

  u8g2.setFont(u8g2_font_7x14_tf ); //u8g2_font_8x13B_mn );
  u8g2.setCursor(45, 1 + 1 * 11);
  u8g2.print("GRP");
  u8g2.setFont(u8g2_font_7x14B_tf); //u8g2_font_8x13B_mn );
  u8g2.setCursor(74 - 6, 1 + 1 * 11);
  u8g2.print(groupe);

  u8g2.setCursor(2 + 11, 17 + 11);
  u8g2.setFont(u8g2_font_7x14_tf);
  u8g2.print(statutS);
  u8g2.setFont(u8g2_font_8x13B_mn );
  u8g2.setCursor(35, 17 + 11);
  u8g2.print(chronoS);

  u8g2.drawFrame(4, 28 - 10, 5, 3);
  u8g2.drawFrame(3, 28 - 8, 7, 8);
  int pile  = ((tension - a_mini) / (a_maxi - a_mini)) * 8; //compute battery level
  u8g2.drawBox(3, 28 - pile, 7, pile);

  u8g2.setCursor(11, 33 + 11);
  u8g2.print(temps);
  u8g2.sendBuffer();					// transfer internal memory to the display
  delay(30);
}
