#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
// Client Code
#include "BLEDevice.h"
#include "driver/gpio.h"

#define CHECK_INTERVAL_WHEN_NOT_FIRED 20
#define CHECK_INTERVAL_WHEN_FIRED 4
#define DODEBUG 1
#define TEMP_LOW 15 //40
#define TEMP_HIGH 20  //80
#define ALERT_TEMP 15 //TEMP_HIGH
#define BLINK_INTERVAL_SOLW 5
#define BLINK_INTERVAL_MID 3  //10
#define BLINK_INTERVAL_QUICK 1
#define min_temp  0
#define max_temp  500
#define CLEAR_ALERT_TIMES (10)  //(60*3)  //clear alert for 3 min when a button is pressed

//#define SERVICE_UUID        "2c4133b0-1d80-4a87-966b-e71e4cc3a577"
//#define CHARACTERISTIC_UUID "b1c1ee71-2ecd-4a6c-9159-59058addefcc"

// TODO: change the service UUID to the one you are using on the server side.
// The remote service we wish to connect to.
static BLEUUID serviceUUID("2c4133b0-1d80-4a87-966b-e71e4cc3a577");
// The characteristic of the remote service we are interested in.
static BLEUUID    charUUID("b1c1ee71-2ecd-4a6c-9159-59058addefcc");



static boolean doConnect = false;
static boolean connected = false;
static boolean doScan = false;
static BLERemoteCharacteristic* pRemoteCharacteristic;
static BLEAdvertisedDevice* myDevice=nullptr;
int t_Ambient;
int t_Object;
int nIsFired=0;
int iFoundNumber=0;
int iFoundNumber0=0;
int stilltime=0;
int iLoop=0; 
int angle=0;
int nTimesToSend=CHECK_INTERVAL_WHEN_FIRED;   // to control the measure freq
int blHaveGotTemperture=0;
int blinkTimes=1;
int stop_alert=0;
int alert_times=0;

void  rescan();
int getTemperture(const uint8_t* buf,int length);
int getIsFired(const uint8_t* buf,int length);
void stepMotor(const int sequence[][4], int steps, bool reverse);
void resetMotor(int n);
const int in1 = D0;  // to ULN2003  IN1
const int in2 = D1;  // to ULN2003  IN2
const int in3 = D2;  // to ULN2003  IN3
const int in4 = D3;  // to ULN2003  IN4
const int ledPin =  D10;  //to led 
#define  keyPin GPIO_NUM_9   //to key
// stepper motor's 8 step 
const int stepSequence[8][4] = {
  {1, 0, 0, 0},
  {1, 1, 0, 0},
  {0, 1, 0, 0},
  {0, 1, 1, 0},
  {0, 0, 1, 0},
  {0, 0, 1, 1},
  {0, 0, 0, 1},
  {1, 0, 0, 1}
};

// delay（for speed）
const int stepDelay = 1000;  // us

void setup_motor() {
  pinMode(in1, OUTPUT);
  pinMode(in2, OUTPUT);
  pinMode(in3, OUTPUT);
  pinMode(in4, OUTPUT);
  digitalWrite(in1, LOW);
  digitalWrite(in2, LOW);
  digitalWrite(in3, LOW);
  digitalWrite(in4, LOW);  

  resetMotor(512);
}


void test_motor() {
  // clockwise
  for (int i = 0; i < 512; i++) {  // 512 steps is a circle（28BYJ-48）
    stepMotor(stepSequence, 8,false);
  }
  delay(1000);  // 

  // ccw
  for (int i = 0; i < 100; i++) {
    stepMotor(stepSequence, 8, true);
  }
  delay(1000);  // 
}

//reset motor to zero by rotate n step
void resetMotor(int n) 
{
  int currentSteps=0;
  int button_state =0;
  while (currentSteps < n) {
    button_state=gpio_get_level(keyPin);
    if (button_state == 0) {
      break;
    } 
    stepMotor(stepSequence, 8,false); //ccw
    currentSteps++;
  }
  angle = 0; // reset motor angle
}
void rotate_motor_to(int toAngle)
{
  int ntimes=toAngle-angle;
  bool reverse=true;
  if (ntimes<0){
    reverse=!reverse;
    ntimes=-ntimes;
  }
  float ntimes2=1.0*ntimes/0.0879;
  ntimes=(int)(ntimes2/8);
  if (ntimes>0)
    for (int i = 0; i < ntimes; i++) {  // 512 steps is a circle（28BYJ-48）
      stepMotor(stepSequence, 8,reverse);
    }
  angle=toAngle;
}

// drive the stepper motor
void stepMotor(const int sequence[][4], int steps, bool reverse) {
  for (int i = 0; i < steps; i++) {
    int index = reverse ? steps - 1 - i : i;  
    digitalWrite(in1, sequence[index][0]);
    digitalWrite(in2, sequence[index][1]);
    digitalWrite(in3, sequence[index][2]);
    digitalWrite(in4, sequence[index][3]);
    delayMicroseconds(stepDelay);
  }
}

static void notifyCallback(
  BLERemoteCharacteristic* pBLERemoteCharacteristic,
  uint8_t* pData,
  size_t length,
  bool isNotify) {
    // TODO: add codes to handle the data received from the server, and call the data aggregation function to process the data

    // TODO: change the following code to customize your own data format for printing
    //Serial.print("Notify callback for characteristic ");
    Serial.print(pBLERemoteCharacteristic->getUUID().toString().c_str());
    //Serial.print(" of data length ");
    //Serial.println(length);
    //Serial.print("data: ");
    //Serial.write(pData, length);
    //Serial.println();
    int t=getTemperture(pData,length);  //parse the data
    if(t/100>min_temp && t/100<max_temp)
      t_Object=t/100;
    t=t%100;
    if(t>min_temp && t<100)
      t_Ambient=t;
    nIsFired=getIsFired(pData,length);
}

class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient* pclient) {

  }

  void onDisconnect(BLEClient* pclient) {
    connected = false;
    doScan=true;
    //myDevice=nullptr;
    iLoop=0;
    Serial.println("onDisconnect");
  }
};

bool connectToServer() {
    connected = false;
    if (!myDevice)
      return false;
    Serial.print("Forming a connection to ");
    
    Serial.print(myDevice->getAddress().toString().c_str());
    Serial.print(" ");
    Serial.println(myDevice->getName());
    BLEClient*  pClient  = BLEDevice::createClient();
    Serial.println(" - Created client");

    pClient->setClientCallbacks(new MyClientCallback());

    // Connect to the remove BLE Server.
    pClient->connect(myDevice);  // if you pass BLEAdvertisedDevice instead of address, it will be recognized type of peer device address (public or private)
    Serial.println(" - Connected to server");
    pClient->setMTU(517); //set client to request maximum MTU from server (default is 23 otherwise)

    // Obtain a reference to the service we are after in the remote BLE server.
    BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
    if (pRemoteService == nullptr) {
      Serial.print("Failed to find our service UUID: ");
      Serial.println(serviceUUID.toString().c_str());
      pClient->disconnect();
      return false;
    }
    Serial.println(" - Found our service");

    // Obtain a reference to the characteristic in the service of the remote BLE server.
    pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
    if (pRemoteCharacteristic == nullptr) {
      Serial.print("Failed to find our characteristic UUID: ");
      Serial.println(charUUID.toString().c_str());
      pClient->disconnect();
      return false;
    }
    Serial.println(" - Found our characteristic");

    // Read the value of the characteristic.
    if(pRemoteCharacteristic->canRead()) {
      //std::string 
      String value = pRemoteCharacteristic->readValue();
      Serial.print("The characteristic value was: ");
      Serial.println(value.c_str());
    }

    if(pRemoteCharacteristic->canNotify())
      pRemoteCharacteristic->registerForNotify(notifyCallback);

    connected = true;
    return true;
}
/**
 * Scan for BLE servers and find the first one that advertises the service we are looking for.
 */
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
  /**
   * Called for each advertising BLE server.
   */
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    Serial.print("BLE Advertised Device found: ");
    Serial.println(advertisedDevice.toString().c_str());
    iFoundNumber++;
    // We have found a device, let us now see if it contains the service we are looking for.
    if (advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(serviceUUID)) {

      BLEDevice::getScan()->stop();
      myDevice = new BLEAdvertisedDevice(advertisedDevice);
      doConnect = true;
      doScan = true;

    } // Found our server
  } // onResult
}; // MyAdvertisedDeviceCallbacks

void setup() {  
  //pinMode(keyPin, INPUT);
  pinMode(ledPin, OUTPUT);
  pinMode(keyPin, INPUT_PULLUP);
  if (DODEBUG){
   
    for(int i=0;i<3;i++)
    {
        digitalWrite(ledPin, HIGH);
        delay(500);
        digitalWrite(ledPin, LOW);
        delay(500);        
        digitalWrite(ledPin, HIGH);
    }
  }
  
  setup_motor();

  if (DODEBUG){
    for(int i=0;i<3;i++)
    {
        digitalWrite(ledPin, HIGH);
        delay(500);
        digitalWrite(ledPin, LOW);
        delay(500);        
        digitalWrite(ledPin, HIGH);
    }
  }
  
  Serial.begin(115200);
  Serial.println("Starting Arduino BLE Client application...");
  BLEDevice::init("");

  // Retrieve a Scanner and set the callback we want to use to be informed when we
  // have detected a new device.  Specify that we want active scanning and start the
  // scan to run for 5 seconds.
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setInterval(1349);
  pBLEScan->setWindow(449);
  pBLEScan->setActiveScan(true);
  pBLEScan->start(5, false);
} // End of setup.

// This is the Arduino main loop function.
void loop() {
  iLoop++;
  alert_times++; // alert_times+=(1000/blinkTimes);
  //test: rotate_motor_to(iLoop*10%90);

  if(connected==false){
    if (iFoundNumber>iFoundNumber0){
      iFoundNumber0=iFoundNumber;
      stilltime=0;
    }
    else{
      stilltime++;
      if (stilltime>10){
        rescan();     // do rescan firstly if needed    
      }
    }
  }
  
  if (iLoop%blinkTimes==0 && blinkTimes>1) {  
    digitalWrite(ledPin, HIGH);   
    if(connected == false){
      char buf[100];
      memset(buf,0,100);
      itoa(iFoundNumber,buf,10);
      String s(buf);
      Serial.print("\nHave found :");
      Serial.print(s);
      Serial.println(" BLE Servers.");
    }   
  } 
  else {        
    digitalWrite(ledPin, LOW);    
  }

  // If the flag "doConnect" is true then we have scanned for and found the desired
  // BLE Server with which we wish to connect.  Now we connect to it.  Once we are
  // connected we set the connected flag to be true.
  if (doConnect == true) {
    if (connectToServer()){
      Serial.println("We are now connected to the BLE Server.");         
    } else {
      Serial.println("We have failed to connect to the server; We will try again.");
    }    
    doConnect = false;  
  } 
  // If we are connected to a peer BLE Server, update the characteristic each time we are reached
  // with the current time since boot.
  blHaveGotTemperture=0;
  if (connected) {
    if (iLoop%blinkTimes==0 && pRemoteCharacteristic) { // if it time read 
      String value = pRemoteCharacteristic->readValue();
      Serial.print("The characteristic value was:");
      Serial.println(value.c_str());
      int length=value.length();
      int t=getTemperture((const uint8_t* )(value.c_str()),length);
      int t0=t/100;
      if(t0>min_temp && t0<max_temp){
        t_Object=t0;
        t=t%100;
        if(t>min_temp && t<max_temp){
          t_Ambient=t;
          blHaveGotTemperture=1;
        }
        else
          blHaveGotTemperture=0;
      }
      else
       blHaveGotTemperture=0;

      nIsFired=getIsFired((const uint8_t* )(value.c_str()),length);
      Serial.print("temperture of object=");Serial.println(t_Object);
      Serial.print("temperture of Ambient=");Serial.println(t_Ambient);
      
      //here do drive the stepper motor to indicate the temperture
      if( blHaveGotTemperture){       
        rotate_motor_to( (t_Object*5) %360);
        //here change the blink freqence according to the object temperture 
        if (t_Object<TEMP_LOW)
          blinkTimes=1;//no blink
        if (t_Object>=ALERT_TEMP){
          blinkTimes=BLINK_INTERVAL_MID;
        }
        else{
          blinkTimes=BLINK_INTERVAL_SOLW;
        }
      }
      
      int button_state = gpio_get_level(keyPin);
      if (button_state == 0) {
        stop_alert=1;
        alert_times=0;
      } 
      if (alert_times>=CLEAR_ALERT_TIMES){
        stop_alert=0;
        alert_times=0;
      }

      if(stop_alert==1){
        //do not alert
        blinkTimes=1;//no blink
      }
      /* we can write data back to server
      String newValue = "Time since boot: " + String(millis()/1000);
      Serial.println("Setting new characteristic value to \"" + newValue  + "\"");
      // Set the characteristic's value to be the array of bytes that is actually a string.
      pRemoteCharacteristic->writeValue(newValue.c_str(), newValue.length());
      */
    }
  } 
  else if(doScan){
    rescan();
    // this is just example to start scan after disconnect, most likely there is better way to do it in arduino
  } 
  
  //delay(1000/blinkTimes); // Delay a second between loops.
  delay(1000);
} // End of loop

void  rescan(){
  Serial.println("do scan again.");
  iFoundNumber=0;
  iFoundNumber0=0;
  stilltime=0;
  BLEDevice::getScan()->start(0);  
}

int getTemperture(const uint8_t* buf,int length){
	int i=0;
	char sbuf[20];
	int num=-1;
	memset(sbuf,0,20);
	while(buf[i]!=':') {
    sbuf[i]=buf[i];
    i++;
  }
  sbuf[i]=0;
	if(i>0){
    num=atoi(sbuf);
	}
	return num;	
}

int getIsFired(const uint8_t* buf,int length){
	int i=0;
	while(buf[i]!=':') {
    i++;
  }
  i++;
	if(buf[i]=='1'){
		return 1;	
	}
	else 
    return 0;	
}