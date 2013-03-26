/*
WildStang Sign Control Box 2013
By: Josh Smith and Steve Garward
*/

#include "SPI.h"
#include "Wire.h"
#include "LiquidTWI.h"
#include "PinChangeInt.h"
//#include "ADXL362.h"
#include "patterns.h"

// Define the sign addresses
#define SIGN_1 11
#define SIGN_2 12
#define SIGN_3 13

// This means send to all signs
#define SIGN_ALL 255

// Pin definitions
#define RED_BUTTON 1
#define BLUE_BUTTON 7
#define PARTY_BUTTON 8

// LCD backlight RGB PWM pins
#define LCD_RED 5
#define LCD_GREEN 6
#define LCD_BLUE 9

// Encoder input pins
#define SELECT_BUTTON A1
#define ENCODER_A 2
#define ENCODER_B 3

// Mic I/O
#define MIC_STROBE 4
#define MIC_RESET 0
#define MIC_IN A2

volatile boolean patternSelectionChanged = false;
volatile boolean patternChanged = false;
volatile short pattern = 0;
volatile short selectedPattern = 1;

//volatile byte lastSequence;
//volatile byte sign = 1;

// Peripherals
LiquidTWI lcd(0);
//ADXL362 accel;
byte r, g, b;

/****************************** Sound Board Code ******************************/
int PIXELS_PER_SEGMENT = 2; //How many LEDs we want to use for each segment of the "VU meter"
float SENSITIVITY = 2.3; //This sets the sensitivity of the mic (increase for quiet environment (3.0 should be upper bound))

int spectrumValue[7]; // to hold a2d values
int baseline[7];
boolean initialise = false;
int totalVolume = 0;
int relativeLevel = 0;
int averageVolume = 0;
/******************************************************************************/

//#define WS_DEBUG

void setup()
{
   // Set the button pins as inputs
   pinMode(RED_BUTTON, INPUT);
   pinMode(BLUE_BUTTON, INPUT);
   pinMode(PARTY_BUTTON, INPUT);
   pinMode(SELECT_BUTTON, INPUT);
   // Turn on the button input pull-up resistors
   digitalWrite(RED_BUTTON, HIGH);
   digitalWrite(BLUE_BUTTON, HIGH);
   digitalWrite(PARTY_BUTTON, HIGH);
   digitalWrite(SELECT_BUTTON, HIGH);
  
   // Configure LCD backlight pins
   pinMode(LCD_RED, OUTPUT);
   pinMode(LCD_GREEN, OUTPUT);
   pinMode(LCD_BLUE, OUTPUT);

   // Set the Encoder pins to inputs
   pinMode(ENCODER_A, INPUT);
   pinMode(ENCODER_B, INPUT);
   // Turn on their internal pull-up resistors
   digitalWrite(ENCODER_A, HIGH);
   digitalWrite(ENCODER_B, HIGH);

   // Configure Mic pins
   pinMode(MIC_STROBE, OUTPUT);
   pinMode(MIC_RESET, OUTPUT);
   pinMode(MIC_IN, INPUT);
   
   analogReference(DEFAULT);

   digitalWrite(MIC_RESET, LOW);
   digitalWrite(MIC_STROBE, HIGH);
   
   //Normally we can't use delay but during setup it doesn't matter since I2C can't be called
   delay(5000);
   // Initialise and get a baseline
   digitalWrite(MIC_RESET, HIGH);
   digitalWrite(MIC_RESET, LOW);

   for (int i = 0; i < 7; i++)
   {
      baseline[i] = 0;
   }

   for (int j = 0; j < 10; j++)
   {
      for (int i = 0; i < 7; i++)
      {
         digitalWrite(MIC_STROBE, LOW);
         delayMicroseconds(30);  // to allow the output to settle
         baseline[i] += analogRead(MIC_IN);
         digitalWrite(MIC_STROBE, HIGH);
      }
   }

   for (int i = 0; i < 7; i++)
   {
      // Average the baseline readings
      baseline[i] = baseline[i] / 10;
   }

   // set up the LCD's number of rows and columns: 
   lcd.begin(16, 2);
   // Print a message to the LCD.
   lcd.print(" ~ WildStang ~ ");
      
//   accel.begin();
//   accel.beginMeasure();
   
   PCintPort::attachInterrupt(2, upInterrupt, FALLING);
   PCintPort::attachInterrupt(3, downInterrupt, FALLING);
   PCintPort::attachInterrupt(SELECT_BUTTON, selectInterrupt, FALLING);

//If we have defined WS_DEBUG, enable serial for debugging
#ifdef WS_DEBUG
  Serial.begin(9600);
#endif
}


void loop()
{
   //Check if the menu option has been changed and if so, update the menu appropriately
   if (isPatternSelectionChanged())
   {
      showSelection();
      clearPatternSelectionChanged();
   }
   
   //If we have a new pattern chosen, call the appropriate functions to handle the updates
   if (newPatternSelected())
   {
      showSelected();
      switch (pattern)
      {
      case PATTERN_BLANK:
         sendPattern(SIGN_ALL, PATTERN_BLANK, NULL, 0, 0);
         break;
      case PATTERN_RAINBOW:
         sendPattern(SIGN_ALL, PATTERN_RAINBOW, NULL, 0, 0);
         break;
      case PATTERN_RED_FILL:
         sendPattern(SIGN_ALL, PATTERN_RED_FILL, NULL, 0, 0);
        break;
      case PATTERN_BLUE_FILL:
       sendPattern(SIGN_ALL, PATTERN_BLUE_FILL, NULL, 0, 0);
      break;
      case PATTERN_RED_FILL_TILT:
       sendPattern(SIGN_1, PATTERN_RED_FILL, NULL, 0, 0);
//      while (accelAngle(X_AXIS, 30))
//     {
//        // Do nothing
//      }
      sendPattern(SIGN_2, PATTERN_RED_FILL_SHIMMER, NULL, 0, 0);
      timedWait(1000);
      sendPattern(SIGN_3, PATTERN_RED_FILL_SHIMMER, NULL, 0, 0);
         break;
      case PATTERN_BLUE_FILL_TILT:
      sendPattern(SIGN_1, PATTERN_BLUE_FILL, NULL, 0, 0);
//      while (accelAngle(X_AXIS, 30))
//      {
//        // Do nothing
//      }
      sendPattern(SIGN_2, PATTERN_BLUE_FILL_SHIMMER, NULL, 0, 0);
      timedWait(1000);
      sendPattern(SIGN_3, PATTERN_BLUE_FILL_SHIMMER, NULL, 0, 0);
         break;
      case PATTERN_TWINKLE:
      sendPattern(SIGN_ALL, PATTERN_TWINKLE, NULL, 0, 0);
         break;
      case PATTERN_EQ_METER:
         readSoundData();
         sendPattern(SIGN_1, PATTERN_EQ_METER, spectrumValue, 1, 2);
         sendPattern(SIGN_2, PATTERN_EQ_METER, spectrumValue, 3, 2);
         sendPattern(SIGN_3, PATTERN_EQ_METER, spectrumValue, 5, 2);
         break;
      default:
      sendPattern(SIGN_ALL, PATTERN_RAINBOW, NULL, 0, 0);
         break;
      }
  }
}

//This is called when the select button is pressed.
//Since this is an interrupt function, this will be called no matter what loop we are in
void selectInterrupt()
{
  static unsigned long last_interrupt_time = 0;
  unsigned long interrupt_time = millis();
  // If interrupts come faster than 200ms, assume it's a bounce and ignore
  if (interrupt_time - last_interrupt_time > 200) 
  {
     patternChanged = true;
  }
  last_interrupt_time = interrupt_time;
}

//This returns true when a new pattern has been selected
boolean newPatternSelected()
{
   // For now, read the button. Later make this a flag set by interrupt
   boolean result = patternChanged;
   patternChanged = false;
   
   return result;
}

//Reads a button and returns its value
boolean readButton(int buttonPin)
{
   // The inputs are tied high through a pullup resistor, and pulled low when the button is pressed.
   // As a result, we need to return the inverse of what we read for normal logic to exist outside of this
   // without causing confusion throughout the code.
   return !digitalRead(buttonPin);
}

//This prints the name of the pattern that is currently being selected via the options menu
//This is not the function for showing the pattern that is running
void showSelection()
{
   lcd.setCursor(0, 0);
   lcd.print(getPatternName(pattern));
}

//This prints the name of the pattern that is currently running on the signs
void showSelected()
{
   lcd.setCursor(0, 1);
   //This prints an "arrow" character which helps to differentiate the lines
   lcd.print("~");
   lcd.setCursor(1, 1);
   lcd.print(getPatternName(pattern));
}

//Returns the pattern name based on its integer
String getPatternName(byte pattern)
{
   switch (pattern)
   {
      case PATTERN_BLANK:
         return BLANK_TEXT;
      case PATTERN_RAINBOW:
         return RAINBOW_TEXT;
      case PATTERN_RED_FILL:
         return RED_FILL_TEXT;
      case PATTERN_BLUE_FILL:
         return BLUE_FILL_TEXT;
      case PATTERN_RED_FILL_SHIMMER:
         return RED_FILL_SHIMMER_TEXT;
      case PATTERN_BLUE_FILL_SHIMMER:
         return BLUE_FILL_SHIMMER_TEXT;
      case PATTERN_RED_FILL_TILT:
         return RED_FILL_TILT_TEXT;
      case PATTERN_BLUE_FILL_TILT:
         return BLUE_FILL_TILT_TEXT;
      case PATTERN_TWINKLE:
         return TWINKLE_TEXT;
      case PATTERN_EQ_METER:
         return EQ_METER_TEXT;
      default:
         return "None            ";
         break;
   }
}

//This is called when the "up" button is pressed
void upInterrupt()
{
  static unsigned long last_interrupt_time = 0;
  unsigned long interrupt_time = millis();
  // If interrupts come faster than 200ms, assume it's a bounce and ignore
  if (interrupt_time - last_interrupt_time > 200) 
  {
     pattern++;
     if (pattern > PATTERN_MAX)
     {
        pattern = 0;
     }
     patternSelectionChanged = true;
  }
  last_interrupt_time = interrupt_time;
}

//This is called when the "down" button is pressed
void downInterrupt()
{
  static unsigned long last_interrupt_time = 0;
  unsigned long interrupt_time = millis();
  // If interrupts come faster than 200ms, assume it's a bounce and ignore
  if (interrupt_time - last_interrupt_time > 200) 
  {
     pattern--;
     if (pattern < 0)
     {
        pattern = PATTERN_MAX;
     }
     patternSelectionChanged = true;
  }
  last_interrupt_time = interrupt_time;
}

//A simple function to return if a new pattern has been "selected" for the menu
boolean isPatternSelectionChanged()
{
   return patternSelectionChanged;
}

//Resets patternSelectionChanged to show that the pattern name as now been sent to the LCD
void clearPatternSelectionChanged()
{
   patternSelectionChanged = false;
}

//void encoderInterrupt()
//{
//   byte A = digitalRead(ENCODER_A);
//   byte B = digitalRead(ENCODER_B);
//   
//   byte delta = 0;
//   byte sequence = (A ^ B) | B << 1;
//   
//   delta = (sequence - lastSequence) % 4;
//   
//   if (delta == 3)
//   {
//      delta = -1;
//   }
//   else if (delta == 2)
//   {
//      // Assume same direction as last
//      delta = delta * sign;
//   }
//   lastSequence = sequence;
//
//   if (A && B)
//   {
//   if (delta > 0)
//   {
//      sign = 1;
//   }
//   else
//   {
//      sign = -1;
//   }
//   
//   
//      pattern += sign;
//   }
//}

//Calls the sendPatternMessage function to actually send the pattern command
//over the I2C lines
//void sendPattern(int sign, int pattern)
//{
//  if (sign == SIGN_ALL)
//  {
//    sendPatternMessage(SIGN_1, pattern);
//    sendPatternMessage(SIGN_2, pattern);
//    sendPatternMessage(SIGN_3, pattern);
//  }
//  else
//  {
//    sendPatternMessage(sign, pattern);
//  }
//}
//Calls the sendPatternMessage function to actually send the pattern command
//over the I2C lines

void sendPattern(int sign, int pattern, int data[], int start,  int length)
{
  if (sign == SIGN_ALL)
  {
    sendPatternMessage(SIGN_1, pattern, data, start, length);
    sendPatternMessage(SIGN_2, pattern, data, start, length);
    sendPatternMessage(SIGN_3, pattern, data, start, length);
  }
  else
  {
    sendPatternMessage(sign, pattern, data, start, length);
  }
}
//This function takes the address and pattern command data in and sends it out over I2C
void sendPatternMessage(int address, int pattern, int data[], int start, int length)
{
   
  Wire.beginTransmission(address);
  Wire.write(pattern);
  if (data != NULL)
  {
     for (int i = start; i < length; i++)
     {
        Wire.write(data[i]);
     }
  }
  Wire.endTransmission();
}

//Checks if the chosen pattern has been changed and returns it
boolean hasPatternChanged()
{
   return patternChanged;
}

//Sets the new pattern and changes the patternChanged flag to reflect this update
void setPattern(int newPattern)
{
   pattern = newPattern;
   patternChanged = true;
}

//This function mimmics the default delay function.
//Delay should not be used as this function allows I2C bytes to be recieved while we are waiting in a loop
boolean timedWait(unsigned int waitTime)
{
  unsigned long previousMillis = millis();
  unsigned long currentMillis = millis();
  for(previousMillis; (currentMillis - previousMillis) < waitTime; currentMillis = millis())
  {
    if (patternChanged == true)
    {
      return true;
    }
  }
  return false;
}

//Takes in a RGB level and sets the LCD backlight to match these values
void setDisplayColor(int red, int green, int blue)
{
   analogWrite(LCD_RED, 255 - red);
   analogWrite(LCD_GREEN, 255 - green);
   analogWrite(LCD_BLUE, 255 - blue);
}

//boolean accelOverThreshold(short angle) 
//{
//   float xytilt = 0 ;
//
//   int x = accel.readXData();
//   int y = accel.readYData();
//   int z = accel.readZData();
//
//   xytilt = atan ((float)x/(float)y) * 180 / 3.14159;
//
//   if (xytilt > angle || xytilt < (0 - angle))
//   {
//      return true;
//   }
//   else
//   {
//      return false;
//   }
//}

void Wheel(uint16_t WheelPos)
{
  switch(WheelPos / 256)
  {
    case 0:
      r = 255 - WheelPos % 256;   //Red down
      g = WheelPos % 256;      // Green up
      b = 0;                  //blue off
      break; 
    case 1:
      g = 255 - WheelPos % 256;  //green down
      b = WheelPos % 256;      //blue up
      r = 0;                  //red off
      break; 
    case 2:
      b = 255 - WheelPos % 256;  //blue down 
      r = WheelPos % 256;      //red up
      g = 0;                  //green off
      break; 
  }
}

void readSoundData()
{
   digitalWrite(MIC_RESET, HIGH); 
   digitalWrite(MIC_RESET, LOW);
   totalVolume = 0;

   for (int i=0; i < 7; i++)
   {
     digitalWrite(MIC_STROBE, LOW);
     delayMicroseconds(30);  // to allow the output to settle
     spectrumValue[i] = analogRead(MIC_IN) - baseline[i];
     if (spectrumValue[i] < 0)
     {
       spectrumValue[i] = 0;
     }

     totalVolume += spectrumValue[i];

     digitalWrite(MIC_STROBE, HIGH);
   }

   // Average the volume
   averageVolume = totalVolume / 7;

   relativeLevel = averageVolume * SENSITIVITY / 100;

   if (relativeLevel > 10)
   {
     relativeLevel = 10;
   }
   else if (relativeLevel < 0)
   {
     relativeLevel = 0;
   }
}

