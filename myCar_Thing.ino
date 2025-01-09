//
; // DO NOT DELETE. REQUIRED TO COMPILE CORRECTLY WITH #define and #if
//
#define AWS 0
#define USEVT100 0
#define USENEXT 0
#define WRITE_EEPROM 0
#define READ_EEPROM 0
//****************************************************************************
//
//                 Libraries from Library Manager
//
//****************************************************************************
#include <Wire.h>
#include <EEPROM.h>   

#if AWS == 1
    #include <WiFi.h>
    #include <WiFiClientSecure.h>
    #include <MQTTClient.h>                  // by Andreas Motzek
#endif

#include <SparkFunCCS811.h>
#include <SparkFunBME280.h>
#include <SparkFun_Ublox_Arduino_Library.h>
#include <SparkFun_RV8803.h>
#include <SparkFun_Qwiic_Button.h>

#if USEVT100 == 1
    #include <VT100.h>                       // by Kai Liebich
#endif
//****************************************************************************
//
//                         CUSTOM LIBARIES
//
//****************************************************************************
#include "t_ISO8601DateTime.h"
#include "t_TMR.h"
#include "myCarLib.h"

#if AWS == 1
    #include "WiFiNet.h"
    #include "AWS_IoT.h"
#endif

#if USENEXT == 1
    #include <EasyNextionLibrary.h>
#endif
//****************************************************************************
#define CCS811_ADDR 0x5B        // Default I2C Address
#define PIN_RESET 9  
#define DC_JUMPER 1
#define BAROMETER_QUEUE_SIZE 24 // Readings every 5 minutes for 2 hours
//****************************************************************************
const int BLU_LED = 13;
const int BTN_PIN = 0;

#if USENEXT == 1
    EasyNex myNext(Serial1);
    const int nextBLK = 0;
    const int nextBLU = 31;
    const int nextGRN = 2016;
    const int nextGRY = 33840;
    const int nextBRN = 48192;
    const int nextYEL = 65504;
    const int nextRED = 63488;
    const int nextWHT = 65535;
#endif

boolean firstScan  = true;      // A hold over from my PLC programming days.
int     heartbeat  = 0;
float   barometerTrendData[BAROMETER_QUEUE_SIZE];

t_Data myCarData;

t_TMR oneSecTMR;
t_TMR twoSecTMR;
t_TMR fiveSecTMR;
t_TMR fiveMinTMR;

t_ISO8601DateTime utcDateTime;
t_ISO8601DateTime localDateTime;

BME280 mySensor;                 // Humidity, Temperature, and
                                 // Barometric Pressure
CCS811 myCO2Sensor(CCS811_ADDR); // Equivalent CO2 (eCO2) and Total
                                 // Volatile Organic Compounds (TVOC)
SFE_UBLOX_GPS myGPS;

RV8803 myRTC;

QwiicButton myButton;

#if AWS == 1
    WiFiClientSecure   WiFiNet = WiFiClientSecure();
    MQTTClient       awsClient = MQTTClient(128);
#endif
//****************************************************************************
void setup(){
  
    oneSecTMR.setSec(1);
    twoSecTMR.setSec(2);
    fiveSecTMR.setSec(5);
    fiveMinTMR.setMin(5);
    
    pinMode(18,INPUT);

#if USEVT100 == 1
    Serial.begin(115200);
    VT100.begin(Serial);
    delay(500);
#endif

    pinMode(BLU_LED, OUTPUT);
    digitalWrite(BLU_LED, LOW);

    Wire.begin();
    
    mySensor.settings.commInterface = I2C_MODE;
    mySensor.settings.I2CAddress    = 0x77;
    mySensor.begin();
    delay(250);
    
    myCO2Sensor.begin();
    delay(250);

    while(!myGPS.begin());
    myGPS.setI2COutput(COM_TYPE_UBX);
    myGPS.setNavigationFrequency(4);
    myGPS.getPVT(300);
    myGPS.saveConfiguration();    
    delay(250);
 
    utcDateTime.setTZOffsetHR(0);
    utcDateTime.setTZOffsetMIN(0);
 
    localDateTime.setTZOffsetHR(-6);
    localDateTime.setTZOffsetMIN(0);


    EEPROM.begin(5);
    delay(250);

  #if WRITE_EEPROM == 1
        EEPROM.write(0,0);  EEPROM.commit();  // SYS0
        EEPROM.write(1,0);  EEPROM.commit();  // SYS1
        EEPROM.write(2,0);  EEPROM.commit();  // SYS2
        EEPROM.write(3,-6); EEPROM.commit();  // TZ Hour
        EEPROM.write(4,0);  EEPROM.commit();  // TZ Minute
    #endif

   #if READ_EEPROM == 1
       localDateTime.setTZOffsetHR(int8_t(EEPROM.read(3)));
       localDateTime.setTZOffsetMIN(int8_t(EEPROM.read(4)));
   #endif

    #if USENEXT == 1
        myNext.writeNum("time.nTZHour.val", localDateTime.getTZOffsetHR());
        myNext.writeNum("time.nTZMin.val",  localDateTime.getTZOffsetMIN());
    #endif
}
//****************************************************************************
void loop(){

#if USENEXT == 1
    checkNextVarChange();
    myNext.NextionListen();
#endif

#if AWS == 1
    if (publishAWS){
        awsClient.loop();
    }
#endif

    if(firstScan){
        doOneSecondTasks();
        doTwoSecondTasks();
        doFiveSecondTasks();
        doFiveMinuteTasks();
        
        #if USEVT100 == 1
            sendHeaderToVT100(); 
            sendValuesToVT100();
        #endif

        oneSecTMR.restart();
        twoSecTMR.restart();
        fiveSecTMR.restart();
        fiveMinTMR.restart();
    }
    if(oneSecTMR.isExpired()){
       oneSecTMR.restart();
       doOneSecondTasks();
    }
    if(twoSecTMR.isExpired()){
       twoSecTMR.restart();
       doTwoSecondTasks();
       
        #if USEVT100 == 1
            sendHeaderToVT100(); 
            sendValuesToVT100();
        #endif
    }

    if(fiveSecTMR.isExpired()){
        fiveSecTMR.restart();
        doFiveSecondTasks();
    }
    
    if(fiveMinTMR.isExpired()) {
      fiveMinTMR.restart();
      doFiveMinuteTasks();
      heartbeat++;
    }

    firstScan = false;
    
} // END MAIN LOOP
//****************************************************************************
void doOneSecondTasks(){

    int hz = millis();
  
    myCarData.headingDeg    = myGPS.getHeading()  / 1e5;

    //Serial.println("1sec>" + String(millis()-hz) + "ms");

    //myNext.writeStr("tHDR.txt", getHeadingStr(myCarData.headingDeg));
    
    myCarData.speedKPH      = myGPS.getGroundSpeed() / d_mms2kph;   // mm per sec to KPH
    myCarData.speedMPH      = myCarData.speedKPH     / d_kph2mph;   // KPH to MPH
    myCarData.speedMPH      = myCarData.speedKTS     / m_kph2kts;   // KPH to KTS
   // myNext.writeStr("tSPD.txt", String(myCarData.speedMPH,1));
}
//****************************************************************************
void doTwoSecondTasks(){
  
    int hz = millis();
   
    utcDateTime.setDate(myGPS.getYear(),myGPS.getMonth(),myGPS.getDay());
    utcDateTime.setTime(myGPS.getHour(),myGPS.getMinute(),myGPS.getSecond());
    
//    myNext.writeStr("tUTC.txt", 
//                      String(twoDigitInt(utcDateTime.getHour())   + ":" +
//                             twoDigitInt(utcDateTime.getMinute()) + "Z" + "    " +
//                             twoDigitInt(utcDateTime.getDay())    + "-" +
//                             Month2Str(utcDateTime.getMonth())    + "-" + 
//                            utcDateTime.getYear()));
    localDateTime.setDate(utcDateTime.getYear(),
                          utcDateTime.getMonth(),
                          utcDateTime.getDay());
    localDateTime.setTime(utcDateTime.getHour(),
                          utcDateTime.getMinute(),
                          utcDateTime.getSecond());
                          
    localDateTime.applyTZ();
    
//    myNext.writeStr("tTime.txt",
//                    String(twoDigitInt(localDateTime.getHour()) + ":" +
//                           twoDigitInt(localDateTime.getMinute()))); 
//    myNext.writeStr("tDate.txt",
//                    String(twoDigitInt(localDateTime.getDay()) + "-" +
//                    Month2Str(localDateTime.getMonth())        + "-" + 
//                    localDateTime.getYear()));
     
//    myNext.writeNum("nTZHour.val",  localDateTime.getTZOffsetHR());
//    myNext.writeNum("nTZMin.val" ,  localDateTime.getTZOffsetMIN()); 
    
    myCarData.altMSLmtrs    = myGPS.getAltitudeMSL() / 1e3;
    myCarData.altMSLfeet    = myCarData.altMSLmtrs   * m_mtrs2ft; 
//    myNext.writeStr("tALT.txt", String(myCarData.altMSLfeet,0));

    myCarData.pDOP          = myGPS.getPDOP()        / 100;
    myCarData.numSats       = myGPS.getSIV();

//    myNext.writeStr("tNumSats.txt", String(myCarData.numSats));
//    myNext.writeStr("tPDOP.txt"   , String(myCarData.pDOP,1));

    myGPS.getVehAtt();
    if (myGPS.imuMeas.fusionMode == 1){
        
        myCarData.pitch         = myGPS.vehAtt.pitch     / 1e5;
        myCarData.roll          = myGPS.vehAtt.roll      / 1e5;

//        myNext.writeStr("tPitch.txt"  , String(myCarData.pitch,1));
//        myNext.writeStr("tRoll.txt"   , String(myCarData.roll,1));

    } else {
//        myNext.writeStr("tPitch.txt"  , "99.9");
//        myNext.writeStr("tRoll.txt"   , "99.9");   
     }

    //Serial.println("2sec>" + String(millis()-hz) + "ms");

}
//****************************************************************************
void doFiveSecondTasks(){
  
    int hz = millis();
    int onTime,offTime;
    float freq;
   
    onTime  = pulseIn(18,HIGH);
    offTime = pulseIn(18,LOW);
    freq = 1000000/(onTime+offTime);
  
//    myNext.writeNum("nLightLevel.val",freq);

    for(int n = 4; n > 0; n--)
        myCarData.lightLevel[n] = myCarData.lightLevel[n-1];
    myCarData.lightLevel[0] = freq;
    freq = 0.0;
    for(int n = 0; n < 5; n++)
        freq += myCarData.lightLevel[n];
    
    freq = freq / 5;
    
//    myNext.writeNum("nLLAvg.val",int(freq));

    myCarData.tempDegC      = mySensor.readTempC();    
    myCarData.tempDegF      = mySensor.readTempF();
//    myNext.writeStr("tTMP.txt",     String(myCarData.tempDegF,1));  

    myCarData.humidityPct   = mySensor.readFloatHumidity();
    myCarData.pressure_hPa  = mySensor.readFloatPressure() / 100;
    myCarData.pressure_inHg = myCarData.pressure_hPa / d_hPa2inHg;
   
    myCarData.dewPoint_degC = calcDewPointDegC(myCarData.tempDegC, 
                                                  myCarData.humidityPct);
    myCarData.dewPoint_degF = degC2degF(myCarData.dewPoint_degC);

    myCarData.pressure_SL_hPa = 
                           calcSeaLevelPress_hPa(myCarData.pressure_hPa,
                           myCarData.altMSLmtrs);
    myCarData.pressure_SL_inHg = myCarData.pressure_SL_hPa / d_hPa2inHg;
    myCarData.pCorrected_hPa   = 
                          calcCorrectedPress_hPa(myCarData.pressure_hPa,
                          myCarData.pressure_SL_hPa,
                          myCarData.altMSLmtrs,
                          myCarData.tempDegC);
    myCarData.pCorrected_inHg = myCarData.pCorrected_hPa / d_hPa2inHg;

//    if(firstScan){
//        for(int z = 0; z < BAROMETER_QUEUE_SIZE; z++)
//            barometerTrendData[z]= myCarData.pCorrected_inHg;   
//
//    myNext.writeStr("tHMD.txt",     String(myCarData.humidityPct,1));
//    myNext.writeStr("tPRS.txt",     String(myCarData.pCorrected_inHg,2));
//    myNext.writeStr("tPressSL.txt", String(myCarData.pressure_SL_inHg,2)); 
//    myNext.writeStr("tDewPt.txt",   String(myCarData.dewPoint_degF,1));
    
    myCarData.latitude      = myGPS.getLatitude()    / 1e7;
    myCarData.longitude     = myGPS.getLongitude()   / 1e7;
    
//    myNext.writeStr("tLAT.txt",     String(myCarData.latitude,5));
//    myNext.writeStr("tLONG.txt",    String(myCarData.longitude,5));
    
//    myNext.writeNum("time.nTZHour.val", localDateTime.getTZOffsetHR());
//    myNext.writeNum("time.nTZMin.val",  localDateTime.getTZOffsetMIN());
//    myNext.writeStr("tORD.txt",   String(localDateTime.getOrdDay()));

    //myNext.writeStr("tDSTstrt.txt", localDateTime.getDSTStartStr());
    //myNext.writeStr("tDSTend.txt" , localDateTime.getDSTEndStr());
   
//    if (localDateTime.isDST())
//       myNext.writeStr("tDST.txt","YES");   
//    else
//       myNext.writeStr("tDST.txt","NO");

    myGPS.getEsfInfo(250);
        
    //Serial.println("5Sec>" + String(millis()-hz) + "ms\n");
}
//****************************************************************************
void doFiveMinuteTasks(){

    float barometerAvg = 0.0;  
       
//    for(int z = 1; z < BAROMETER_QUEUE_SIZE; z++)
//         barometerTrendData.enqueue(myCarData.pressure_inHg);   
}
//****************************************************************************
void checkNextVarChange(){
  
    int n = 0; //myNext.readNumber("time.nTZHour.val");
    delay(25);
    if(n != localDateTime.getTZOffsetHR()){
        //EEPROM.write(3,int8_t(n));
        //EEPROM.commit();
        //localDateTime.setTZOffsetHR(n);        
    }

//    n = myNext.readNumber("time.nTZMin.val");
    delay(25);
    if(n != localDateTime.getTZOffsetMIN()){
        //EEPROM.write(4,int8_t(n));
        //EEPROM.commit();
        //localDateTime.setTZOffsetMIN(n);         
    }
}
//****************************************************************************
void updateNext(){


}
//****************************************************************************
//
//  BEGIN LARGE BLOCK of CONDTIONAL COMPULATION
//
#if AWS == 1

//****************************************************************************
boolean connectToWiFi(){
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  if(WiFi.status() == WL_CONNECTED)
      return(true);
  else
      return(false);

  while (WiFi.status() != WL_CONNECTED && retries < 2)
  {
    delay(5000); // Delay between retries 30 seconds
    retries++;
  }
  if (WiFi.status() == WL_CONNECTED) 
  {
      digitalWrite(BLU_LED, HIGH);
      displayWiFi();
  }    
  // If WiFi connect fails after xx retries,
  // go to deep sleep for a xx minutes and try again.
  
  if(WiFi.status() != WL_CONNECTED){
    esp_sleep_enable_timer_wakeup(retryDelay * 60L * 1000000L);
    esp_deep_sleep_start();
  }

}
//****************************************************************************
boolean WiFiConnected()
{
    if(WiFi.status() == WL_CONNECTED)
        return(true);
    else
        return(false);   
}
//****************************************************************************
boolean connectToAWS(){
    WiFiNet.setCACert(AWS_CERT_CA);
    WiFiNet.setCertificate(AWS_CERT_CRT);
    WiFiNet.setPrivateKey(AWS_CERT_PRIVATE);

    awsClient.begin(AWS_IOT_ENDPOINT, 8883, WiFiNet);

    if(WiFi.status() == WL_CONNECTED)
      return(true);
    else
      return(false);    

  // Try to connect to AWS IOT. If the connection is not
  // sucessful try xx number of times.
  
    int retries = 0;

    while (!awsClient.connect(THING_NAME) && retries < 5) {
      delay(5000);
      retries++;
    }

    if(awsClient.connected())
        displayAWS();

}
//****************************************************************************

boolean AWSConnected(){
  
    if(awsClient.connected())
        return(true);
    else
        return(false); 
}
//****************************************************************************
void displayWiFi(){
  
//    myNext.writeNum("wInd.pco", nextWHT);
//    delay(25);
//    myNext.writeNum("wInd.bco", nextBLU);
//    delay(25);
}
//****************************************************************************
void displayAWS(){
  
//    myNext.writeNum("aInd.pco", nextBLK);
    delay(25);
//    myNext.writeNum("aInd.bco", nextGRN);
    delay(25);
}
//****************************************************************************
void publishDataToAWS(){
  
    awsClient.publish("myCar_Thing/heartbeat/value",String(heartbeat).c_str());
    
     awsClient.publish("myCar_Thing/Location/long", String(myCarData.longitude,7).c_str());
     awsClient.publish("myCarThing/Location/lat",  String(myCarData.latitude,7).c_str());
    
    awsClient.publish("myThing/DateTime/UTC/Date",utcDateTime.getDateStr().c_str());
    awsClient.publish("myThing/DateTime/UTC/Time",utcDateTime.getTimeStr().c_str());
       
    awsClient.publish("myThing/Altitude/feet", 
                         String(myCarData.altMSLfeet,1).c_str()); 
    awsClient.publish("myThing/Altitude/mtrs", 
                         String(myCarData.altMSLmtrs,1).c_str());     

    awsClient.publish("myThing/Temperature/degF", 
                         String(myCarData.tempDegF,1).c_str());
}
#endif

//
#if VT100 == 1
//
//*************************************************************************************************
void sendHeaderToVT100() {
     
     if (firstScan)
        VT100.reset();
       
     VT100.cursorOff();
     //
     // Display Title
     //
     if (firstScan) {
         VT100.setCursor(0,0);
         VT100.formatText  (VT_BRIGHT);
         VT100.setTextColor(VT_GREEN);
         Serial.print      ("myCar Thing");
         VT100.formatText  (VT_DIM);
         VT100.setTextColor(VT_WHITE);
     }
     //
     // Display GPS Status
     //
     VT100.setCursor(0,40);
     VT100.formatText(VT_BRIGHT);
     if(myCarData.numSats == 0) {
         VT100.setBackgroundColor(VT_RED);
         VT100.setTextColor(VT_WHITE);
         Serial.print("GPS");   
     }
     if(myCarData.numSats == 2) {
         VT100.setBackgroundColor(VT_YELLOW);
         VT100.setTextColor(VT_BLACK);
         Serial.print("GPS");
     }
     if(myCarData.numSats >= 3) {
         VT100.setBackgroundColor(VT_GREEN);
         VT100.setTextColor(VT_WHITE);
         Serial.print("GPS");
     }
     VT100.formatText(VT_DIM);
     VT100.setTextColor(VT_WHITE);
     VT100.setBackgroundColor(VT_BLACK);
     //
     // Display Date Time
     //     
     VT100.setCursor(0,61);
     VT100.formatText(VT_BRIGHT);
     Serial.print(localDateTime.getShortDateStr());
     Serial.print("-" + String(localDateTime.getYear()));
     VT100.setCursor(0,73);
     Serial.print(localDateTime.getShortTimeStr());
     VT100.formatText(VT_DIM);
}
//*************************************************************************************************
void sendValuesToVT100() {

    uint8_t startRow = 4;
    uint8_t startCol = 1;
    uint8_t colWidth = 18; 

    label2VT100(startRow, startCol, colWidth, "Temp degF");
    VT100.setCursor(startRow,(startCol + colWidth));
    Serial.print(float2Str(myCarData.tempDegF, 3,1));      

    startRow++;
    label2VT100(startRow, startCol, colWidth, "Temp degC"); 
    VT100.setCursor(startRow,(startCol + colWidth));
    Serial.print(float2Str(myCarData.tempDegC, 3,1)); 

    startRow++;
    label2VT100(startRow, startCol, colWidth, "Humidity %");     
    VT100.setCursor(startRow,(startCol + colWidth));
    Serial.print(float2Str(myCarData.humidityPct, 3,1)); 

    startRow++;
    label2VT100(startRow, startCol, colWidth, "dewPt degF");     
    VT100.setCursor(startRow,(startCol + colWidth));
    Serial.print(float2Str(myCarData.dewPoint_degF, 3,1)); 

    startRow++;
    label2VT100(startRow, startCol, colWidth, "dewPt degC");     
    VT100.setCursor(startRow,(startCol + colWidth));
    Serial.print(float2Str(myCarData.dewPoint_degC, 3,1)); 

    startRow++;
    label2VT100(startRow, startCol, colWidth, "Barometer inHg"); 
    VT100.setCursor(startRow,(startCol + colWidth));
    Serial.print(float2Str(myCarData.pCorrected_inHg, 4,2));

    startRow++;
    label2VT100(startRow, startCol, colWidth, "Barometer mbar"); 
    VT100.setCursor(startRow,(startCol + colWidth));
    Serial.print(float2Str(myCarData.pCorrected_hPa, 4,0));

    startRow++;
    label2VT100(startRow, startCol, colWidth, "Barometer Trnd"); 
    VT100.setCursor(startRow,(startCol + colWidth));
    Serial.print(steady);

    startRow++;
    label2VT100(startRow, startCol, colWidth, "P(raw)  inHg"); 
    VT100.setCursor(startRow,(startCol + colWidth));
    Serial.print(float2Str(myCarData.pressure_inHg, 4,2));

    startRow++;
    label2VT100(startRow, startCol, colWidth, "P(raw)  mbar"); 
    VT100.setCursor(startRow,(startCol + colWidth));
    Serial.print(float2Str(myCarData.pressure_hPa, 4,0));

    startRow++;
    label2VT100(startRow, startCol, colWidth, "P(cLvL) inHg");
    VT100.setCursor(startRow,(startCol + colWidth));
    Serial.print(float2Str(myCarData.pressure_SL_inHg, 4,2)); 

    startRow++;
    label2VT100(startRow, startCol, colWidth, "P(cLvL) mbar");
    VT100.setCursor(startRow,(startCol + colWidth));
    Serial.print(float2Str(myCarData.pressure_SL_hPa, 4,0));
    
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++  
    
    startRow = 4;
    startCol = 25;

    label2VT100(startRow, startCol, colWidth, "Latitude");     
    VT100.setCursor(startRow,(startCol + colWidth));
    if(myCarData.latitude > 0.0)
        Serial.print("N ");
    else
        Serial.print("S ");
    Serial.print(float2Str(abs(myCarData.latitude), 3,7)); 

    startRow++;
    label2VT100(startRow, startCol, colWidth, "Longitude");  
    VT100.setCursor(startRow,(startCol + colWidth));
    if(myCarData.longitude > 0.0)
        Serial.print("E ");
    else
        Serial.print("W ");
    Serial.print(float2Str(abs(myCarData.longitude), 3,7)); 

    startRow++;
    label2VT100(startRow, startCol, colWidth, "Heading"); 
    VT100.setCursor(startRow,(startCol + colWidth));    
    Serial.print(threeDigitInt(myCarData.headingDeg)); 

    startRow++;
    label2VT100(startRow, startCol, colWidth, "Speed MPH"); 
    VT100.setCursor(startRow,(startCol + colWidth));
    Serial.print(float2Str(myCarData.speedMPH, 3,1)); 

    startRow++;
    label2VT100(startRow, startCol, colWidth, "Speed KTS");
    VT100.setCursor(startRow,(startCol + colWidth));
    Serial.print(float2Str(myCarData.speedKTS, 3,1));

    startRow++;
    label2VT100(startRow, startCol, colWidth, "Speed KPH");
    VT100.setCursor(startRow,(startCol + colWidth));
    Serial.print(float2Str(myCarData.speedKPH, 3,1));

    startRow++;
    label2VT100(startRow, startCol, colWidth, "Alt GPS feet");
    VT100.setCursor(startRow,(startCol + colWidth));
    Serial.print(float2Str(myCarData.altMSLfeet, 3,1));    
    
    startRow++;
    label2VT100(startRow, startCol, colWidth, "Alt GPS mtrs");
    VT100.setCursor(startRow,(startCol + colWidth));
    Serial.print(float2Str(myCarData.altMSLmtrs, 3,1)); 

    startRow++;
    label2VT100(startRow, startCol, colWidth, "Num Sats");
    VT100.setCursor(startRow,(startCol + colWidth));
    Serial.print(float2Str(myCarData.numSats, 2,0)); 

    startRow++;
    label2VT100(startRow, startCol, colWidth, "pDOP");
    VT100.setCursor(startRow,(startCol + colWidth));
    Serial.print(float2Str(myCarData.pDOP, 3,1)); 
}
//*************************************************************************************************
void label2VT100(uint8_t _r, uint8_t _c, uint8_t _w, String _l){
    
    if(firstScan) {
        VT100.setCursor(_r, _c);
        Serial.print(rightJustStr(_l, _w));
    }
  
}
//
#endif
//
//*************************************************************************************************
String rightJustStr(String inStr, uint8_t len) {
     
    String  retVal   = "";
    uint8_t inStrLen = inStr.length();

    for(int z = 1; z <= (len - inStrLen - 3); z++) {
       retVal += " ";
    }

    return(retVal + inStr + " = ");
} 
