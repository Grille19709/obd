/*
 * KitelinerOS + OBD - Camper Van Control Panel
 * JC4827W543 (NV3041A QSPI + GT911)
 *
 * Camper panel (from "ESP32 LCD Camper Van") with the OBD functions of
 * "OBD Adapter/ble_anzeige" on one board:
 *   - "Car" tab: 8 live engine values from a Bluetooth LE ELM327 adapter
 *   - "Home" tab: real battery (JBD BMS) and solar (Victron SmartSolar) values
 *   - "More" tab: connection state and BLE devices in range
 *   - the phone page still works (panel shows up as BLE device "ESP32-OBD")
 * Devices and keys: config.h. All radio work runs in background tasks (ble_data.h).
 */

#include <Arduino.h>
#include "DisplayConfig.h"
#include <Wire.h>
#include "ble_data.h"

// GT911 Touch (raw I2C)
#define GT911_ADDR 0x5D
int touchX=-1, touchY=-1; bool touchPressed=false;

void gt911Init(){
    pinMode(38,OUTPUT); pinMode(3,INPUT_PULLUP);
    digitalWrite(38,LOW); delay(10); digitalWrite(38,HIGH); delay(50);
    Wire.begin(8,4);
    Wire.beginTransmission(GT911_ADDR); Wire.write(0x80); Wire.write(0x00);
    Serial.println(Wire.endTransmission()==0?"GT911 OK":"GT911 not found");
}
void gt911Read(){
    touchPressed=false; Wire.beginTransmission(GT911_ADDR); Wire.write(0x81); Wire.write(0x4E);
    if(Wire.endTransmission()!=0){return;} Wire.requestFrom(GT911_ADDR,(uint8_t)1);
    if(!Wire.available()){return;} uint8_t buf=Wire.read();
    if((buf&0x80)==0){return;} uint8_t pts=buf&0x0F; if(pts==0){
        Wire.beginTransmission(GT911_ADDR);Wire.write(0x81);Wire.write(0x4E);Wire.write(0x00);Wire.endTransmission();return;}
    Wire.beginTransmission(GT911_ADDR);Wire.write(0x81);Wire.write(0x50);Wire.endTransmission();
    Wire.requestFrom(GT911_ADDR,(uint8_t)5); if(Wire.available()<5){return;}
    uint8_t d[5]; for(int i=0;i<5;i++)d[i]=Wire.read();
    touchX=((uint16_t)d[1]<<8)|d[0]; touchY=((uint16_t)d[3]<<8)|d[2];
    touchPressed=(touchX<480&&touchY<272);
    Wire.beginTransmission(GT911_ADDR);Wire.write(0x81);Wire.write(0x4E);Wire.write(0x00);Wire.endTransmission();
}

// Screen state
enum Screen{SCR_DASH=0,SCR_CAR,SCR_LIGHT,SCR_CLIMATE,SCR_WATER,SCR_SETTINGS,SCR_COUNT};
Screen curScr=SCR_DASH; bool fullRedraw=true,dataChanged=false;
const int TAB_Y=232,TAB_H=40,TAB_W=SCREEN_WIDTH/SCR_COUNT;
const char* TAB_LBL[]={"Home","Car","Light","Air","Water","More"};
const uint16_t TAB_COL[]={COLOR_ACCENT,COLOR_ACCENT2,COLOR_YELLOW,COLOR_GREEN,0x34DB,COLOR_GRAY};

// Sensors (temperatures and water levels are still simulated: no sensors connected yet)
float tempIn=21.5f,tempOut=8.0f; int waterF=65,waterG=40,waterW=15,waterFo=0,waterGo=0,waterWo=0;
bool lightMain=true,lightAmb=false,lightAwn=false,heatOn=false,fanOn=true;
int fanSpd=2,bright=255; float targetT=20.0f;
unsigned long lastUpd=0,lastLive=0,lastRadio=0; float lastTempIn=-1,lastTempOut=-1;
bool lastHeat=false,lastFan=false,lastLight=false;
// battery / solar as last drawn on the Home screen
float lastSoc=-1,lastBatA=0,lastPvW=-1; bool lastBatOk=false,lastPvOk=false; uint8_t lastSolarState=0xFF;

String f(float v,int d){char b[16];dtostrf(v,0,d,b);return String(b);}

void drawTabs(); void updSensors();
#include "ui_screens.h"

void setup(){
    Serial.begin(115200); delay(300);
    pinMode(GFX_BL,OUTPUT); digitalWrite(GFX_BL,HIGH);
    gfx->begin(); gt911Init();
    gfx->fillScreen(COLOR_BG);
    drawTabs(); drawDash(); fullRedraw=false;
    bleDataInit();
}

void loop(){
    gt911Read();
    if(touchPressed){
        if(touchY>=TAB_Y){int t=touchX/TAB_W;if(t>=0&&t<SCR_COUNT&&(int)curScr!=t){curScr=(Screen)t;fullRedraw=true;drawTabs();}}
        else{switch(curScr){
            case SCR_LIGHT:
                if(touchX>=340&&touchX<=430&&touchY>=55&&touchY<=85){lightMain=!lightMain;fullRedraw=true;}
                if(touchX>=340&&touchX<=430&&touchY>=119&&touchY<=149){lightAmb=!lightAmb;fullRedraw=true;}
                if(touchX>=340&&touchX<=430&&touchY>=183&&touchY<=213){lightAwn=!lightAwn;fullRedraw=true;}break;
            case SCR_CLIMATE:
                if(touchX>=340&&touchX<=430&&touchY>=55&&touchY<=85){heatOn=!heatOn;fullRedraw=true;}
                if(touchX>=340&&touchX<=430&&touchY>=119&&touchY<=149){fanOn=!fanOn;fullRedraw=true;}
                if(touchY>=178&&touchY<=206){if(touchX>=30&&touchX<=100)fanSpd=0;else if(touchX>=120&&touchX<=190)fanSpd=1;else if(touchX>=210&&touchX<=280)fanSpd=2;else if(touchX>=300&&touchX<=370)fanSpd=3;fullRedraw=true;}
                if(touchY>=210&&touchY<=230){if(touchX>=20&&touchX<=130)targetT-=0.5f;if(touchX>=350&&touchX<=460)targetT+=0.5f;targetT=constrain(targetT,10,30);fullRedraw=true;}break;
            case SCR_SETTINGS:
                if(touchY>=80&&touchY<=110){bright=constrain(map(touchX,20,460,10,255),10,255);analogWrite(GFX_BL,bright);fullRedraw=true;}break;
            default:break;}
        }delay(100);
    }
    if(millis()-lastUpd>=2000){lastUpd=millis();updSensors();}
    if(fullRedraw){fullRedraw=false;gfx->fillRect(0,0,SCREEN_WIDTH,TAB_Y,COLOR_BG);
        switch(curScr){case SCR_DASH:drawDash();break;case SCR_CAR:drawCar();break;case SCR_LIGHT:drawLight();break;case SCR_CLIMATE:drawClimate();break;case SCR_WATER:drawWater();break;case SCR_SETTINGS:drawSettings();lastRadio=millis();break;default:break;}}
    else if(dataChanged){dataChanged=false;if(curScr==SCR_DASH)drawDash();if(curScr==SCR_WATER)drawWater();}
    // live values arrive in the background
    if(curScr==SCR_CAR&&millis()-lastLive>=250){lastLive=millis();updCar();}
    if(curScr==SCR_SETTINGS&&millis()-lastRadio>=2000){lastRadio=millis();drawRadio();}
    delay(10);
}

void updSensors(){
    tempIn+=random(-2,3)*0.1f;tempOut+=random(-2,3)*0.1f;
    waterF+=random(-2,3);waterG+=random(-1,2);waterW+=random(-1,3);
    tempIn=constrain(tempIn,5,40);tempOut=constrain(tempOut,-10,45);
    waterF=constrain(waterF,0,100);waterG=constrain(waterG,0,100);waterW=constrain(waterW,0,100);
    if(abs(tempIn-lastTempIn)>=0.3||abs(tempOut-lastTempOut)>=0.3||
       abs(waterF-waterFo)>=1||abs(waterG-waterGo)>=1||abs(waterW-waterWo)>=1||
       heatOn!=lastHeat||fanOn!=lastFan||lightMain!=lastLight)dataChanged=true;
    lastTempIn=tempIn;lastTempOut=tempOut;lastHeat=heatOn;lastFan=fanOn;lastLight=lightMain;
    waterFo=waterF;waterGo=waterG;waterWo=waterW;
    // real battery / solar values
    bool batOk=valid(V_BAT_SOC),pvOk=valid(V_PV_W);
    if(batOk!=lastBatOk||pvOk!=lastPvOk||solarState!=lastSolarState||
       (batOk&&(val[V_BAT_SOC]!=lastSoc||fabsf(val[V_BAT_A]-lastBatA)>=0.1f))||
       (pvOk&&fabsf(val[V_PV_W]-lastPvW)>=1))dataChanged=true;
    lastBatOk=batOk;lastPvOk=pvOk;lastSolarState=solarState;
    lastSoc=val[V_BAT_SOC];lastBatA=val[V_BAT_A];lastPvW=val[V_PV_W];
}

void drawTabs(){
    gfx->fillRect(0,TAB_Y,SCREEN_WIDTH,TAB_H,COLOR_BG_DARKER);
    gfx->drawFastHLine(0,TAB_Y,SCREEN_WIDTH,COLOR_GRAY);gfx->setTextSize(1);
    for(int i=0;i<SCR_COUNT;i++){int tx=i*TAB_W;bool a=(i==(int)curScr);if(a)gfx->fillRect(tx+2,TAB_Y+2,TAB_W-4,TAB_H-4,TAB_COL[i]);gfx->setTextColor(a?COLOR_WHITE:COLOR_GRAY);gfx->setCursor(tx+(TAB_W-strlen(TAB_LBL[i])*6)/2,TAB_Y+14);gfx->print(TAB_LBL[i]);if(i>0)gfx->drawFastVLine(tx,TAB_Y+4,TAB_H-8,COLOR_GRAY);}
}
