#include <SoftwareSerial.h>
#include <ESP8266.h>

#include <MPU6050_light.h>
#include <Wire.h>

#include "AP.h"

#define BAUD_RATE 9600
#define DEBUG false

#define RX 2
#define TX 3
#define DISABLE_PIN 5
#define BUZZER_PIN 7
#define RED_LED 12
#define GREEN_LED 13

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

  /*
  Serial.print("ESP: ");
  Serial.println(initializeESP() ? ok : fail);

  Serial.print("MPU: ");
  Serial.println(initializeMPU() ? ok : fail);
  */

  Serial.print("ESP: ");
  bool espInit = initializeESP();
  Serial.println(espInit ? ok : fail);

  Serial.print("MPU: ");
  bool mpuInit = initializeMPU();
  Serial.println(mpuInit ? ok : fail);

  if(espInit && mpuInit) {
    digitalWrite(GREEN_LED, HIGH);
  }
  else {
    digitalWrite(RED_LED, HIGH);

    while(true);
  }
    
}

void loop()
{
  char action = GetRequestAction();
  HandleRequestAction(action);

  updateMPU();
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
void HandleRequestAction(char action)
{
  switch (action)
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
    CalculateMpuOffsets();
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

char GetRequestAction()
{
  int bufferLen = 180;
  char espBuffer[bufferLen] = {0};

  if (esp8266.available() > 0)
  {
    CopyEspBuffer(espBuffer, bufferLen);

    Serial.println("--");
    Serial.println(espBuffer);
    Serial.println("--");

    char *request = GetIncomingRequestPointer(espBuffer);

    return request == NULL ? 'N' : HandleRequest(request);
  }
  else
    return 'N';
}

//---- ESP Buffer ----//
void clearEspBuffer()
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

  clearEspBuffer();
}

//---- ESP Incoming Request ----//
char *GetIncomingRequestPointer(char *espBuffer)
{
  return strstr(espBuffer, "+IPD");
}

char HandleRequest(char *request)
{
  int answerLen = 150;
  char answer[answerLen] = {0};
  char action;

  // -- Connection ID -- //
  int connectionId = GetConnectionId(request);

  if (connectionId == -1)
    return 'I';

  // -- Request Action -- //
  int actionLen = 20;
  char actionValue[actionLen] = {0};
  char json[50] = {0};

  CopyActionRequest(request, actionValue, actionLen);

  if (actionValue == NULL)
    return 'I';
   
  if (strcmp(actionValue, "get_status") == 0)
  {
    action = 'G';
    
    strcat(json, "{\"alarm_status\": \"");
    strcat(json, alarmActive ? "ON" : "OFF");
    strcat(json, "\"}");
  }
  else {
    strcat(json, successJSON);
    
    if (strcmp(actionValue, "turn_off") == 0)
      action = '0';
    else if (strcmp(actionValue, "turn_on") == 0)
      action = '1';
    else if (strcmp(actionValue, "set") == 0)
      action = 'S';
    else if (strcmp(actionValue, "disable") == 0)
      action = 'D';
    else if (strcmp(actionValue, "calibrate") == 0)
      action = 'C';
    else {
      action = 'I'; 
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

void CopyActionRequest(char *request, char *actionStr, int actionLen)
{
  int pos = 0;
  char findStr[] = "action=";
  char *p = strstr(request, findStr);

  if (p == NULL)
  {
    actionStr = NULL;
    return;
  }

  p += strlen(findStr);

  while (*p != '\0' && *p != ' ' && *p != '&' && pos < actionLen - 1)
  {
    actionStr[pos] = *p;

    pos++;
    p++;
  }
  actionStr[pos] = '\0';
}

//-----------//
//  MPU6050  //
//-----------//
bool initializeMPU()
{
  //byte status = mpu.begin();

  if (mpu.begin() != 0)
    return false;

  CalculateMpuOffsets();

  return true;
}

void CalculateMpuOffsets()
{
  mpu.calcOffsets();
}

void updateMPU()
{
  mpu.update();

  checkAngles();
}

void checkAngles()
{
  float xAngle = mpu.getAngleX();
  float yAngle = mpu.getAngleY();
  float zAngle = mpu.getAngleZ();

  if (DEBUG)
  {
    Serial.println(xAngle);
    Serial.println(yAngle);
    Serial.println(zAngle);
    Serial.println("--");
  }

  if (alarmSet && (abs(xAngle) > 15 || abs(yAngle) > 15))
    TurnAlarmOn();
}

//---------//
//  OTHER  //
//---------//
void panicMode()
{
  if(!alarmSet)
    return;
  
  //Send notification

  int buzzerState = HIGH;
  while (alarmActive && alarmSet)
  {
    Serial.println("PANIC");
    digitalWrite(BUZZER_PIN, buzzerState);

    buzzerState = !buzzerState;

    char action = GetRequestAction();
    HandleRequestAction(action);

    //if (digitalRead(DISABLE_PIN) == HIGH)
    //  alarmActive = false;

    delay(500);
  }

  digitalWrite(BUZZER_PIN, LOW);
}

void ConcatResponseHeader(char *buff)
{
  strcat(buff, "HTTP/1.1 200 OK\r\n");
  strcat(buff, "Content-Type: application/json\r\n");
  strcat(buff, "Access-Control-Allow-Origin: *\r\n");
  strcat(buff, "Connection: close\r\n\r\n");
}

//-----------------//
//  ESP Responses  //
//-----------------//
void BuildAnswer(char *answer, char *json)
{
  ConcatResponseHeader(answer);
  strcat(answer, json);
}

//---------//
//  Alarm  //
//---------//
void SetAlarm() {
  alarmSet = true;

  CalculateMpuOffsets();
}

void DisableAlarm() {
  alarmSet = false;
  alarmActive = false;
}

void TurnAlarmOn() {
  if(!alarmSet)
    return;
   
  alarmActive = true;
  
  panicMode();
}

void TurnAlarmOff() {
  alarmActive = false;
  
  CalculateMpuOffsets();
}
