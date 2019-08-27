#include <ESP8266WiFi.h>
//#define HOSTNAME "ControlBoard" // useless
#define MAX_SRV_CLIENTS 1
#define MAX_Buff_Length 5
//#define LED_BUILTIN 16

/***********************************************
 * 2019.07.13 A template of TCP server Station 
 * 
 * use bool UseNetWorkFunciton to start net work function, if this is false then neither softAp or TCP server going to work
 * use bool UsingSoftAP to start ap mode or connect to a wifi
 * use bool tcpMessageDebug to debug message format
 * the message format should be (header)content;(header)content;[CR][LF]
 * 
 * use method HeaderAnalysis() to recognize header label
 * use method ContentAnalysis() to read command after header
 * use method CommandApplyAtEnd() to give command after message end
 * use method CleanCommandTemp() to clean command cache after apply commands
 *************************************************/

typedef struct
{
  String result;
  bool isEnd;
  bool err;
}ProcessContentResult;
ProcessContentResult PCResult;

typedef struct
{
  byte result;
  bool isEnd;
  bool err;
}ProcessEndingResult;
ProcessEndingResult PEResult;

// is not going to use net working function for debug run just turn off this value 
bool UseNetWorkFunciton = true;
// if using soft ap then set this value to true, otherwrise set to false
bool UsingSoftAP = false;

const char* ssid = "FogponicControlStation";
const char* password = "qwertyui";

IPAddress local_IP(50,0,0,50); 
IPAddress gateway(50,0,0,1);
IPAddress subnet(255,255,255,0);

WiFiServer server(23);
WiFiClient serverClients[MAX_SRV_CLIENTS];
/*
const char* testServerIP = "10.0.0.100";
int testPort = 23;
WiFiClient client;
bool bConnected = false;
*/

// for state machine, 'const' make the variable read only
const uint8_t sm_pi_begin = -10;
uint8_t sm_pi_state = sm_pi_begin; //  sate machine for process incomming data
const uint8_t sm_pi_headerAnalyse = 0; // state machine processing incoming data, analyse the header
const uint8_t sm_pi_clearBeforeGoBack = 1; // state machine processing incoming data, analyse the header
const uint8_t sm_pi_error = 5;
const uint8_t sm_pi_contentAnalyse = 10; // analyse content after header
const uint8_t sm_pi_workingFine = 11;
const uint8_t sm_pi_fullEnding = 20;
const uint8_t sm_pi_EndingStage_0 = 21;
const uint8_t sm_pi_EndingStage_1 = 22;


char pi_buff[MAX_Buff_Length];
uint8_t pi_bufferCount = 0;

int speedCommand = 0;  // can not use uint8_t, because will get incorrect number when trying to return negative uint8_t value from function

bool fogTimerRollover = false;
unsigned long FogGeneratorTimer = 0;
unsigned long FogGeneratorTimerCache = 0;
bool serialMessageTimerRollover = false;
unsigned long SerialMessagePrintTimerCache = 0;
bool ledOffTimerRollover = false;
unsigned long ledOffTimerCache = 0;  // use for led control
bool ledOnTimerRollover = false;
unsigned long ledOnTimerCache = 0;

///*
uint8_t FogGeneratorPin = 0;  // pin 0, ESPDuino ESP-13
uint8_t FanPin = 2;  // pin 2
uint8_t TemperatureReadPin = 4;
//*/
/*
uint8_t lMotorPin = 5;  // pin 1, NodeMCU ESP-12E
uint8_t rMotorPin = 4;  // pin 2
uint8_t mMotorPin = 0;  // pin 3
*/
bool valueInDefultFlag = 0;  // set this value in 1 if machine restart

// debug mode define
bool tcpMessageDebug = true;

//-----------------

void setup() {
  // initiallize values
  
  valueInDefultFlag = 1;
  // end of initiallize value
  
  Serial.begin(115200); // initiallize serial for debug
  //Serial.setDebugOutput(true);  // dont know why when this enabled it will constantly give can not connect to wifi
  
  if (UseNetWorkFunciton)
  {  
    Serial.print("Initialize network...");
    if (UsingSoftAP)
    {
      UseSoftAP();
    }
    else
    {
      ConnectToExistentWIFI();
    }

    Serial.print("Initialize TCP server...");
    server.begin(); // start to run TCP server
    server.setNoDelay(true);
    uint8_t serverStatus = server.status();
    if (serverStatus == 1) Serial.println("Done.");
    else
    {
      Serial.println("Fail.");
      Serial.print("server statue: ");
      Serial.println(serverStatus);
    }
  }
  else
  {// if is not using wifi function at all
    Serial.println("Skip network and TCP server initialization.");
    WiFi.mode(WIFI_OFF);  // turn off wifi mode
    // maybe deep sleep is better? did not test, but this works for me
  }

  Serial.println("Initialize Pin and LED...");
  
  pinMode(LED_BUILTIN, OUTPUT); // initiallize LED
  digitalWrite(LED_BUILTIN, HIGH); // in esp13, high means off

  pinMode(FogGeneratorPin, OUTPUT); // initialize pin out is necessary, althought don't initialize still can use pwm function but that require pwm out put low and high to work
  pinMode(FanPin, OUTPUT); // if no initialization and directly use pwm to control will not get correct value
  //pinMode(TemperatureReadPin, OUTPUT);

  
  Serial.println("All initialization Done. Start Running...");
}

void loop() {

  LEDManager(); // manage LED
  
  if (UseNetWorkFunciton)
  {
    TCPClientsManagement(); // manage clients and process incoming messages
  }

  MessageToBePrintToSerialConstantly();
  
  //DebugAndTest ();
}

void TCPClientsManagement ()
{
  uint8_t i;  // "uint8_t" its shorthand for: a type of unsigned integer of length 8 bits
  
  // if client is connected in
  if (server.hasClient())
  {
    // Looking for new client connection
    for(i = 0; i < MAX_SRV_CLIENTS; i++)
    {
      if (!serverClients[i] || !serverClients[i].connected())
      {
        if(serverClients[i]) serverClients[i].stop();
        serverClients[i] = server.available();
        Serial.print("New client: "); Serial.println(i);
        continue;
      }
    }
    WiFiClient serverClient = server.available();
    serverClient.stop();  // why immediately close client?
  }

  // check process incoming message, look if its correct format
  // correct format, should be: (Header)Content;[CR][LF]
  for(i = 0; i < MAX_SRV_CLIENTS; i++)
  {
    if (serverClients[i] && serverClients[i].connected())
    {
      Socket_Communication (i); // monitor cache for incoming data and process it 
    }
  }
}

void ConnectToExistentWIFI()
{
  WiFi.config(local_IP, gateway, subnet); // static IP address
  WiFi.hostname("MyESPName");

  Serial.print("Start to connect to wifi: ");
  Serial.println(ssid);
  //WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password); // start connect router
  
  Serial.println("\nConnecting to router.");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println(WiFi.status() == WL_CONNECTED? "Ready" : "Failed!");
}

void UseSoftAP()
{
  Serial.print("Setting soft-AP configuration ... ");
  Serial.println(WiFi.softAPConfig(local_IP, gateway, subnet) ? "Ready" : "Failed!");
  Serial.print("Setting soft-AP: '");
  Serial.print(ssid);
  Serial.print("'...");
  boolean result = WiFi.softAP(ssid, password);
  if(result == true)
  {
    Serial.println("Ready");
  }
  else
  {
    Serial.println("Failed!");
  }

  Serial.print("Soft-AP IP address = ");
  Serial.println(WiFi.softAPIP());
}

void LEDManager ()
{
  unsigned int offResetTime = 1900; // the time for led to turn off
  unsigned int onResetTime = 100;  // the time for led to turn on
  
  digitalWrite(LED_BUILTIN, HIGH); // turn off LED
  if (ledOffTimerCache == 0) ledOffTimerCache = millis(); // start countting led turnning off time
  if (millis() - ledOffTimerCache < 0)  // rollover preventive, if rollover
  { 
    ledOffTimerCache = 4294967295 - ledOffTimerCache; // calculate already passed time
    ledOffTimerRollover = true;
  }
  if (TimeCounter (ledOffTimerCache, offResetTime, ledOffTimerRollover))  // off time reached
  {
    digitalWrite(LED_BUILTIN, LOW); // turn on LED
    if (ledOnTimerCache == 0)  ledOnTimerCache = millis();  // start countting led turnning on time
    if (millis() - ledOnTimerCache < 0)
    { 
      ledOnTimerCache = 4294967295 - ledOnTimerCache;
      ledOnTimerRollover = true;
    }
    if (TimeCounter (ledOnTimerCache, onResetTime, ledOnTimerRollover))  // on time reached
    {
      ledOnTimerCache = ledOffTimerCache = 0; // reset timer
      ledOnTimerRollover = ledOffTimerRollover = false;
    }
  }
}

void Socket_Communication (int _clientNum)
{
  while(serverClients[_clientNum].available()) // loop when there are data coming
  {
    char _num;
    switch (sm_pi_state)
    {
      case sm_pi_begin:
        _num = serverClients[_clientNum].read();
        if (_num == '(')  sm_pi_state = sm_pi_headerAnalyse;
        else if (_num == '[') sm_pi_state = sm_pi_fullEnding;
        else
        {
          sm_pi_state = sm_pi_error;  // uncorrenct format data
          Serial.print("PI State Machine error: sm_pi_state");
        }
        //Serial.print("state change: "); // keep this code for future debug
        //Serial.println(sm_pi_state);
      break;
      
      case sm_pi_headerAnalyse: // analyse the header to understander what command comes
        ProcessIncoming_ContentExtract (_clientNum);
        if (PCResult.isEnd && !PEResult.isEnd && !PCResult.err) // when tacking header should not receive ending mark
        {
          // extract header from incoming message
          if (HeaderAnalysis(PCResult.result) != sm_pi_error) sm_pi_state = sm_pi_contentAnalyse;
          else sm_pi_state = sm_pi_error;
        }
        else if (PCResult.err || PEResult.isEnd)
        {
          sm_pi_state = sm_pi_error;
          Serial.println("PI State Machine error: sm_pi_headerAnalyse - 01.");
        }
      break;

      case sm_pi_contentAnalyse: // analyse the content after header to understander what is the command
        ProcessIncoming_ContentExtract (_clientNum);
        if (PCResult.isEnd && !PEResult.isEnd && !PCResult.err) // when tacking header should not receive ending mark
        {
          // extract content from incoming message then go back to beginning for next command or ending
          if (ContentAnalysis(PCResult.result) != sm_pi_error) sm_pi_state = sm_pi_clearBeforeGoBack;
          else sm_pi_state = sm_pi_error;
        }
        else if (PCResult.err || PEResult.isEnd)
        {
          sm_pi_state = sm_pi_error;
          Serial.println("PI State Machine error: sm_pi_contentAnalyse - 01.");
        }
      break;
      
      case sm_pi_clearBeforeGoBack: // use to reset something
        CleanPISturct ();  //reset structure
        sm_pi_state = sm_pi_begin;
      break;
      
      case sm_pi_error: // if is not correct format or other error
        serverClients[_clientNum].flush();  // flush cache
        CleanCommandTemp (); // reset motor command temp
        CleanPISturct ();  //reset structures
        sm_pi_state = sm_pi_begin;
        Serial.println("PI State Machine error: Error occured, Format not correct.");
      break;
      
      case sm_pi_fullEnding:
        ProcessIncoming_ContentExtract (_clientNum);
        if (PEResult.isEnd && !PCResult.isEnd && !PEResult.err)
        {
          if (PEResult.result == 13)  sm_pi_state = sm_pi_EndingStage_0;// if char is CR
          else
          {
            sm_pi_state = sm_pi_error;
            Serial.println("PI State Machine error: sm_pi_fullEnding_1.");
          }
        }
        else if (PEResult.err || PCResult.isEnd)
        {
          sm_pi_state = sm_pi_error;
          Serial.println("PI State Machine error: sm_pi_fullEnding_0.");
        }
      break;

      case sm_pi_EndingStage_0:
        _num = serverClients[_clientNum].read();
        if (_num == '[') sm_pi_state = sm_pi_EndingStage_1;
        else
        {
          sm_pi_state = sm_pi_error;  // uncorrenct format data
          Serial.print("PI State Machine error: sm_pi_EndingStage_0");
        }
      break;
      
      case sm_pi_EndingStage_1:
        ProcessIncoming_ContentExtract (_clientNum);
        if (PEResult.isEnd && !PCResult.isEnd && !PEResult.err)
        {
          if (PEResult.result == 10) // if char is LF
          {

            /*
            Serial.println("Good, get end");
            */
            CommandApplyAtEnd ();
            CleanCommandTemp (); // reset motor command temp
            sm_pi_state = sm_pi_clearBeforeGoBack;  // prepare go back to beginning
          }
          else
          {
            sm_pi_state = sm_pi_error;
            Serial.println("PI State Machine error: sm_pi_EndingStage_1_1.");
          }
        }
        else if (PEResult.err || PCResult.isEnd)
        {
          sm_pi_state = sm_pi_error;
          Serial.println("PI State Machine error: sm_pi_EndingStage_1_0.");
        }
      break;
    }
  }
  if (PCResult.err || PEResult.err)
  {
    CleanPISturct ();  //reset structure
    Serial.println("PI outside error: structure err..");
  }
}

void ProcessIncoming_ContentExtract (int _clientNum) // extract data from incoming message before break mark
{
  ProcessContentResult* _cResult = &PCResult;
  ProcessEndingResult* _eResult = &PEResult;
  
  char _num;
  while (serverClients[_clientNum].available()) // loop until package finish
  {
    _num = serverClients[_clientNum].read();
    if (_num == ')' ||  _num == ';')
    {
      _cResult->result = String (pi_buff);
      _cResult->isEnd = true;
      CleanPIBuff (); // got reuslt, can reset buffer
      return;
    }
    else if (_num == ']')
    {
      _eResult->result = pi_buff[pi_bufferCount - 1];
      _eResult->isEnd = true;
      CleanPIBuff (); // got reuslt, can reset buffer
      return;
    }
    pi_buff[pi_bufferCount] = _num;
    pi_bufferCount ++;
    if (pi_bufferCount > 5)
    {
      _cResult->err = true; // if content get more then 5 letter
      CleanPIBuff ();
      return;
    }
  }
  /*
  _cResult->err = true; // content result and this command should not be execute
  _eResult->err = true; // content result and this command should not be execute
  CleanPIBuff ();
  Serial.println("PI content error: Error occured, content process unexpected failed");
  */
}

void CleanCommandTemp ()
{
  //leftMotorTemp = rightMotorTemp = midMotorTemp = speedTemp = -999;  // clean temp stroge valuable
}

void CleanPISturct () 
{
  PCResult = ProcessContentResult ();  //reset structure
  PEResult = ProcessEndingResult ();  //reset structure
}

void CleanPIBuff ()
{
  memset(pi_buff, 0, sizeof(pi_buff));  // zero the buffer
  pi_bufferCount = 0;
}

int Convert_String_to_Int (String _str)
{
  char _takeNum [_str.length() + 1]; // don't know why toCharArray will not return full lenght number if array length exactly same as string length. May be first position of array has been occupied by other usage, so add 1 more position.
  _str.toCharArray (_takeNum, sizeof (_takeNum)); // string to array
  
  return atoi(_takeNum);  // array to int
}

uint8_t HeaderAnalysis (String _str)
{
  if (!tcpMessageDebug)
  {
    if (_str == "SP")  return sm_pi_workingFine;
    else
    {
      return sm_pi_error;  // uncorrenct format data
      Serial.println("PI State Machine error: sm_pi_headerAnalyse - 02.");
    }
  }
  else
  {
    Serial.println("Is in Header analysis!!");
    return sm_pi_workingFine;
  }
}

uint8_t ContentAnalysis (String _str)
{
  if (!tcpMessageDebug)
  {
    if (_str == "SP")  return sm_pi_workingFine;
    else
    {
      return sm_pi_error;  // uncorrenct format data
      Serial.println("PI State Machine error: sm_pi_contentAnalyse - 02.");
    }
  }
  else
  {
    Serial.println("Is in content analysis!!");
    return sm_pi_workingFine;
  }
}

void CommandApplyAtEnd ()
{
  if (!tcpMessageDebug)
  {
  }
  else
  {
    Serial.println("Is ending and appling commands!!");
  }
}

void MessageToBePrintToSerialConstantly()
{
  if (SerialMessagePrintTimerCache == 0)  SerialMessagePrintTimerCache = millis();  // start countting led turnning on time
  if (millis() - SerialMessagePrintTimerCache < 0)
  { 
    SerialMessagePrintTimerCache = 4294967295 - SerialMessagePrintTimerCache;
    serialMessageTimerRollover = true;
  }
  if (TimeCounter (SerialMessagePrintTimerCache, 3000, serialMessageTimerRollover))  // sent message every 3s
  {
    SerialMessagePrintTimerCache = 0; // reset timer
    serialMessageTimerRollover = false;

    // write message below
    if (UseNetWorkFunciton)
    {
      if (UsingSoftAP)
      {// monitor how many connecter
        Serial.printf("Stations connected = %d\n", WiFi.softAPgetStationNum());
      }
      else
      {

      }
    }
    
    // write message above
  }
}

void DebugAndTest ()
{
}

// use to calculate how long past after previous time
// can not handle rollover inside, so better reset every 50 days or do it outside
bool TimeCounter (unsigned long _startTime, unsigned int _targetTime, bool _rollover)
{
  unsigned int _compare = 0;
  if (!_rollover) _compare = (unsigned int)(millis() - _startTime);
  else _compare = (unsigned int)(millis() + _startTime);
  if (_compare >= _targetTime)
  {
    return true;
  }
  else
  {
    return false;
  }

}
