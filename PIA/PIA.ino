#include <SoftwareSerial.h>
#include <ESP8266.h>

#include <MPU6050_light.h>
#include <Wire.h>

#include "WifiAuthentication.h"

#define BAUD_RATE 9600

#define RX 2
#define TX 3
#define DISABLE_PIN 5
#define RED_LED 8
#define GREEN_LED 9
#define BUZZER_PIN 13

SoftwareSerial esp8266(RX, TX);
ESP8266 wifi(esp8266);

//A4 -> SDA
//A5 -> SCL
MPU6050 mpu(Wire);

const char *successJSON = "{\"state\": \"success\"}";
const char *invalidJSON = "{\"state\": \"invalid action\"}";
const char *ok = "Ok";
const char *fail = "Fail";

bool alarmActive;
bool alarmSet;

//------------------//
//  SETUP AND LOOP  //
//------------------//
void setup()
{
  Serial.begin(BAUD_RATE);
  esp8266.begin(BAUD_RATE);
  Wire.begin();

  alarmSet = false;
  alarmActive = false;

  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(DISABLE_PIN, INPUT);
  pinMode(RED_LED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);

  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(RED_LED, LOW);
  digitalWrite(GREEN_LED, LOW);

  Serial.print("ESP: ");
  bool espInit = initializeESP();
  Serial.println(espInit ? ok : fail);

  Serial.print("MPU: ");
  bool mpuInit = InitializeMPU();
  Serial.println(mpuInit ? ok : fail);

  if (espInit && mpuInit)
  {
    digitalWrite(GREEN_LED, HIGH);
  }
  else
  {
    digitalWrite(RED_LED, HIGH);

    while (true)
      ;
  }
}

void loop()
{
  CheckEsp();

  if(alarmSet)
    UpdateMPU();
}

/*
    action    |  char value
  ---------------------------
    turn_off  |  0
    turn_on   |  1
    set       |  S
    disable   |  D
    calibrate |  C
    get       |  G
    invalid   |  I
    no_action |  N
*/
void ExecuteRequestedAction(char actionChar)
{
  switch (actionChar)
  {
  case '0':
    TurnAlarmOff();
    break;
  case '1':
    TurnAlarmOn();
    break;
  case 'S':
    SetAlarm();
    break;
  case 'D':
    DisableAlarm();
    break;
  case 'C':
    CalibrateDevice();
    break;
  case 'G':
    break;
  case 'I':
  case 'N':
    break;
  default:
    Serial.println(fail);
  }
}

//-----------//
//  ESP8266  //
//-----------//
bool initializeESP()
{
  if (!wifi.restart())
    return false;

  if (!wifi.setOprToStationSoftAP())
    return false;

  if (!wifi.joinAP(ssid, password))
    return false;

  //String localIp = wifi.getLocalIP();
  //Serial.println(localIp);

  if (!wifi.enableMUX())
    return false;

  if (!wifi.startTCPServer(80))
    return false;

  if (!wifi.setTCPServerTimeout(10))
    return false;

  return true;
}

void CheckEsp() {
  char actionChar = GetRequestAction();
  
  ExecuteRequestedAction(actionChar);
}

char GetRequestAction()
{
  int bufferLen = 100;
  char espBuffer[bufferLen] = {0};
  char action = 'N';

  if (esp8266.available() > 0)
  {
    CopyEspBuffer(espBuffer, bufferLen);

    Serial.println("--");
    Serial.println(espBuffer);
    Serial.println("--");

    char *request = GetRequestPointer(espBuffer);

    if(request != NULL)
      HandleRequest(request, &action);
  }
  
  return action;
}

//---- ESP Buffer ----//
void ClearEspBuffer()
{
  while (esp8266.available())
    esp8266.read();
}

void CopyEspBuffer(char *espBuffer, int bufferLen)
{
  int bufferPos = 0;

  while (esp8266.available() && bufferPos < bufferLen - 1)
  {
    char c = (char)esp8266.read();
    espBuffer[bufferPos] = c;

    bufferPos++;

    if (!esp8266.available())
      delay(200);
  }
  espBuffer[bufferPos] = '\0';

  ClearEspBuffer();
}

//---- ESP Request ----//
char *GetRequestPointer(char *espBuffer)
{
  return strstr(espBuffer, "+IPD");
}

void HandleRequest(char *request, char *action)
{
  int jsonLen = 50;
  int answerLen = 110 + jsonLen;
  char answer[answerLen] = {0};
  char json[jsonLen] = {0};

  // -- Connection ID -- //
  int connectionId = GetConnectionId(request);

  if (connectionId == -1) {
    *action = 'I';
    return;
  }

  // -- Request Action -- //
  int actionLen = 20;
  char actionString[actionLen] = {0};

  CopyActionString(request, actionString, actionLen);

  if (actionString == NULL) {
    *action = 'I';
    return;
  }

  if (strcmp(actionString, "get_status") == 0)
  {
    *action = 'G';

    strcat(json, "{\"alarm_set\": \"");
    strcat(json, alarmSet ? "SET" : "DISABLED");
    strcat(json, "\", \"alarm_status\": \"");
    strcat(json, alarmActive ? "ON" : "OFF");
    strcat(json, "\"}");
  }
  else
  {
    strcpy(json, successJSON);

    if (strcmp(actionString, "turn_off") == 0)
      *action = '0';
    else if (strcmp(actionString, "turn_on") == 0)
      *action = '1';
    else if (strcmp(actionString, "set") == 0)
      *action = 'S';
    else if (strcmp(actionString, "disable") == 0)
      *action = 'D';
    else if (strcmp(actionString, "calibrate") == 0)
      *action = 'C';
    else
    {
      *action = 'I';
      strcpy(json, invalidJSON);
    }
  }

  BuildAnswer(answer, json);

  // -- Response and Connection Close -- //
  Serial.print("> Response: ");
  Serial.println(wifi.send(connectionId, answer, strlen(answer)) ? ok : fail);

  Serial.print("> Release: ");
  Serial.println(wifi.releaseTCP(connectionId) ? ok : fail);

  return action;
}

int GetConnectionId(char *request)
{
  return strlen(request) < 6 ? -1 : request[5] - 48;
}

void CopyActionString(char *request, char *actionString, int actionLen)
{
  int pos = 0;
  char findStr[] = "action=";
  char *p = strstr(request, findStr);

  if (p == NULL)
  {
    actionString = NULL;
    return;
  }

  p += strlen(findStr);

  while (*p != '\0' && *p != ' ' && *p != '&' && pos < actionLen - 1)
  {
    actionString[pos] = *p;

    pos++;
    p++;
  }
  actionString[pos] = '\0';
}

//-----------//
//  MPU6050  //
//-----------//
bool InitializeMPU()
{
  return mpu.begin() == 0;
}

void CalculateMpuOffsets()
{
  //TurnLedOn (?
  mpu.calcOffsets();
  //TurnLedOff (?
}

void UpdateMPU()
{
  mpu.update();

  CheckAngles();
}

void CheckAngles()
{
  float xAngle = mpu.getAngleX();
  float yAngle = mpu.getAngleY();
  float zAngle = mpu.getAngleZ();

  if (abs(xAngle) > 15 || abs(yAngle) > 15)
    TurnAlarmOn();
}

//---------//
//  OTHER  //
//---------//
void PanicMode()
{
  int buzzerState = HIGH;
  
  //Send notification

  while (alarmActive)
  {
    Serial.println("PANIC");
    
    digitalWrite(BUZZER_PIN, buzzerState);
    buzzerState = !buzzerState;

    delay(500);

    CheckEsp();

    //if (digitalRead(DISABLE_PIN) == HIGH)
    //  alarmActive = false;
  }

  digitalWrite(BUZZER_PIN, LOW);
}

//-----------------//
//  ESP Responses  //
//-----------------//
void ConcatResponseHeader(char *buff)
{
  strcat(buff, "HTTP/1.1 200 OK\r\n");
  strcat(buff, "Content-Type: application/json\r\n");
  strcat(buff, "Access-Control-Allow-Origin: *\r\n");
  strcat(buff, "Connection: close\r\n\r\n");
}

void BuildAnswer(char *answer, char *json)
{
  ConcatResponseHeader(answer);
  strcat(answer, json);
}

//---------//
//  Alarm  //
//---------//
void SetAlarm()
{
  if(!alarmSet && !alarmActive) {
    CalculateMpuOffsets();

    alarmSet = true; 
  }
}

void DisableAlarm()
{
  if(alarmSet) {
    alarmSet = false; 
    alarmActive = false;
  }
}

void TurnAlarmOn()
{
  if(alarmSet && !alarmActive) {
    alarmActive = true;

    PanicMode();
  }
}

void TurnAlarmOff()
{
  if(alarmSet && alarmActive) {
    alarmActive = false;

    CalibrateDevice();
  }
}

void CalibrateDevice() {
  if(alarmSet && !alarmActive) {
    CalculateMpuOffsets();
  }
}
