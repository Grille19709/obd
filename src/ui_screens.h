// ui_screens.h - All screen drawing functions

void card(int x,int y,int w,int h,uint16_t c,const char* t){
    gfx->fillRoundRect(x,y,w,h,6,c);gfx->drawRoundRect(x,y,w,h,6,COLOR_GRAY);
    gfx->setTextColor(COLOR_LIGHT_GRAY);gfx->setTextSize(1);gfx->setCursor(x+8,y+4);gfx->print(t);
}
void bar(int x,int y,int w,int h,int p,uint16_t fc){
    gfx->fillRoundRect(x,y,w,h,4,COLOR_BG_DARKER);
    int fw=map(constrain(p,0,100),0,100,0,w-4);
    if(fw>0)gfx->fillRoundRect(x+2,y+2,fw,h-4,3,fc);
}
void toggle(int x,int y,int w,bool s,const char* l){
    gfx->fillRoundRect(x,y,w,30,15,s?COLOR_GREEN:COLOR_GRAY);
    gfx->fillCircle(s?x+w-15:x+15,y+15,12,COLOR_WHITE);
    gfx->setTextColor(COLOR_TEXT);gfx->setTextSize(1);gfx->setCursor(x+w/2-20,y+38);gfx->print(l);
}

// The built-in font has no degree sign ("°" is UTF-8 and prints as two junk glyphs):
// draw a small ring at the cursor, then "C". s = current text size.
void degC(uint8_t s,uint16_t c=COLOR_WHITE){
    int x=gfx->getCursorX(),y=gfx->getCursorY(),r=s<2?2:s+1;
    gfx->drawCircle(x+r+1,y+r,r,c);
    if(s>=2)gfx->drawCircle(x+r+1,y+r,r-1,c);
    gfx->setCursor(x+6*s,y);gfx->print("C");
}
// temperature centred in a card, same size and colour for every card
void tempCell(int x,int y,int w,float t){
    String v=f(t,1);int tw=(v.length()+2)*18;
    gfx->setTextSize(3);gfx->setTextColor(COLOR_WHITE);
    gfx->setCursor(x+(w-tw)/2,y+30);gfx->print(v);degC(3);
}

// ---- Home: battery (BMS) and solar (Victron) are real values, "--" without data ----
void drawDash(){
    gfx->setTextColor(COLOR_WHITE);gfx->setTextSize(3);gfx->setCursor(10,5);gfx->print("KITELINER");
    gfx->drawFastHLine(10,36,SCREEN_WIDTH-20,COLOR_ACCENT2);

    card(10,44,230,95,COLOR_BG_DARKER,"BATTERY");
    gfx->setTextSize(4);gfx->setCursor(70,58);
    if(valid(V_BAT_SOC)){
        float p=val[V_BAT_SOC];
        gfx->setTextColor((p>50)?COLOR_GREEN:(p>20)?COLOR_YELLOW:COLOR_RED);gfx->print(f(p,0)+"%");
        gfx->setTextSize(1);gfx->setTextColor(COLOR_LIGHT_GRAY);gfx->setCursor(30,110);
        gfx->print(f(val[V_BAT_V],2)+"V  "+f(val[V_BAT_A],1)+"A  "+f(val[V_BAT_AH],0)+"Ah");
        if(valid(V_BAT_TEMP)){gfx->print("  "+f(val[V_BAT_TEMP],0));degC(1,COLOR_LIGHT_GRAY);}
    }else{
        gfx->setTextColor(COLOR_GRAY);gfx->print("--");
        gfx->setTextSize(1);gfx->setCursor(30,110);
        if(valid(V_PV_V))gfx->print(f(val[V_PV_V],2)+"V (solar charger)");else gfx->print("no battery data");
    }

    card(250,44,220,95,COLOR_BG_DARKER,"SOLAR");
    gfx->setTextSize(3);gfx->setCursor(320,58);
    if(valid(V_PV_W)){
        gfx->setTextColor(val[V_PV_W]>0?COLOR_YELLOW:COLOR_GRAY);gfx->print(f(val[V_PV_W],0)+"W");
        gfx->setTextSize(1);gfx->setTextColor(COLOR_LIGHT_GRAY);gfx->setCursor(262,96);
        gfx->print(f(val[V_PV_A],1)+"A  "+solarStateText(solarState));
        gfx->setCursor(262,114);gfx->print("today "+f(val[V_PV_KWH],2)+" kWh");
    }else{
        gfx->setTextColor(COLOR_GRAY);gfx->print("--");
        gfx->setTextSize(1);gfx->setCursor(262,110);gfx->print("no solar data");
    }

    card(10,148,150,75,COLOR_BG_DARKER,"INTERIOR");tempCell(10,148,150,tempIn);
    card(170,148,150,75,COLOR_BG_DARKER,"EXTERIOR");tempCell(170,148,150,tempOut);

    card(330,148,140,75,COLOR_BG_DARKER,"STATUS");gfx->setTextSize(1);
    gfx->setTextColor(heatOn?COLOR_ORANGE:COLOR_GRAY);gfx->setCursor(340,168);gfx->print(heatOn?"HEAT:ON":"HEAT:OFF");
    gfx->setTextColor(fanOn?COLOR_GREEN:COLOR_GRAY);gfx->setCursor(340,188);gfx->print(fanOn?"FAN: ON":"FAN:OFF");
    gfx->setTextColor(lightMain?COLOR_YELLOW:COLOR_GRAY);gfx->setCursor(340,208);gfx->print(lightMain?"LIGHT:ON":"LIGHT:OFF");
}

// ---- Car: 8 live OBD values, only changed numbers are redrawn (no flicker) ----
struct CarCell{Val v;const char* name;const char* unit;uint8_t dec;};
const CarCell CAR[8]={
    {V_RPM,"RPM","1/min",0},{V_SPEED,"SPEED","km/h",0},{V_COOLANT,"COOLANT","",0},{V_CARBAT,"STARTER BAT","V",1},
    {V_BOOST,"BOOST","bar",2},{V_RAIL,"RAIL PRESSURE","bar",0},{V_MAF,"AIR MASS","g/s",1},{V_LOAD,"ENGINE LOAD","%",0}};
const int CAR_W=110,CAR_H=88;
char carShown[8][8];uint16_t carColor[8];uint8_t carStatusShown=0xFF;

uint16_t carValueColor(Val v,float x){
    switch(v){
        case V_COOLANT:return x<70?COLOR_ACCENT2:(x>105?COLOR_RED:COLOR_GREEN);
        case V_CARBAT:return (x<11.8f||x>15.0f)?COLOR_RED:COLOR_GREEN;
        default:return COLOR_WHITE;}
}
void updCar(){
    if(carStatusShown!=(uint8_t)obdStatus){carStatusShown=obdStatus;
        gfx->fillRect(250,8,220,20,COLOR_BG);gfx->setTextSize(1);
        gfx->setTextColor(obdStatus==ST_CONNECTED?COLOR_GREEN:COLOR_ORANGE);
        String s=String("OBD: ")+obdStatusText();gfx->setCursor(470-s.length()*6,14);gfx->print(s);}
    for(int i=0;i<8;i++){
        char b[8];uint16_t c=COLOR_GRAY;
        if(valid(CAR[i].v)){snprintf(b,sizeof(b),"%5.*f",CAR[i].dec,val[CAR[i].v]);c=carValueColor(CAR[i].v,val[CAR[i].v]);}
        else strcpy(b,"   --");
        if(c==carColor[i]&&!strcmp(b,carShown[i]))continue;
        strcpy(carShown[i],b);carColor[i]=c;
        gfx->setTextSize(3);gfx->setTextColor(c,COLOR_BG_DARKER);
        gfx->setCursor(10+(i%4)*116+10,44+(i/4)*94+30);gfx->print(b);}
}
void drawCar(){
    gfx->setTextColor(COLOR_WHITE);gfx->setTextSize(3);gfx->setCursor(10,5);gfx->print("ENGINE");
    gfx->drawFastHLine(10,36,SCREEN_WIDTH-20,COLOR_ACCENT2);
    for(int i=0;i<8;i++){int x=10+(i%4)*116,y=44+(i/4)*94;
        card(x,y,CAR_W,CAR_H,COLOR_BG_DARKER,CAR[i].name);
        gfx->setTextColor(COLOR_GRAY);
        if(CAR[i].v==V_COOLANT){gfx->setCursor(x+CAR_W-8-12,y+70);degC(1,COLOR_GRAY);}
        else{gfx->setCursor(x+CAR_W-8-strlen(CAR[i].unit)*6,y+70);gfx->print(CAR[i].unit);}
        carShown[i][0]=0;}
    carStatusShown=0xFF;updCar();
}

void drawLight(){
    gfx->setTextColor(COLOR_WHITE);gfx->setTextSize(3);gfx->setCursor(10,5);gfx->print("LIGHTING");
    gfx->drawFastHLine(10,36,SCREEN_WIDTH-20,COLOR_YELLOW);
    card(20,48,210,52,lightMain?0x19C3:COLOR_BG_DARKER,"");
    gfx->setTextSize(1);gfx->setTextColor(COLOR_WHITE);gfx->setCursor(30,74);gfx->print("Interior Lights");toggle(340,55,90,lightMain,"ON / OFF");
    card(20,112,210,52,lightAmb?0x18C7:COLOR_BG_DARKER,"");
    gfx->setCursor(30,138);gfx->print("Ambient LED");toggle(340,119,90,lightAmb,"ON / OFF");
    card(20,176,210,52,lightAwn?0x3943:COLOR_BG_DARKER,"");
    gfx->setCursor(30,202);gfx->print("Awning Light");toggle(340,183,90,lightAwn,"ON / OFF");
}

void drawClimate(){
    gfx->setTextColor(COLOR_WHITE);gfx->setTextSize(3);gfx->setCursor(10,5);gfx->print("CLIMATE");
    gfx->drawFastHLine(10,36,SCREEN_WIDTH-20,COLOR_GREEN);
    card(20,48,210,52,heatOn?0x38C3:COLOR_BG_DARKER,"");
    gfx->setTextSize(1);gfx->setTextColor(COLOR_WHITE);gfx->setCursor(30,74);gfx->print("Diesel Heater");toggle(340,55,90,heatOn,"ON / OFF");
    card(20,112,210,52,fanOn?0x19C5:COLOR_BG_DARKER,"");
    gfx->setCursor(30,138);gfx->print("Roof Fan");toggle(340,119,90,fanOn,"ON / OFF");
    gfx->setTextSize(1);gfx->setTextColor(COLOR_LIGHT_GRAY);gfx->setCursor(220,157);gfx->print("Speed");
    const char* sl[]={"OFF","LOW","MED","HIGH"};
    for(int s=0;s<4;s++){int sx=30+s*90;uint16_t b=(s==fanSpd)?COLOR_GREEN:COLOR_BG_DARKER;
        gfx->fillRoundRect(sx,178,70,28,5,b);gfx->drawRoundRect(sx,178,70,28,5,COLOR_GRAY);
        gfx->setTextColor(s==fanSpd?COLOR_WHITE:COLOR_LIGHT_GRAY);gfx->setCursor(sx+20,186);gfx->print(sl[s]);}
    gfx->setTextSize(1);gfx->setTextColor(COLOR_TEXT);gfx->setCursor(140,214);gfx->print("Target: "+f(targetT,1));degC(1,COLOR_TEXT);
}

void drawWater(){
    gfx->setTextColor(COLOR_WHITE);gfx->setTextSize(3);gfx->setCursor(10,5);gfx->print("WATER");
    gfx->drawFastHLine(10,36,SCREEN_WIDTH-20,0x34DB);
    card(10,44,460,56,COLOR_BG_DARKER,"FRESH WATER");bar(20,72,440,18,waterF,0x34DB);
    gfx->setTextSize(1);gfx->setTextColor(COLOR_WHITE);gfx->setCursor(420,48);gfx->print(String(waterF)+"%");
    card(10,110,460,56,COLOR_BG_DARKER,"GREY WATER");bar(20,138,440,18,waterG,COLOR_GRAY);
    gfx->setCursor(420,114);gfx->print(String(waterG)+"%");
    card(10,176,460,50,COLOR_BG_DARKER,"WASTE TANK");bar(20,204,440,16,waterW,waterW>75?COLOR_RED:COLOR_ORANGE);
    gfx->setCursor(420,180);gfx->print(String(waterW)+"%");
}

// ---- More: brightness, connection state and the BLE devices in range ----
void drawRadio(){
    card(10,122,225,104,COLOR_BG_DARKER,"CONNECTIONS");
    gfx->setCursor(20,144);gfx->setTextColor(obdStatus==ST_CONNECTED?COLOR_GREEN:COLOR_ORANGE);
    gfx->print(String("OBD:     ")+obdStatusText());
    gfx->setCursor(20,160);gfx->setTextColor(COLOR_GRAY);gfx->print(obdFound?obdAddr:"(BLE ELM327 adapter)");
    gfx->setCursor(20,180);gfx->setTextColor(valid(V_BAT_SOC)?COLOR_GREEN:COLOR_ORANGE);
    gfx->print(valid(V_BAT_SOC)?"Battery: connected":(strlen(AKKU_MAC)?"Battery: not reachable":"Battery: off"));
    gfx->setCursor(20,200);gfx->setTextColor(valid(V_PV_W)?COLOR_GREEN:COLOR_ORANGE);
    gfx->print(valid(V_PV_W)?String("Solar:   ")+solarStateText(solarState):String(victronOn?"Solar:   no data":"Solar:   off"));

    card(245,122,225,104,COLOR_BG_DARKER,"BLE DEVICES IN RANGE");
    int y=140;
    for(int i=0;i<SEEN_MAX;i++){
        if(!seen[i].at||millis()-seen[i].at>30000)continue;
        gfx->setTextColor(obdFound&&!strcmp(seen[i].addr,obdAddr)?COLOR_GREEN:COLOR_LIGHT_GRAY);
        gfx->setCursor(255,y);gfx->print(seen[i].name);
        gfx->setCursor(425,y);gfx->print(seen[i].rssi);y+=14;}
    if(y==140){gfx->setTextColor(COLOR_GRAY);gfx->setCursor(255,y);gfx->print("none");}
}
void drawSettings(){
    gfx->setTextColor(COLOR_WHITE);gfx->setTextSize(3);gfx->setCursor(10,5);gfx->print("SETTINGS");
    gfx->drawFastHLine(10,36,SCREEN_WIDTH-20,COLOR_GRAY);
    gfx->setTextSize(1);gfx->setTextColor(COLOR_LIGHT_GRAY);gfx->setCursor(20,54);gfx->print("Display Brightness");
    gfx->setCursor(340,54);gfx->setTextColor(COLOR_GRAY);gfx->print("KitelinerOS v1.1 OBD");
    gfx->drawRoundRect(20,80,440,30,5,COLOR_GRAY);
    int fw=map(bright,10,255,0,436);gfx->fillRoundRect(22,82,fw,26,4,COLOR_ACCENT);
    gfx->setTextColor(COLOR_WHITE);gfx->setCursor(225,87);gfx->print(String(map(bright,10,255,0,100))+"%");
    drawRadio();
}
