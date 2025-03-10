#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Wire.h>
#include <Adafruit_MLX90614.h>
#include <stdlib.h> 
Adafruit_MLX90614 mlx = Adafruit_MLX90614();


#define SERVICE_UUID        "2c4133b0-1d80-4a87-966b-e71e4cc3a577"
#define CHARACTERISTIC_UUID "b1c1ee71-2ecd-4a6c-9159-59058addefcc"
#define NCOUNT 10
#define CHECK_INTERVAL_WHEN_NOT_FIRED 4 //20
#define CHECK_INTERVAL_WHEN_FIRED 1 //4
#define FIRED_DVALUE  1    //15  //when the object temperture is greater then the ambitoin temperture for 15C,support that the cooktop is fired.
#define DODEBUG 1
BLEServer* pServer = NULL;
BLECharacteristic* pCharacteristic = NULL;
bool deviceConnected = false;
bool oldDeviceConnected = false;
unsigned long previousMillis = 0;
const long interval = 1000;
int iLoop=0;
int nIsFired=0;
const int ledPin =  D10;     
int t_Ambient;
int t_Object;
int at_Ambient[NCOUNT];
int at_Object[NCOUNT];
int i_curpos=0;
int nReadCount=0;
int nTimesToSend=CHECK_INTERVAL_WHEN_FIRED;
char buf[20];


class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    if (DODEBUG)
      Serial.println("Connected");
    deviceConnected = true;        
  };

  void onDisconnect(BLEServer* pServer) {
    if (DODEBUG)
      Serial.println("DisConnected");
    deviceConnected = false;
  }
};

class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) { //when a characteristic is written
      String value = pCharacteristic->getValue();

      if (value.length() > 0) {
        Serial.println("*********");
        Serial.print("Send New value: ");
        //Serial.print(value);
        for (int i = 0; i < value.length(); i++)          
          Serial.print(value[i]);

        Serial.println();
        Serial.println("*********");
      }
    }
};

int read_temperture(int avgN){  
  int rt;
  int t_obj=0;
  int t_Amb=0;
  //read temperture from MLX90614,and save into the array 
  t_Amb=(int)(mlx.readAmbientTempC());  
  if(t_Amb<0)
    t_Amb=0;
  at_Ambient[i_curpos]=t_Amb;
  t_obj=(int)(mlx.readObjectTempC());
  if(t_obj<0)
    t_obj=0;  
  at_Object[i_curpos]=t_obj;
  rt=100*t_obj+t_Amb;
  //let to looply use the array for the next time reading
  i_curpos++;
  if(i_curpos>=NCOUNT)
    i_curpos=0;  
  nReadCount++; // the number of temperture reading
  if(nReadCount>=avgN){    
    t_obj=0;
    t_Amb=0;
    for (int i=0;i<avgN;i++){
      int idx=(i_curpos-1-i+NCOUNT)%NCOUNT;
      t_obj+=at_Object[idx];
      t_Amb+=at_Ambient[idx];
    }
    t_obj/=avgN;
    t_Amb/=avgN;          // get avgN of recent datas,and calculate the average.
    rt=100*t_obj+t_Amb;   //here combine the Ambient temperture and Object temperture into a integer. 
  } 
  return rt;

  //Serial.print("\tObject = ");  Serial.print(t_Object);  Serial.println("*C");
  /*
  Serial.print("Ambient = ");
  Serial.print(mlx.readAmbientTempF());
  Serial.print("*F\tObject = ");
  Serial.print(mlx.readObjectTempF());
  Serial.println("*F");*/
}

void setup() {
  mlx.begin();
  if (DODEBUG){
    pinMode(ledPin, OUTPUT);
    for(int i=0;i<3;i++)
    {
        digitalWrite(ledPin, HIGH);
        delay(500);
        digitalWrite(ledPin, LOW);
        delay(500);        
        
    }
    digitalWrite(ledPin, HIGH);
  }
  int t=read_temperture(5);
  t_Object=t/100;
  t_Ambient=t%100;
  Serial.begin(115200);

  BLEDevice::init("XIAO_ESP32C3_YUJUNXIAN");

  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);
  //BLECharacteristic *
  pCharacteristic = pService->createCharacteristic(
                                        CHARACTERISTIC_UUID,
                                        BLECharacteristic::PROPERTY_READ |
                                        BLECharacteristic::PROPERTY_WRITE|
                                        BLECharacteristic::PROPERTY_NOTIFY
                                       );
  pCharacteristic->addDescriptor(new BLE2902());
  //pCharacteristic->setCallbacks(new MyCallbacks());
  //pCharacteristic->setValue("Hello World");//from pCharacteristic on BLEService of pServer on BLEDevice.

  memset(buf,0,20);
  itoa(100*t_Object+t_Ambient,buf,10);  
  int l=strlen(buf);
  buf[l+1]='0';
  buf[l]=':';        
  buf[l+2]=0;              
  String s(buf);
  pCharacteristic->setValue(s);

  pService->start();
  
  Serial.println("Service started.");
  
  BLEAdvertising *pAdvertising = pServer->getAdvertising();
  //pAdvertising->start();
  //BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);  // functions that help with iPhone connections issue
  pAdvertising->setMinPreferred(0x12);
  pAdvertising->start();

  //BLEDevice::startAdvertising();
  
  Serial.println("Advertising started.");
  Serial.print("Characteristic defined:");
  Serial.println(s);
  Serial.println("Now you can read it in your phone!");
}

void loop() {
  iLoop++;
  if (iLoop%2 == 0) {
      // turn off the LED：
      if (DODEBUG)
        digitalWrite(ledPin, LOW);     
      if (iLoop%nTimesToSend==0){ //it is time to read temperture
        int t=read_temperture(5);
        t_Object=t/100;
        t_Ambient=t%100;
        if (DODEBUG) {
          Serial.print("Ambient = ");   Serial.print(t_Ambient); Serial.print("°C");
          Serial.print("\tObject = ");  Serial.print(t_Object);  Serial.println("°C");
        }
        if(t_Object-t_Ambient>FIRED_DVALUE){
          nIsFired=1;
          nTimesToSend=CHECK_INTERVAL_WHEN_FIRED;
          iLoop=0;
        }
        else {
          nIsFired=0;
          nTimesToSend=CHECK_INTERVAL_WHEN_NOT_FIRED;
          iLoop=0;
        }      
      
        if (deviceConnected) {  //if the sensor is connected by display, set the new temperture value into the specified characteristic
          char isFired='0';
          if (nIsFired){
            isFired='1';
          }          
          memset(buf,0,20);
          itoa(100*t_Object+t_Ambient,buf,10);  
          int l=strlen(buf);
          buf[l+1]=isFired;
          buf[l]=':';        
          buf[l+2]=0;              
          String s(buf);
          pCharacteristic->setValue(s);
          pCharacteristic->notify();
          if (DODEBUG){
            Serial.print("Notify value:");
            Serial.println(s);
          }
        }
      }

  } 
  else {
      // turn on the LED：
      if (DODEBUG)
        digitalWrite(ledPin, HIGH);
  }
  
  // disconnecting
  if (!deviceConnected && oldDeviceConnected) {
      delay(500);  // give the bluetooth stack the chance to get things ready
      pServer->startAdvertising();  // advertise again
      Serial.println("Start advertising");
      oldDeviceConnected = deviceConnected;
  }
  // connecting
  if (deviceConnected && !oldDeviceConnected) {
      // do stuff here on connecting
      oldDeviceConnected = deviceConnected;
  }

  delay(1000);
}

