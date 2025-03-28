/* 
 * Project myProject
 * Author: Your Name
 * Date: 
 * For comprehensive documentation and examples, please visit:
 * https://docs.particle.io/firmware/best-practices/firmware-template/
 */

// Include Particle Device OS APIs
#include "Particle.h"

// Let Device OS manage the connection to the Particle Cloud after the first connection
SYSTEM_MODE(SEMI_AUTOMATIC);

// System thread defaults to on in 6.2.0 and later and this line is not required
// Run the application and system concurrently in separate threads
#ifndef SYSTEM_VERSION_v620
  SYSTEM_THREAD(ENABLED);
#endif

// Show system, cloud connectivity, and application logs over USB
// View logs with CLI using 'particle serial monitor --follow'
SerialLogHandler logHandler(LOG_LEVEL_INFO);

// declaration for global functions and variables
//PMIC pmic;
CloudEvent event;             // define cloud event (publish)
void publish_status();        // function for publishing app status
unsigned long lastPublish;    // variable for last publish time

//global constants
const std::chrono::milliseconds PUBLISH_PERIOD = 1min;    // sets interval for publishing when connected to power, EAF change to 15min
const unsigned long WAKE_INTERVAL_MS = 5 * 60 * 1000;    // sets interval for waking from sleep, EAF change 10 to 60 (1 hour)
const float LOW_BATTERY_THRESHOLD = 15;                   // sets battery threshold level
constexpr char const* batteryStates[] = {                 // constant expression for battery states
  "unknown", "not charging", "charging",
  "charged", "discharging", "fault", "disconnected"
};
constexpr char const* powerSources[] = {                 // constant expression for power sources
  "unknown", "vin", "usb host", "usb adapter",
  "usb otg", "battery"
};

// setup() runs once, when the device is first turned on
void setup() {
  
  // set power module configuration
  SystemPowerConfiguration conf;
    conf.powerSourceMaxCurrent(1500)                              // sets max current from power source (set to max)
      .powerSourceMinVoltage(3880)                                // sets min batt voltage
      .batteryChargeCurrent(900)                                  // sets batt charge current, size based off of solar panel, 1/2 of actual power in case for shady conditions
      .batteryChargeVoltage(4112)                                 // sets batt charge voltage
      .feature(SystemPowerFeature::PMIC_DETECTION)                // enables PMIC Detection
      .auxiliaryPowerControlPin(PIN_INVALID).interruptPin(A7);    // disables 3V3_AUX
  int res = System.setPowerConfiguration(conf); 
  Log.info("setPowerConfiguration=%d", res);
  // returns SYSTEM_ERROR_NONE (0) in case of success

  delay(5000);                                  // delay for the PMIC to read the battery level
  float batterySoc = System.batteryCharge();    // read the battery SoC from PMIC

  // if battery is below level
  if (batterySoc >= LOW_BATTERY_THRESHOLD) {
      Particle.connect();
      waitFor(Particle.connected, 60000);  // wait until device connects or 10 seconds, whichever is faster
    }

  // go back to sleep
  else  {
    Log.info("Fail to connect due to Battery charge: %f", batterySoc);
    
    // Prepare for sleep
    SystemSleepConfiguration config;
    config.mode(SystemSleepMode::ULTRA_LOW_POWER)   // set sleep to ULP
      .gpio(A7, FALLING)                            // wake of PMIC _INT (toggle low when changed noted)
      .duration(WAKE_INTERVAL_MS);                  // wake on defined interval
    System.sleep(config);

    // to mimic hibernation mode, reset device (re-run setup())
    System.reset();   // reset the system, ULP continues execution where it left off
  }
}

// loop() runs over and over again, as quickly as it can execute.
void loop() {
  // The core of your code will likely live here.

  // Example: Publish event to cloud every 10 seconds. Uncomment the next 3 lines to try it!
  // Log.info("Sending Hello World to the cloud!");
  // Particle.publish("Hello world!");
  // delay( 10 * 1000 ); // milliseconds and blocking - see docs for more info!

  int powerSource = System.powerSource();       // read the power source from PMIC
  int batteryState = System.batteryState();     // read the battery state from PMIC
  float batterySoc = System.batteryCharge();    // read the battery SoC from PMIC

  // log values
  Log.info("Power source: %s", powerSources[std::max(0, powerSource)]);
  Log.info("Battery state: %s", batteryStates[std::max(0, batteryState)]);
  Log.info("Battery charge: %f", batterySoc);
  
  //continue to publish value while charging or charged @ PUBLISH_PERIOD interval
  if ((batteryState == 2) || (batteryState == 3)) {
    if (Particle.connected()) {
        if ((lastPublish == 0) || (millis() - lastPublish >= PUBLISH_PERIOD.count())) {
          lastPublish = millis();
          publish_status();
        }
    }
  }
  // go back to sleep
  else  {
    // get latest value of SoC
    batterySoc = System.batteryCharge();

    // check to confirm batt level is above
    if (batterySoc >= LOW_BATTERY_THRESHOLD) {
      if (Particle.connected()) {
        publish_status();
      }
    }
    else {
      Log.info("Fail to publish due to Battery charge: %f", batterySoc);
    }
    
    // Prepare for sleep
    SystemSleepConfiguration config;
    config.mode(SystemSleepMode::ULTRA_LOW_POWER)   // set sleep to ULP
      .gpio(A7, FALLING)                            // wake of PMIC _INT (toggle low when changed noted)
      .duration(WAKE_INTERVAL_MS);                  // wake on defined interval
    System.sleep(config);;

    // to mimic hibernation mode, reset device (re-run setup())
    System.reset();   // reset the system, ULP continues execution where it left off
  }
}

void publish_status() {

  // local variables
  int powerSource = System.powerSource();       // read the power source from PMIC
  int batteryState = System.batteryState();     // read the battery state from PMIC
  float batterySoc = System.batteryCharge();    // read the battery SoC from PMIC

  // set objects within json
  particle::Variant obj;
  obj.set("Power source:", powerSources[std::max(0, powerSource)]);
  obj.set("Battery state:", batteryStates[std::max(0, batteryState)]);
  obj.set("Battery charge %:", batterySoc);
  

  // set event name, data, and publish()
  event.name("Status Message");
  event.data(obj);
  Particle.publish(event);
  Log.info("publishing %s", obj.toJSON().c_str());

  // Wait while sending
  waitForNot(event.isSending, 60000);

  // logic for determining success/failure of publish()
  if (event.isSent()) {
    Log.info("publish succeeded");
    event.clear();
  }
  else 
  if (!event.isOk()) {
    Log.info("publish failed error=%d", event.error());
    event.clear();
  }
}