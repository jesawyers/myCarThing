#ifndef myCarLib_h
#define myCarLib_h
#include <Arduino.h>
#include "t_TMR.h"

#define upArrow "\u25b2" // Symbol For Barometer Trending Higher
#define dnArrow "\u25bc" // Symbol For Barometer Trending Lower
#define steady  "\u25ac" // Symbol For Barometer Steady

const double d_hPa2inHg = 33.863886666667;
const double d_ft2mtrs  = 3.280839895;
const double m_mtrs2ft  = 3.280839895;
const double d_mms2kph  = 278;
const double d_kph2mph  = 1.609;
const double m_kph2kts  = 0.5399565;

class t_Data {
    public:
    float  latitude         = 0;
    float  longitude        = 0; 
    float  speedMPH         = 0;
    float  speedKPH         = 0;
    float  speedKTS         = 0;
    float  pitch            = 0;
    float  roll             = 0;
    int    headingDeg       = 0;  
    float  tempDegC         = 0; 
    float  tempDegF         = 0;
    float  humidityPct      = 0;
    float  pressure_hPa     = 0;
    float  pressure_inHg    = 0;
    float  dewPoint_degC    = 0;
    float  dewPoint_degF    = 0;
    float  pressure_SL_hPa  = 0;
    float  pressure_SL_inHg = 0;
    float  pCorrected_hPa   = 0;
    float  pCorrected_inHg  = 0;
    byte   pressureTrend    = 0;
    float  altMSLmtrs       = 0;  
    double altMSLfeet       = 0;
    byte      fixType       = 0;
    uint8_t   numSats       = 0;
    float     pDOP          = 0;
    float     lightLevel[5] = {15625,15625,15625,15625,15625};
};
//********************************************************************************
double degC2degF(double _in) {
     
     return((_in * (9.0/5.0)) + 32.0);  
     
} // END Function
//********************************************************************************
double calcDewPointDegC(double _tempC, double _humid) 
{
     // This equation is an approximation of dew point within 1 degC
 
     return((_tempC / 100)  * ((100.0 - _humid)/5));
}  
//********************************************************************************
double calcSeaLevelPress_hPa(double inP_hPa, double inAlt_mtrs) 
{
   double calc = 0.0;

   calc = pow((1 - (inAlt_mtrs / 44330)), 5.255);
   
   return(inP_hPa / calc);
} 
//********************************************************************************
double calcCorrectedPress_hPa(double inP_hPa, double inP_SL, double inAlt_mtrs, double inDegC) 
{
   double calc = 0.0;

   calc = (0.0065 * inAlt_mtrs) / (inDegC + (0.0065 * inAlt_mtrs) + 273.15);
   calc = 1 - calc;
   return(inP_hPa * pow(calc,-5.257));
}
//********************************************************************************
String twoDigitInt(int input)
{
    if (input < 10) 
    {
         return (("0" + String(input)));   
    } 
    else 
    {
         return (String(input));
    }  
} 
//********************************************************************************
String threeDigitInt(int input){
    if (input < 10) 
         return (("00" + String(input)));  

    if ((input >= 10) && (input < 100))
         return (("0" + String(input))); 
             
    return (String(input)); 
}
//********************************************************************************
String Month2Str(unsigned int monthNum){
      switch (monthNum) 
      {
          case 1 : return ("JAN");
                   break;
          case 2 : return ("FEB");
                   break;         
          case 3 : return ("MAR");
                   break;
          case 4 : return ("APR");
                   break;                  
          case 5 : return ("MAY");
                   break;
          case 6 : return ("JUN");
                   break;         
          case 7 : return ("JUL");
                   break;
          case 8 : return ("AUG");
                   break;                        
          case 9 : return ("SEP");
                   break;
          case 10 : return ("OCT");
                   break;         
          case 11 : return ("NOV");
                    break;
          case 12 : return ("DEC");
                    break;                      
          default : return ("err");
     }       
}
//********************************************************************************
String getHeadingStr(int input){
    String retVal = threeDigitInt(input);

    // Check for close to 000
    if (abs(0 - input) <= 2)
        retVal = "-N-";

    // Check for close to 045
    if (abs(45 - input) <= 2)
        retVal = "-NE-";
        
    // Check for close to 090
    if (abs(90 - input) <= 2)
        retVal = "-E-";

    // Check for close to 135
    if (abs(135 - input) <= 2)
        retVal = "-SE-";

    // Check for close to 180
    if (abs(180 - input) <= 2)
        retVal = "-S-";

    // Check for close to 225
    if (abs(225 - input) <= 2)
        retVal = "-SW-";

    // Check for close to 270
    if (abs(270 - input) <= 2)
        retVal = "-W-";

    // Check for close to 315
    if (abs(315 - input) <= 2)
        retVal = "-NW-";

    return(retVal);
}
//*************************************************************************************************
char *float2Str(float floatvar, signed char len, unsigned char dp ) {

     static char dtostrfbuffer[15];
    
     dtostrf(floatvar,len, dp, dtostrfbuffer);
     
     return (dtostrfbuffer);
     
}  // END Function
#endif
