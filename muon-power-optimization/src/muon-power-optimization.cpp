/* 
 * Project myProject
 * Author: Your Name
 * Date: 
 * For comprehensive documentation and examples, please visit:
 * https://docs.particle.io/firmware/best-practices/firmware-template/
 */

// Include Particle Device OS APIs
#include "Particle.h"
#include "JsonParserGeneratorRK.h"

// Let Device OS manage the connection to the Particle Cloud
SYSTEM_MODE(SEMI_AUTOMATIC);

// Run the application and system concurrently in separate threads
SYSTEM_THREAD(ENABLED);

// Show system, cloud connectivity, and application logs over USB
// View logs with CLI using 'particle serial monitor --follow'
SerialLogHandler logHandler(LOG_LEVEL_INFO);


PMIC pmic;
//FuelGauge fuel;

// declaration for functions
void publish_status();



const unsigned long WAKE_INTERVAL_MS = 10 * 60 * 1000; // 1 hour
const float LOW_BATTERY_THRESHOLD = 20; // 20%

//EAF moved from loop()
constexpr char const* batteryStates[] = {
  "unknown", "not charging", "charging",
  "charged", "discharging", "fault", "disconnected"
};

constexpr char const* powerSources[] = {
  "unknown", "vin", "usb host", "usb adapter",
  "usb otg", "battery"
};

// global variables
 bool powerGood = true;
// int batteryPercent = 100;



// setup() runs once, when the device is first turned on
void setup() {
  
  SystemPowerConfiguration conf;
    
    conf.powerSourceMaxCurrent(1500) 
        .powerSourceMinVoltage(3880) 
        .batteryChargeCurrent(900) 
        .batteryChargeVoltage(4112)
        .feature(SystemPowerFeature::PMIC_DETECTION)
        .auxiliaryPowerControlPin(PIN_INVALID).interruptPin(A7);

    int res = System.setPowerConfiguration(conf); 
    Log.info("setPowerConfiguration=%d", res);
    // returns SYSTEM_ERROR_NONE (0) in case of success

    // disable 3V3_AUX
    // SystemPowerConfiguration powerConfig = System.getPowerConfiguration();
    // powerConfig.auxiliaryPowerControlPin(PIN_INVALID).interruptPin(A7);
    // System.setPowerConfiguration(powerConfig);

    // reset the pmic
    //System.setPowerConfiguration(SystemPowerConfiguration()); 

    //EAF not sure if this is actually necessary
    //digitalWrite(A7, 0);    // turn 3V3_AUX off
    //pinMode(A6, INPUT);    // sets PMIC_INT pin as input
    //pinMode(A7, INPUT_PULLUP);    // sets PMIC_INT pin as input

  // pinMode(PMIC_INT, INPUT_PULLUP);     // Enable wake on PMIC_INT
  // pinMode(PIN_ENABLE_3V3_AUX, OUTPUT);
  // digitalWrite(PIN_ENABLE_3V3_AUX, LOW);  // Disable unused 3V3_AUX peripherals

  // pmic.disableCharging();  // Optional: manually control charging
  // pmic.enableCharging();   // Ensure charging is re-enabled

  // // Connect conditionally based on battery state
  // batteryPercent = fuel.getSoC();
  // powerGood = pmic.isPowerGood();

  // if (batteryPercent > LOW_BATTERY_THRESHOLD || powerGood) {
  //     Particle.connect(); // Avoid connecting if we're on low power
  // }

  // TODO need to add wrapper around connect, check batt level
  Particle.connect();
  waitFor(Particle.connected, 10000);  // wait until device connects or 10 seconds, whichever is faster
}

// loop() runs over and over again, as quickly as it can execute.
void loop() {
  // The core of your code will likely live here.

  // Example: Publish event to cloud every 10 seconds. Uncomment the next 3 lines to try it!
  // Log.info("Sending Hello World to the cloud!");
  // Particle.publish("Hello world!");
  // delay( 10 * 1000 ); // milliseconds and blocking - see docs for more info!

  // PMIC power(true);

  // Log.info("Current PMIC settings:");
  // Log.info("VIN Vmin: %u", power.getInputVoltageLimit());
  // Log.info("VIN Imax: %u", power.getInputCurrentLimit());
  // Log.info("Ichg: %u", power.getChargeCurrentValue());
  // Log.info("Iterm: %u", power.getChargeVoltageValue());

  int powerSource = System.powerSource();
  int batteryState = System.batteryState();
  float batterySoc = System.batteryCharge();

  Log.info("Power source: %s", powerSources[std::max(0, powerSource)]);
  Log.info("Battery state: %s", batteryStates[std::max(0, batteryState)]);
  Log.info("Battery charge: %f", batterySoc);
  
  delay(10000); // delay 

  // publish_status();
  // delay(10000); // Let publish finish, etc.



  powerGood = pmic.isPowerGood();
  //continue to publish value while charging or charged
  if (powerGood) {
  //if ((batteryState == "charged") || (batteryState == "charging")) {
  //if ((batterySoc > LOW_BATTERY_THRESHOLD) || (batteryState == 'charging')) {
    publish_status();
  }
  // go back to sleep
  else  {
    
    batterySoc = System.batteryCharge();

    if (batterySoc >= LOW_BATTERY_THRESHOLD) {
      publish_status();
    }
    
    // Prepare for sleep
    SystemSleepConfiguration config;
    config.mode(SystemSleepMode::ULTRA_LOW_POWER)
          .gpio(A7, FALLING)
          .duration(WAKE_INTERVAL_MS);

    System.sleep(config);

    //to mimic hibernation mode
    System.reset();   //reset the system, ULP continues execution where it left off

  }
}


void publish_status()
{
  //send publish only if cloud is connected
  if(Particle.connected() == TRUE)
  {

    PMIC power(true);
    
    int powerSource = System.powerSource();
    int batteryState = System.batteryState();
    float batterySoc = System.batteryCharge();
    
    //create JSON buffer and write values to it
    JsonWriterStatic<256> jw;		//creates a 256 byte buffer to write JSON to
    {
      JsonWriterAutoObject obj(&jw);						          //creates an object to pass JSON    

      jw.insertKeyValue("Power source: %s", powerSources[std::max(0, powerSource)]);
      jw.insertKeyValue("Battery state: %s", batteryStates[std::max(0, batteryState)]);
      jw.insertKeyValue("Battery charge: %f", batterySoc);      
    }

    //Publish data
    //waitFor(Particle.publish("Status Message", jw.getBuffer(), WITH_ACK), 10000);  // wait until cloud receives message or 10 seconds, whichever is faster
     bool bResult = Particle.publish("Status Message", jw.getBuffer(), WITH_ACK);
     delay(10000); // Let publish finish, etc.
    // waitFor(bResult, 10000);  // wait until cloud receives message or 10 seconds, whichever is faster

  } 
  else
  {
    //store and forward
  } 
}