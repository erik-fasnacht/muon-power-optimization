/* 
 * Project Muon + M-SoM Solar Powered Wake Sleep Example
 * Author: Erik Fasnacht
 * Date: 3/31/2025
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

// defines flag and address for writing and reading the flash PM_FLAG value
#define PM_FLAG 0x5555
#define EEPROM_ADDR 10

// Using Serial1 (RX/TX) for debugging logs and an external TTL serial to USB (FT232) converter
// is useful when testing sleep modes. Sleep causes USB serial to disconnect, and you will often
// lose the debug logs immediately after wake. With an external USB serial converter, your
// serial terminal stays connected so you get all log messages. If you don't have one, you can
// comment out the Serial1LogHandler and uncomment the SerialLogHandler to use USB.
Serial1LogHandler logHandler(115200);
//SerialLogHandler logHandler1(LOG_LEVEL_INFO);

// This is the maximum amount of time to wait for the cloud to be connected in
// milliseconds. This should be at least 5 minutes. If you set this limit shorter,
// on Gen 2 devices the modem may not get power cycled which may help with reconnection.
const std::chrono::milliseconds connectMaxTime = 6min;

// This is the minimum amount of time to stay connected to the cloud. You can set this
// to zero and the device will sleep as fast as possible, however you may not get 
// firmware updates and device diagnostics won't go out all of the time. Setting this
// to 10 seconds is typically a good value to use for getting updates.
const std::chrono::milliseconds cloudMinTime = 10s;

// How long to sleep
const std::chrono::milliseconds sleepTime = 60min;

// Maximum time to wait for publish to complete. It normally takes 20 seconds for Particle.publish
// to succeed or time out, but if cellular needs to reconnect, it could take longer, typically
// 80 seconds. This timeout should be longer than that and is just a safety net in case something
// goes wrong.
const std::chrono::milliseconds publishMaxTime = 3min;

// Maximum amount of time to wait for a user firmware download in milliseconds
// before giving up and just going back to sleep
const std::chrono::milliseconds firmwareUpdateMaxTime = 5min;

// sets interval for publishing when connected to power,
const std::chrono::milliseconds PUBLISH_PERIOD = 15min;

// These are the states in the finite state machine, handled in loop()
enum State {
    STATE_WAIT_CONNECTED = 0,
    STATE_PUBLISH = 1,
    STATE_PRE_SLEEP = 2,
    STATE_SLEEP = 3,
    STATE_FIRMWARE_UPDATE = 4
};

// sets battery threshold level
const float LOW_BATTERY_THRESHOLD = 15;    

// sets the value for PMIC_INT
const pin_t PMIC_INTERRUPT_PIN = A7;

// constant expression for battery states
constexpr char const* batteryStates[] = {                 
  "unknown", "not charging", "charging",
  "charged", "discharging", "fault", "disconnected"
};

// constant expression for power sources
constexpr char const* powerSources[] = {                 
  "unknown", "vin", "usb host", "usb adapter",
  "usb otg", "battery"
};

//function forward declarations
void publish_status();                                          // publishing app status
void firmwareUpdateHandler(system_event_t event, int param);    // OTA handler

// declaration for global variables
CloudEvent event;                         // define cloud event (publish)
unsigned long lastPublish;                // used for last publish time
State state = STATE_WAIT_CONNECTED;       // set the initial device state
unsigned long stateTime;                  // used for the timers between states
bool firmwareUpdateInProgress = false;    // used for determining firwmare is in progress
uint16_t powerModuleConfig = 0;

// setup() runs once, when the device is first turned on
void setup() {
  
  // power module hasn't been configured already
  // configuration writes value in flash so don't want to overly configure values, just once
  EEPROM.get(EEPROM_ADDR, powerModuleConfig);
  if (powerModuleConfig != PM_FLAG)
  {
    Log.info("Configuring Power Module");
    // set power module configuration
    SystemPowerConfiguration conf;
      conf.powerSourceMaxCurrent(1500)                                              // sets max current from power source (set to max)
        .powerSourceMinVoltage(3880)                                                // sets min batt voltage
        .batteryChargeCurrent(900)                                                  // sets batt charge current, size based off of solar panel
        .batteryChargeVoltage(4112)                                                 // sets batt charge voltage
        .feature(SystemPowerFeature::PMIC_DETECTION)                                // enables PMIC Detection
        .auxiliaryPowerControlPin(PIN_INVALID).interruptPin(PMIC_INTERRUPT_PIN);    // disables 3V3_AUX
    int res = System.setPowerConfiguration(conf); 
    Log.info("setPowerConfiguration=%d", res);
    // returns SYSTEM_ERROR_NONE (0) in case of success

    // write flag value to EEPROM
    EEPROM.put(EEPROM_ADDR, PM_FLAG);
    Log.info("Setting PM_FLAG");
  }
  

  // firmware update handler to delay sleep while an update is being downloaded
  System.on(firmware_update, firmwareUpdateHandler);
     
  // check battery level and state
  delay(5s);                                    // delay before reading from the PMIC, EAF @RICK without this delay I get whonky values from time to time
  float batterySoc = System.batteryCharge();    // read the battery SoC from PMIC
  int batteryState = System.batteryState();     // read the battery state from PMIC
  Log.info("Battery state: %s", batteryStates[std::max(0, batteryState)]);
  Log.info("Battery charge: %f", batterySoc);

  if ((batterySoc >= LOW_BATTERY_THRESHOLD) || ((batteryState == 2) || (batteryState == 3))) {
    // It's only necessary to turn cellular on and connect to the cloud. Stepping up
    // one layer at a time with Cellular.connect() and wait for Cellular.ready() can
    // be done but there's little advantage to doing so.
    Cellular.on();    
    Particle.connect();

    // set the stateTime variable to the current millis() time
    stateTime = millis();
      
  }

  // go back to sleep
  else  {
    Log.info("Fail to connect due to Battery charge: %f", batterySoc);      

    // Prepare for sleep
    SystemSleepConfiguration config;
    config.mode(SystemSleepMode::ULTRA_LOW_POWER)   // set sleep to ULP
      .gpio(PMIC_INTERRUPT_PIN, FALLING)            // wake of PMIC _INT (toggle low when changed noted)
      .duration(sleepTime);                         // wake on defined interval
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

  //local variables for the loop()
  int batteryState = System.batteryState();     
  float batterySoc = System.batteryCharge();
  Log.info("Battery state: %s", batteryStates[std::max(0, batteryState)]);
  Log.info("Battery charge: %f", batterySoc);

  switch(state) {
        case STATE_WAIT_CONNECTED:
            // Wait for the connection to the Particle cloud to complete, no break, want switch case to fall through
            if (Particle.connected()) {
                Log.info("connected to the cloud in %lu ms", millis() - stateTime);
                state = STATE_PUBLISH; 
                stateTime = millis(); 
            }
            // device didn't connect to the cloud
            else  {
              if (millis() - stateTime >= connectMaxTime.count()) {
                // Took too long to connect, go to sleep
                Log.info("failed to connect, going to sleep");
                state = STATE_SLEEP;
              }
              // break from switch statement
              break;
            }

        case STATE_PUBLISH:
            // read the battery state and SoC from PMIC
            batteryState = System.batteryState();     
            batterySoc = System.batteryCharge();
            Log.info("Battery state: %s", batteryStates[std::max(0, batteryState)]);
            Log.info("Battery charge: %f", batterySoc);
            //continue to publish value while charging or charged @ PUBLISH_PERIOD interval
            if ((batterySoc >= LOW_BATTERY_THRESHOLD) && ((batteryState == 2) || (batteryState == 3))) {
              if ((lastPublish == 0) || (millis() - lastPublish >= PUBLISH_PERIOD.count())) {
                lastPublish = millis();
                state = STATE_WAIT_CONNECTED;    // go back to STATE_WAIT_CONNECTED
                publish_status();
              }
            }
            // go to sleep after publish
            else  {
              // read the latest battery SoC from PMIC
              batterySoc = System.batteryCharge();
              Log.info("Battery charge: %f", batterySoc);
              // check to confirm batt level is above
              if (batterySoc >= LOW_BATTERY_THRESHOLD) {
                publish_status();
              }
              else {
                Log.info("Fail to publish due to Battery charge: %f", batterySoc);
              }
              // check to put device into  STATE_PRE_SLEEP
              if (millis() - stateTime < cloudMinTime.count()) {
                Log.info("waiting %lu ms before sleeping", (unsigned long)(cloudMinTime.count() - (millis() - stateTime)));
                state = STATE_PRE_SLEEP;
              }
              // cloudMinTime has elapsed, go into STATE_SLEEP
              else {
                state = STATE_SLEEP;
              }
            }
            // break from switch statement
            break;

        case STATE_PRE_SLEEP:
            // This delay is used to make sure firmware updates can start and diagnostics go out
            // It can be eliminated by setting cloudMinTime to 0 and sleep will occur as quickly as possible.
            if (millis() - stateTime >= cloudMinTime.count()) {
                state = STATE_SLEEP;
            }
            // break from switch statement
            break;

        case STATE_SLEEP:
            // check to determine if a firmware update has been detected
            if (firmwareUpdateInProgress) {
                Log.info("firmware update detected");
                state = STATE_FIRMWARE_UPDATE;
                stateTime = millis();
                // break from switch statement
                break;
            }
            // go to sleep
            Log.info("going to sleep for %ld seconds", (long) sleepTime.count());
            {
              
              // gracefully disconnect from network
              Particle.disconnect(CloudDisconnectOptions().graceful(true).timeout(5000));        
              Network.disconnect();         
              Network.off();
              Cellular.off();                                 
    
              // Prepare for sleep
              SystemSleepConfiguration config;
              config.mode(SystemSleepMode::ULTRA_LOW_POWER)   // set sleep to ULP
                .gpio(PMIC_INTERRUPT_PIN, FALLING)            // wake of PMIC _INT (toggle low when changed noted)
                .duration(sleepTime);                         // wake on defined interval
              System.sleep(config);

              // to mimic hibernation mode, reset device (re-run setup())
              System.reset();   // reset the system, ULP continues execution where it

            }
            // This is never reached; when the device wakes from sleep it will start over with setup() due to System.reset()
            break; 

        case STATE_FIRMWARE_UPDATE:
            // firwmare update is complete?, go to sleep
            if (!firmwareUpdateInProgress) {
                Log.info("firmware update completed");
                state = STATE_SLEEP;
            }
            // firmware update timed out
            else  {
              if (millis() - stateTime >= firmwareUpdateMaxTime.count()) {
                Log.info("firmware update timed out");
                state = STATE_SLEEP;
              }
            }
            // break from switch statement
            break;
    }
}

// function for publishing the status of the device
void publish_status() {

  // local variables
  int powerSource = System.powerSource();
  int batteryState = System.batteryState();
  float batterySoc = System.batteryCharge();                     

  // set objects within json
  particle::Variant obj;
  obj.set("Battery charge %:", batterySoc);
  obj.set("Battery state:", batteryStates[std::max(0, batteryState)]);
  obj.set("Power source:", powerSources[std::max(0, powerSource)]);
  
  // set event name, data, and publish()
  if (state == STATE_WAIT_CONNECTED){
    event.name("Powered");
  }
  else  {
    event.name("Sleep");
  }
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

// function for handling the firmware update
void firmwareUpdateHandler(system_event_t event, int param) {
    switch(param) {
        case firmware_update_begin:
            firmwareUpdateInProgress = true;
            break;

        case firmware_update_complete:
        case (int)firmware_update_failed:
            firmwareUpdateInProgress = false;
            break;
    }
}