 /*********************************************************************
 This is programming code for the nRF52 based Bluefruit BLE boards
 
 The code uses heavily the Adafruit supplied Bluefruit BLE libraries !!
 Adafruit invests time and resources providing open source code,
 please support Adafruit and open-source hardware by purchasing
 products from Adafruit!

 MIT license, check LICENSE for more information
 All text must be included in any redistribution
*********************************************************************/

        //            PUBLIC DOMAIN VERSION

/* --------------------------------------------------------------------------
 *                     Notice this code should work with
 *  100% WAHOO proprietary Unknown Characteristic of Cycling Power Service
 * --------------------------------------------------------------------------
 *
 *  The code links a BLE Server (Zwift side) and a BLE Client (Wahoo side) with a bridge in between, the
 *  Feather nRF52 being man-in-the-middle. The bridge can control, filter and alter the interchanged data!
 *  The client-side scans and connects with the Wahoo relevant Cycling Trainer Service: CPS 
 *  plus the additional Wahoo proprietary characteristic and collects cyling power data...
 *  The Server-side advertises and enables connection with Cycling apps like Zwift and collects 
 *  relevant resistance data, it simulates as if an active Wahoo trainer is connected to Zwift or alike!
 *    
 *  Requirements: Zwift app or alike, Feather nRF52 board and a Wahoo trainer
 *  1) Upload and Run this code on the Feather nRF52
 *  2) Start the Serial Monitor to catch debugging info
 *  3) Start/Power On the Wahoo trainer  
 *  4) Feather and Trainer will pair as reported on the Serial Monitor
 *  5) Start Zwift on your computer or tablet
 *  6) Search on Zwift pairing screen for the Feather nRF52 a.k.a. "Wahoo Sim"
 *  7) Pair: Power and Controllable with "Wahoo Sim"
 *  8) Notice Wahoo does NOT support Speed nor Cadence, optionally pair with alternative
 *  9) Start the default Zwift ride or any ride you wish
 * 10) Optionally: Make Serial Monitor visible on top of the Zwift window 
 * 11) Hop on the bike and make it happen..
 * 12) Inspect the info presented by Serial Monitor and on the SSD1306.....
 *  
 */

#include <bluefruit.h>

// Compile toggle that determines Serial Monitor is switched ON (1) or OFF (0)
#define Serial_Monitor 1

// Libraries for use of I2C devices (Oled and VL6180X distance sensor)
#include <SPI.h>
#include <Wire.h>

// Necessary libraries for use of Oled display(s)
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Additional splash screen bitmap and icon(s) for Oled
#include "Adafruit_SSD1306_Icons.h" // needs to be in directory of main code
// Declarations for an SSD1306 128x64 display connected to I2C (SDA, SCL pins)
#define SCREEN_WIDTH 128            // SSD1306-OLED display width, in pixels
#define SCREEN_HEIGHT 64            // SSD1306-OLED display height, in pixels
#define OLED_RESET -1               // No reset pin on this OLED display
#define OLED_I2C_ADDRESS 0x3C       // I2C Address of OLED display

// Declare the display
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#include <avr/dtostrf.h>

/* Cycling Power Service
 * CP Service: 0x1818  
 * CP Characteristic: 0x2A63 (Measurement)
 * CP Characteristic: 0x2A65 (Feature)
 * CP Characteristic: 0x2A5D (Location) NOT supported in Wahoo legacy trainers
 * CP Characteristic: 0x2A66 (CYCLING_POWER_CONTROL_POINT)
 */
 // --------------------------------------------------------------------------------------------------------------------------------------------
BLEService        server_cps = BLEService(UUID16_SVC_CYCLING_POWER);
// Define how Server CPS is advertised
const uint16_t Server_appearance = 0x0480;  // 1152 -> Cycling
BLECharacteristic server_cpmc = BLECharacteristic(UUID16_CHR_CYCLING_POWER_MEASUREMENT);
BLECharacteristic server_cpfc = BLECharacteristic(UUID16_CHR_CYCLING_POWER_FEATURE);
BLECharacteristic server_cplc = BLECharacteristic(UUID16_CHR_SENSOR_LOCATION);
BLECharacteristic server_cpcp = BLECharacteristic(UUID16_CHR_CYCLING_POWER_CONTROL_POINT); // Indicate, write

// Hidden Wahoo Trainer characteristic to the Cycling Power Service !
// Unknown Characteristic is 128 bit:            A0 26 E0 05 - 0A 7D - 4A B3 - 97 FA - F1 50 0F 9F EB 8B
// Declare in Reversed order !!!
uint8_t CYCLING_POWER_WAHOO_TRAINER_Uuid[16] = {0x8B, 0xEB, 0x9F, 0x0F, 0x50, 0xF1, 0xFA, 0x97, 0xB3, 0x4A, 0x7D, 0x0A, 0x05, 0xE0, 0x26, 0xA0};
BLECharacteristic server_cpwt = BLECharacteristic(CYCLING_POWER_WAHOO_TRAINER_Uuid);

// Server helper class instance for Device Information Service
BLEDis bledis;

//-----------------------------------------------------------------------------------------------------------------------------------------------
BLEClientService        client_cps(UUID16_SVC_CYCLING_POWER);
BLEClientCharacteristic client_cpmc(UUID16_CHR_CYCLING_POWER_MEASUREMENT);
BLEClientCharacteristic client_cpfc(UUID16_CHR_CYCLING_POWER_FEATURE);
uint32_t client_cpfcDef = 0;
BLEClientCharacteristic client_cplc(UUID16_CHR_SENSOR_LOCATION);
uint16_t client_cplc_loc_value = 0;
BLEClientCharacteristic client_cpcp(UUID16_CHR_CYCLING_POWER_CONTROL_POINT); // Indicate, write
BLEClientCharacteristic client_cpwt(CYCLING_POWER_WAHOO_TRAINER_Uuid);

// CPS Wahoo Trainer Operation Codes in Decimal
const uint8_t unlock                     = 32;
const uint8_t setResistanceMode          = 64;
const uint8_t setStandardMode            = 65;
const uint8_t setErgMode                 = 66;
const uint8_t setSimMode                 = 67;
const uint8_t setSimCRR                  = 68;
const uint8_t setSimWindResistance       = 69;
const uint8_t setSimGrade                = 70;
const uint8_t setSimWindSpeed            = 71;
const uint8_t setWheelCircumference      = 72;
const uint8_t UnlockCommandBuf[3]        = {unlock, 0xEE, 0xFC}; // Unlock codes

#if Serial_Monitor
const char* Feature_str[] = { 
      "Pedal power balance supported",
      "Accumulated torque supported",
      "Wheel revolution data supported",
      "Crank revolution data supported",
      "Extreme magnatudes supported",
      "Extreme angles supported",
      "Top/bottom dead angle supported",
      "Accumulated energy supported",
      "Offset compensation indicator supported",
      "Offset compensation supported",
      "Cycling power measurement characteristic content masking supported",
      "Multiple sensor locations supported",
      "Crank length adj. supported",
      "Chain length adj. supported",
      "Chain weight adj. supported",
      "Span length adj. supported",
      "Sensor measurement context",
      "Instantaineous measurement direction supported",
      "Factory calibrated date supported",
      "Enhanced offset compensation supported" };
#endif

#define SERVICE_DEVICE_INFORMATION                  0x180A
#define CHARACTERISTIC_MANUFACTURER_NAME_STRING     0x2A29
#define CHARACTERISTIC_MODEL_NUMBER_STRING          0x2A24
#define CHARACTERISTIC_SERIAL_NUMBER_STRING         0x2A25

// Client Service Device Information
BLEClientService        client_diss(SERVICE_DEVICE_INFORMATION);
BLEClientCharacteristic client_disma(CHARACTERISTIC_MANUFACTURER_NAME_STRING);
BLEClientCharacteristic client_dismo(CHARACTERISTIC_MODEL_NUMBER_STRING);
BLEClientCharacteristic client_dissn(CHARACTERISTIC_SERIAL_NUMBER_STRING);

// -----------------------------------------------------------------
// Your hardware MAC/DEVICE ADDRESSES
// Laptop/Desktop Device Address that runs Zwift: [00:01:02:03:04:05]
// Little Endian: in reversed order !!!!
#define LAPTOPADDRESS {0x05,0x04,0x03,0x02,0x01,0x00}
// Trainer Wahoo KICKR Device Address [00:01:02:03:04:05]
// Little Endian: in reversed order !!!!
#define TRAINERADDRESS {0x05,0x04,0x03,0x02,0x01,0x00}
// -----------------------------------------------------------------

// Struct containing Device info to administer dis/connected devices
typedef struct
{
  char DevName[11];
  uint8_t DevAddr[6];
  char PeerName[32];
  uint16_t conn_handle;
  bool IsConnected;
} Device_info_t;
 
  // Initialize connectable device registration
  Device_info_t Trainer    = {"Trainer", TRAINERADDRESS, "", BLE_CONN_HANDLE_INVALID, false};
  Device_info_t Laptop     = {"Laptop", LAPTOPADDRESS, "", BLE_CONN_HANDLE_INVALID, false};
  Device_info_t Smartphone = {"Smartphone",{0x00}, "", BLE_CONN_HANDLE_INVALID, false};
// ------------------------------------------------------------
// set value for a flat road = 0% grade 
long RawgradeValue = 20000;
// Grade of a road is defined as a measure of the road's steepness as it rises and falls along its route
float gradePercentValue = 0.0;
//-----------------------------------------------------------------

// Timing variables
#define TIME_SPAN            3000 // Time span in millis 3000 = 3 second
unsigned long TimeInterval = 0;

/*
 * Setup ------------------------------------------------------------------
 */
void setup()
{
#if Serial_Monitor 
  Serial.begin(115200);
  while ( !Serial ) delay(100);   // for nrf52 with native usb, milliseconds
  Serial.println("        Test of Wahoo KICKR - Zwift Bridge");
#if defined(ARDUINO_NRF52840_FEATHER) 
  Serial.print("Processor: Feather nRF52840");
#endif   
#if defined(ARDUINO_NRF52832_FEATHER) 
  Serial.print("Processor: Feather nRF52832");
#endif
  Serial.println("");
  Serial.println("------------------ Version # 3.1 ----------------------");
  // Initialise the Bluefruit module
  Serial.println("Initialise the Bluefruit nRF52 module");
#endif

  // Start the show for the Oled display
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDRESS)) {
#if Serial_Monitor
  Serial.println(F("OLED display allocation failed!"));
#endif
  } else {
#if Serial_Monitor
    Serial.println(F("OLED display is running..."));
#endif
    // Load Oled with initial display buffer contents on the screen,
    // the SSD1306 library initializes with a Adafruit splash screen,
    // (respect or edit the splash.h in the library).
    display.display(); // Acknowledge Adafruit rights, license and efforts
    delay(500); // show some time
  }
  // Ready to show our own SIMCLINE splash screen
  display.clearDisplay(); // clean the oled screen
  display.setTextColor(SSD1306_WHITE);
  display.drawBitmap(24, 0, Mountain_bw_79x64, 79, 64, 1);
  display.display();
  delay(2000); // Take somewhat more time.....
  //Show Name and SW version on Oled
  ShowOnOledLarge("SIMCLINE", "Bridge", "Test 3.1", 500);

// ------------------------------------------------------
#if defined(ARDUINO_NRF52840_FEATHER)
// Allow for extra memory usage nRF52840 has plenty!
    Bluefruit.configUuid128Count(2); // 1 Service and 1 Char CPWT
    Bluefruit.configCentralBandwidth(BANDWIDTH_HIGH);
    Bluefruit.configPrphBandwidth(BANDWIDTH_HIGH);
    Bluefruit.configPrphConn(BLE_GATT_ATT_MTU_DEFAULT, BLE_GAP_EVENT_LENGTH_DEFAULT * 2, BLE_GATTS_HVN_TX_QUEUE_SIZE_DEFAULT * 2, BLE_GATTC_WRITE_CMD_TX_QUEUE_SIZE_DEFAULT * 2);
#endif
// -----------------------------------------------------------

#if defined(ARDUINO_NRF52832_FEATHER)
// Squeeze the memory to a minimum... to avoid nRF52832 out off memory errors
    Bluefruit.configUuid128Count(2); // 1 Service and 1 Char CPWT
    Bluefruit.configPrphBandwidth(BANDWIDTH_LOW); 
    Bluefruit.configCentralBandwidth(BANDWIDTH_LOW);
//    Bluefruit.configAttrTableSize(1024); // 1024
#endif
//
  // begin (Peripheral = 1, Central = 1)
  Bluefruit.begin(1, 1);
  
  // Set the device name (keep it short!) 
#if Serial_Monitor
  Serial.println("Setting Device Name to 'Wahoo Sim'");
#endif
  // Supported tx_power values depending on mcu:
  // - nRF52832: -40dBm, -20dBm, -16dBm, -12dBm, -8dBm, -4dBm, 0dBm, +3dBm and +4dBm.
  // - nRF52840: -40dBm, -20dBm, -16dBm, -12dBm, -8dBm, -4dBm, 0dBm, +2dBm, +3dBm, +4dBm, +5dBm, +6dBm, +7dBm and +8dBm.
  Bluefruit.setTxPower(4); // See above for supported values: +4dBm
  Bluefruit.setName("Wahoo Sim"); 

  Setup_Client_CPS();
  Client_Start_Scanning();
  while (Bluefruit.Scanner.isRunning()) { // do nothing else but scanning....
  }
#if Serial_Monitor 
  Serial.println("Scanning for Wahoo Cycle Power Service is stopped!");
#endif  
  TimeInterval = millis() + TIME_SPAN; // ADD just enough delay
  // wait enough time or go on when client is connected and set!
  while ( (TimeInterval > millis()) || (!Trainer.IsConnected) ) {
    }

  // Setup the Server Cycle Power Service
  // BLEService and BLECharacteristic classes initialized
  Setup_Server_CPS();
  Start_Server_Advertising();
  
  while (Bluefruit.Advertising.isRunning()) { // ONLY advertise!
  }
#if Serial_Monitor 
  Serial.println("Wahoo Simulated Advertising stopped! Paired to Zwift?");
#endif  
  TimeInterval = millis() + TIME_SPAN; // ADD just enough DELAY
  // wait enough time or go on when server is connected and set!
  while ( (TimeInterval > millis()) || (!Laptop.IsConnected) ) { 
    }
  // Only now enable Client (Wahoo) data streams...
  Client_Enable_Notify_Indicate(); 
#if Serial_Monitor   
  Serial.println("Up and running!");
#endif
} // End --------------------------------------------------------------------

void ShowIconsOnTopBar(void) {
  // Show Icons on Top Bar
  if (Trainer.IsConnected) { // show icon
    display.drawBitmap(112, 0, power_icon16x16, 16, 16, 1);
  }
  if (Laptop.IsConnected) { // show icon
    display.drawBitmap(0, 0, zwift_icon16x16, 16, 16, 1);
  }
  if (Smartphone.IsConnected) { // show icon Phone
    display.drawBitmap(0, 0, mobile_icon16x16, 16, 16, 1);
  }
}

void ShowOnOledLarge(const char* Line1, const char* Line2, const char* Line3, uint16_t Pause) {
  // Clear and set Oled to display 3 line info -> centered
  int pos = 1;
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  // ShowIconsOnTopBar();
  display.setTextSize(2);  // Large characters 11 pixels wide
  if (Line1) {
    pos = round( (127 - (12 * strlen(Line1))) / 2 );
    display.setCursor(pos, 2); // 16
    display.print(Line1);
  }
  if (Line2) {
    pos = round( (127 - (12 * strlen(Line2))) / 2 );
    display.setCursor(pos, 22); // 16
    display.print(Line2);
  }
  if (Line3) {
    pos = round( (127 - (12 * strlen(Line3))) / 2 );
    display.setCursor(pos, 44); // 16
    display.print(Line3);
  }
  display.display();
  delay(Pause);  // Pause indicated time in ms
}

void ShowSlopeTriangleOnOled(void) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  ShowIconsOnTopBar();
  display.setCursor(102, 10); //26
  display.setTextSize(2);
  display.print(F("%"));
  char tmp[7];
  dtostrf(gradePercentValue, 5, 1, tmp); // show sign only if negative
  display.setCursor(10, 6); // 22
  display.setTextSize(3);
  display.print(tmp);
  // The following calculations give more "weight" to lower grade values
  // (like: 1.2% or 0.5%), these will occur more often in practice and are not well
  // displayable at 128*64! --> 64 * 64 = 4096 and this value should not be
  // exceeded (4096/20) = 204.8
  int pos = 64 - int(sqrt(abs(204 * gradePercentValue))); // cast to int to get rid of decimals only now!
  if (gradePercentValue > 0) {
    display.fillTriangle( 1, 63, 127, 63, 127, pos, SSD1306_INVERSE);
  } else {
    display.fillTriangle( 127, 63, 1, 63, 0, pos, SSD1306_INVERSE);
  }
  // Draw the baseline to smooth small decimal values and show flat road case
  display.drawFastHLine(1, 63, 127, SSD1306_WHITE);
  display.display();
} // ---------------------------------------------------------------

void SetNewGradePercentValue(void) 
{
  gradePercentValue = float((RawgradeValue - 20000)/100);
#if Serial_Monitor
  Serial.printf("RawgradeValue: %05d - New Grade percent: %03.1f\n", RawgradeValue, gradePercentValue);
#endif
}

void Periph_adv_stop_callback(void) {
#if Serial_Monitor
  Serial.println(F("Advertising by peripheral is stopped after timeout..."));
#endif
}

/*
 * Client Functions
 */

void Setup_Client_CPS(void)
{
#if Serial_Monitor   
  Serial.println("Configuring Client (Wahoo) Cycle Power Service");
#endif

  // Initialize client_CPS client
  client_cps.begin();

  // Initialize client characteristics of client_CPS.
  // Note: Client Char will be added to the last service that is begin()ed.
  client_cplc.begin();

   // Initialize Control Point and set up Indicate callback for receiving responses (indicate!)
  client_cpcp.setIndicateCallback(client_cpcp_indicate_callback);
  client_cpcp.begin();

  // Initialize CP Feature characteristics of client_CPS.
  // Note: Client Char will be added to the last service that is begin()ed.
  client_cpfc.begin();

  // set up callback for receiving measurement
  client_cpmc.setNotifyCallback(client_cpmc_notify_callback);
  client_cpmc.begin();
  
  // set up callback for receiving response messages (indicate!)
  client_cpwt.setIndicateCallback(client_cpwt_indicate_callback);
  client_cpwt.begin();
  
  // Initialize DISS client.
  client_diss.begin();
  // Initialize some characteristics of the Device Information Service.
  client_disma.begin();
  client_dismo.begin();
  client_dissn.begin();

  // Increase Blink rate to different from PrPh advertising mode
  Bluefruit.setConnLedInterval(250);

  // Callbacks for Central
  Bluefruit.Central.setDisconnectCallback(Client_disconnect_callback);
  Bluefruit.Central.setConnectCallback(Client_connect_callback);
} // End

void Client_Start_Scanning(void)
{
#if Serial_Monitor   
  Serial.println("Scanning for Wahoo Cycle Power Service");
#endif
  /* Start Central Scanning
   * - Enable auto scan if disconnected
   * - Interval = 100 ms, window = 80 ms
   * - Don't use active scan
   * - Filter only accept client_CP service
   * - Start(timeout) with timeout = 0 will scan forever (until connected)
   */
  Bluefruit.Scanner.setRxCallback(client_scan_callback);
  Bluefruit.Scanner.restartOnDisconnect(true); // default is true !!! if false --> this stage we do not want to RESTART
  Bluefruit.Scanner.filterRssi(-70);   // original value of -80 , we want to scan only nearby peripherals, so get close to your device !!
  Bluefruit.Scanner.setInterval(160, 80); // in unit of 0.625 ms
  Bluefruit.Scanner.filterUuid(UUID16_SVC_CYCLING_POWER); // only invoke callback if this specific service is advertised 
  Bluefruit.Scanner.useActiveScan(true);
  Bluefruit.Scanner.start(0);  // 0 = Don't stop scanning or n = after n/100 seconds
} // End


void client_scan_callback(ble_gap_evt_adv_report_t* report)
{
/*
* Callback invoked when scanner pick up an advertising data
* @param report Structural advertising data
*/
  // Since we configure the scanner with filterUuid()
  // Scan callback only is invoked for device(s) with client_CPS service advertised
  // Connect to device with client_CPS service in advertising BUT
  
  // Check Trainer Device Address first
  uint8_t central_addr[6] = {0};
  memcpy(central_addr, report->peer_addr.addr, 6);
  if ( !(memcmp(central_addr, Trainer.DevAddr, 6) == 0) ) {
    return; // Keep scanning for the right one !!
  }
  
  if (Bluefruit.Scanner.isRunning()) { Bluefruit.Scanner.stop(); }
#if Serial_Monitor  
  Serial.println("Advertised Client Cycling Power Service is found! ... Raw data packet:");
  Serial.println(F("Timestamp Addr              Rssi Data"));
  Serial.printf("%09d ", millis());
  // MAC is in little endian --> print reverse
  Serial.printBufferReverse(report->peer_addr.addr, 6, ':');
  Serial.print(F(" "));
  Serial.print(report->rssi);
  Serial.print(F("  "));
  Serial.printBuffer(report->data.p_data, report->data.len, '-');
  Serial.println();
#endif  
  Bluefruit.Central.connect(report);
}// End

void Client_Enable_Notify_Indicate(void)
{
  // Reaching here means we are ready to go, let's enable notification on measurement chr
  if ( client_cpmc.enableNotify() )
  {
#if Serial_Monitor  
    Serial.println("Ready to receive Client Cycling Power Measurement values");
#endif
  } else {
#if Serial_Monitor  
    Serial.println("Couldn't enable notify for client_CP Measurement Characteristic. Increase DEBUG LEVEL for troubleshooting");
#endif
  }
  
  if ( client_cpcp.enableIndicate() )
  {
#if Serial_Monitor  
    Serial.println("Ready to receive Client Cycling Control Point Responses");
#endif
  } else {
#if Serial_Monitor  
    Serial.println("Couldn't enable indicate for client_CP Control Point Characteristic. Increase DEBUG LEVEL for troubleshooting");
#endif
  }
  if ( client_cpwt.enableIndicate() )
    {
  #if Serial_Monitor  
      Serial.println("Unlocking client_CPS Wahoo Trainer Characteristic --> Ready to receive Response Messages");
  #endif
      // Activate the trainer
      // client_CPWT needs: "write with response" !!
      client_cpwt.write_resp(UnlockCommandBuf, 3); // Unlock the client_CPS Wahoo Characteristic at the Wahoo trainer
      delay(300); // Give the trainer some time to wake up
    } else {
  #if Serial_Monitor  
      Serial.println("Couldn't enable indicate for Client Cycling Power Wahoo Trainer Characteristic. Increase DEBUG LEVEL for troubleshooting");
  #endif
    }
} // End

/*
 * Callback invoked when an connection is established
 * @param conn_handle
 */
void Client_connect_callback(uint16_t conn_handle)
{
  char central_name[32] = { 0 };
  uint8_t central_addr[6] = {0};
  // Get the reference to current connection
  BLEConnection* connection = Bluefruit.Connection(conn_handle);
  connection->getPeerName(central_name, sizeof(central_name));
  
  ble_gap_addr_t peer_address = connection->getPeerAddr();
  memcpy(central_addr, peer_address.addr, 6);
 
#if Serial_Monitor
  Serial.printf("Central connected to Peripheral device: %s conn handle [%d] MAC Address: ", central_name, conn_handle);
  PrintPeerAddress(central_addr);
  Serial.println();
#endif 

  Trainer.conn_handle = conn_handle;
  Trainer.IsConnected = true;
  memcpy(Trainer.PeerName, central_name, sizeof(central_name));
#if Serial_Monitor
    Serial.printf("Devname: %s Peername: %s conn_handle %d Isconnected: %2X\n", Trainer.DevName, Trainer.PeerName, Trainer.conn_handle, Trainer.IsConnected);
#endif
  ShowOnOledLarge("Pairing", (const char*)Trainer.DevName, "Done!", 500);
  Get_client_Diss(conn_handle);
#if Serial_Monitor
  Serial.print("Connecting and checking client_CPS Service ... ");
#endif
  // If client_CPS is not found, disconnect, resume scanning, and return
  if ( client_cps.discover(conn_handle) )
  {
#if Serial_Monitor
    Serial.printf("CPS Found! Handle: [%d]\n", conn_handle);
#endif
  } else {
#if Serial_Monitor
    Serial.println("Not Found!"); 
    Serial.println("Disconnecting since Client Cyling Power Service is mandatory!");
#endif
    // MANDATORY so disconnect since we couldn't find the client_CPS service
    Bluefruit.disconnect(conn_handle);
    // For Softdevice v6: after receiving a report (scan_callback), scanner will be paused (!)
    // We need to call Scanner resume() to continue scanning since we did not find a client_CPS Service!
    // Bluefruit.Scanner.resume();
    return;
  }
#if Serial_Monitor
  Serial.print("Discovering client_CP Measurement characteristic ... ");
#endif
  if ( !client_cpmc.discover() )
  {
    // Measurement chr is mandatory, if it is not found (valid), then disconnect
#if Serial_Monitor
    Serial.println("Not Found!");  
    Serial.println("Disconnecting since client_CP Measurement Characteristic is mandatory!");
#endif
    Bluefruit.disconnect(conn_handle);
    return;
  }
#if Serial_Monitor
  Serial.println("Found it!");
  
  Serial.print("Discovering client_CP Control Point characteristic ... ");
#endif
  if ( client_cpcp.discover() )
  {
    // CP Control Point chr is not mandatory
#if Serial_Monitor
    Serial.println("Found it!");  
#endif
  } else {
#if Serial_Monitor
  Serial.println("Not Found!");  
#endif
  }
#if Serial_Monitor 
  Serial.print("Discovering Client Wahoo Trainer specific characteristic (CPWT) ... ");
#endif
  if ( !client_cpwt.discover() ) //   CYCLING_POWER_WAHOO_TRAINER
  {
  #if Serial_Monitor 
    Serial.println("Not Found and disconnecting!"); 
    Serial.println("Wahoo Trainer Characteristic is mandatory!");
  #endif
    Bluefruit.disconnect(conn_handle);
    return;
  } else {   
#if Serial_Monitor
    Serial.println("Found it!"); 
#endif
  }
#if Serial_Monitor 
  Serial.print("Discovering Client Cycling Power Feature characteristic ... ");
#endif
  if ( client_cpfc.discover() )
  {
#if Serial_Monitor
    Serial.println("Found it!");
#endif    
  // Configure the Cycle Power Feature characteristic
  // See: https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.characteristic.cycling_power_feature.xml
  // Properties = Read
  // Min Len    = 1
  // Max Len    = 32
  //    B0:3    = UINT8 - Cycling Power Feature (MANDATORY)
  //      b0    = Pedal power balance supported; 0 = false, 1 = true
  //      b1    = Accumulated torque supported; 0 = false, 1 = true
  //      b2    = Wheel revolution data supported; 0 = false, 1 = true
  //      b3    = Crank revolution data supported; 0 = false, 1 = true
  //      b4    = Extreme magnatudes supported; 0 = false, 1 = true
  //      b5    = Extreme angles supported; 0 = false, 1 = true
  //      b6    = Top/bottom dead angle supported; 0 = false, 1 = true
  //      b7    = Accumulated energy supported; 0 = false, 1 = true
  //      b8    = Offset compensation indicator supported; 0 = false, 1 = true
  //      b9    = Offset compensation supported; 0 = false, 1 = true
  //      b10   = Cycling power measurement characteristic content masking supported; 0 = false, 1 = true
  //      b11   = Multiple sensor locations supported; 0 = false, 1 = true
  //      b12   = Crank length adj. supported; 0 = false, 1 = true
  //      b13   = Chain length adj. supported; 0 = false, 1 = true
  //      b14   = Chain weight adj. supported; 0 = false, 1 = true
  //      b15   = Span length adj. supported; 0 = false, 1 = true
  //      b16   = Sensor measurement context; 0 = force, 1 = torque
  //      b17   = Instantaineous measurement direction supported; 0 = false, 1 = true
  //      b18   = Factory calibrated date supported; 0 = false, 1 = true
  //      b19   = Enhanced offset compensation supported; 0 = false, 1 = true
  //   b20:21   = Distribtue system support; 0 = legacy, 1 = not supported, 2 = supported, 3 = RFU
  //   b22:32   = Reserved
  
  // Read 32-bit client_cpfc value
  const uint8_t CPFC_FIXED_DATALEN = 4;
  client_cpfcDef = client_cpfc.read32();
#if Serial_Monitor
  uint8_t cpfcData[CPFC_FIXED_DATALEN] = {(uint8_t)(client_cpfcDef & 0xff), (uint8_t)(client_cpfcDef >> 8), 
                                          (uint8_t)(client_cpfcDef >> 16), (uint8_t)(client_cpfcDef >> 24)};
  Serial.print("Power Feature 4 bytes: [ ");
  for (int i = 0; i < CPFC_FIXED_DATALEN; i++) {
    if ( i <= sizeof(cpfcData)) {
      Serial.printf("%02X ", cpfcData[i], HEX);
    }
  }
  Serial.println("] ");
  for (int i = 0; i < 20; i++) {
    if ( client_cpfcDef & (1 << i) )
      {
       Serial.println(Feature_str[i]);
      }
    }
#endif
  } else {
#if Serial_Monitor
    Serial.println("NOT Found!");
#endif
  }
#if Serial_Monitor
  Serial.print("Discovering Client Power Sensor Location characteristic ... ");
#endif
  if ( client_cplc.discover() )
  {
#if Serial_Monitor
    Serial.println("Found it!");
#endif    
  // The Sensor Location characteristic
  // See: https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.characteristic.sensor_location.xml
  // Properties = Read
  // Min Len    = 1
  // Max Len    = 1
  //  B0:1      = UINT16 - Sensor Location   
    // power sensor location value is 16 bit
#if Serial_Monitor
    const char* power_str[] = { "Other", "Top of shoe", "In shoe", "Hip", "Front wheel", "Left crank", "Right crank", "Left pedal",
    "Right pedal", "Front hub", "Rear dropout", "Chainstay", "Rear wheel", "Rear hub", "Chest", "Spider", "Chain ring"};
#endif
    // Read 16-bit client_cplc value from peripheral
    client_cplc_loc_value = client_cplc.read16();
#if Serial_Monitor
    uint8_t loc_value = (uint8_t)client_cplc_loc_value;
    Serial.print("Power Location Sensor: ");
    Serial.printf("Loc#: %d %s\n", loc_value, power_str[loc_value]);
#endif
  } else {
#if Serial_Monitor
    Serial.println("NOT Found!");
#endif
  }
   //Client_CPS_IsConnected = true;
} // End client_connect_callback


void Get_client_Diss(uint16_t conn_handle)
{
  // If diss is not found then go on.... NOT FATAL !
  if (!client_diss.discover(conn_handle) ) { return; }
#if Serial_Monitor  
  Serial.print(F("Found Device Information: \n"));
#endif
  char dissDataBuf[20]; // Max len = 20 !!!
    //  1
  if ( client_disma.discover() ) {
      // read and print out Manufacturer
      memset(dissDataBuf, 0, sizeof(dissDataBuf));
      if ( client_disma.read(dissDataBuf, sizeof(dissDataBuf)) ) {
#if Serial_Monitor
        Serial.print("Manufacturer:  ");
        Serial.println(dissDataBuf);
#endif
      }
  }
    //  2
  if ( client_dismo.discover() ) {
      // read and print out Model Number
      memset(dissDataBuf, 0, sizeof(dissDataBuf));
      if ( client_dismo.read(dissDataBuf, sizeof(dissDataBuf)) ) {
#if Serial_Monitor
        Serial.print("Model Number:  ");
        Serial.println(dissDataBuf);
#endif
      }
  }
    //  3
  if ( client_dissn.discover() ) {
      // read and print out Serial Number
      memset(dissDataBuf, 0, sizeof(dissDataBuf));
      if ( client_dissn.read(dissDataBuf, sizeof(dissDataBuf)) ) {
#if Serial_Monitor
        Serial.print("Serial Number: ");
        Serial.println(dissDataBuf);
#endif
      }
  }
} // End DISS

/**
 * Callback invoked when a connection is dropped
 * @param conn_handle
 * @param reason is a BLE_HCI_STATUS_CODE which can be found in ble_hci.h
 */
void Client_disconnect_callback(uint16_t conn_handle, uint8_t reason)
{
  Trainer.conn_handle = BLE_CONN_HANDLE_INVALID;
  Trainer.IsConnected = false;
  ShowOnOledLarge("Lost!", (const char*)Trainer.DevName, "Connection", 500);
#if Serial_Monitor
  Serial.printf("Client Disconnected Peripheral Device: %s handle [%d] reason = 0x\n", Trainer.PeerName, conn_handle, reason, HEX);
#endif
}


/**
 * Hooked callback that triggered when a measurement value is sent from peripheral
 * @param chr   Pointer client characteristic that even occurred,
 *              in this example it should be client_cpcp
 * @param data  Pointer to received data
 * @param len   Length of received data
 */
 // https://github.com/sputnikdev/bluetooth-gatt-parser/blob/master/src/main/resources/gatt/characteristic/org.bluetooth.characteristic.cycling_power_control_point.xml

void client_cpcp_indicate_callback(BLEClientCharacteristic* chr, uint8_t* data, uint16_t len)
{
/*
static const unsigned MAX_CONTROL_BYTES  = 5;
const uint8_t RESPONSE_SUCCESS = 1;
const uint8_t RESPONSE_OP_CODE_NOT_SUPPORTED = 2;
const uint8_t RESPONSE_INVALID_PARAMETER = 3;
const uint8_t RESPONSE_OPERATION_FAILED = 4;
const uint8_t OPCODE_RESPONSE_CODE = 32;
Len = 3
cpcpData[0] = Response Code = 0x20 // 32 Decimal
cpcpData[1] = OpCode
cpcpData[2] = responseValue
Len = 5
cpcpData[3] = (uint8_t)(responseParameter & 0xFF);
cpcpData[4] = (uint8_t)(responseParameter >> 8);
*/
  // NO TREATMENT OF RESPONSE !!!!!
  // Send Client's (Wahoo) response message to the Server (Zwift)
  server_cpcp.write(data, len); // Just pass on and process later!
  
#if Serial_Monitor  
  uint8_t cpcpDataLen = (uint8_t)len;
  uint8_t cpcpData[cpcpDataLen]= {}; 
  // Transfer first the contents of data to buffer (array of chars)
  Serial.print("Raw client Control Point Data: [ "); 
  for (int i = 0; i < cpcpDataLen; i++) {
    if ( i <= sizeof(cpcpData)) {
      cpcpData[i] = *data++;
      Serial.printf("%02X ", cpcpData[i], HEX);
    }
  }
  Serial.print("] ");
#endif
}

/**
 * Hooked callback that triggered when a measurement value is sent from peripheral
 * @param chr   Pointer client characteristic that even occurred,
 *              in this example it should be client_cpmc
 * @param data  Pointer to received data
 * @param len   Length of received data
 */
void client_cpmc_notify_callback(BLEClientCharacteristic* chr, uint8_t* data, uint16_t len)
{
/*{     (uint8_t)(cpmcDef & 0xff), (uint8_t)(cpmcDef >> 8),  // flags 
                             (uint8_t)(powerOut & 0xff), (uint8_t)(powerOut >> 8),  // inst. power 
                                                                                0,  // bal
                                                                             0, 0,  // torque
                                                                       0, 0, 0, 0,  // cum. rev
                                                                             0, 0,  // wheel time
                                                                             0, 0,  // cum. crank
                                                                             0, 0,  // crank time
                                                                             0, 0,  // max force
                                                                             0, 0,  // min force
                                                                             0, 0,  // max tor
                                                                             0, 0,  // min tor
                                                                             0, 0,  // max ang
                                                                             0, 0,  // min ang
                                                                             0, 0,  // tdc
                                                                             0, 0,  // bdc
                                                                             0, 0 }; // total energy
*/
  // Client (Wahoo) CPMC data is tranferred to the Server (Zwift)
  server_cpmc.notify(data, len); // Just pass on and process later!
  
#if Serial_Monitor
  uint8_t cpmcDataLen = (uint8_t)len;
  uint8_t cpmcDataBuf[cpmcDataLen] = {};
  // Transfer first the contents of data to buffer (array of chars)
  Serial.print("Raw client_CPS Data: [ ");
  for (int i = 0; i < cpmcDataLen; i++) {
    if ( i <= sizeof(cpmcDataBuf)) {
      cpmcDataBuf[i] = *data++;
      Serial.printf("%02X ", cpmcDataBuf[i], HEX);
    }
  }
  Serial.print("] ");
  long PowerValue = 0;
  uint8_t lsb_InstantaneousPower = cpmcDataBuf[2]; // Instantaneous Power LSB
  // POWER is stored in 2 bytes !!!
  uint8_t msb_InstantaneousPower = (cpmcDataBuf[3] & 0xFF); //  
  PowerValue = lsb_InstantaneousPower + msb_InstantaneousPower * 256;
  Serial.printf("Power Value: %4d\n", PowerValue);
#endif 
} // End cpmc_notify_callback

/**
 * Hooked callback that triggered when a measurement value is sent from peripheral
 * @param chr   Pointer client characteristic that even occurred,
 *              in this example it should be client_cpmc
 * @param data  Pointer to received data
 * @param len   Length of received data
 */
void client_cpwt_indicate_callback(BLEClientCharacteristic* chr, uint8_t* data, uint16_t len)
{
  // The receipt of operation settings is acknowledged by the trainer: handle it
  // Send Client's (Wahoo) acknowldege message to the Server (Zwift)
  server_cpwt.write(data, len); // Just pass on and process later!

#if Serial_Monitor 
  uint8_t RespBufferLen = (uint8_t)len;
  uint8_t RespBuffer[RespBufferLen] = {}; // It is max 6 bytes long
  // Transfer first the contents of data to buffer (array of chars)
  Serial.print("Response: [ "); 
  for (int i = 0; i < RespBufferLen; i++) {
    if ( i <= sizeof(RespBuffer)) {
      RespBuffer[i] = *data++;
      Serial.printf("%02X ", RespBuffer[i], HEX);
    }
  }
  Serial.print(" ]");
  // Do something with the response string
  switch(RespBuffer[1]) {
    case setSimMode: { 
      Serial.print(" setSimMode");
      break; }
    case setSimGrade: {
      Serial.print(" setSimGrade");
      break; }
    default: {
      Serial.print(" ? unknown ?");  
    }
  }
  if (  RespBuffer[0] = 0x01 ) {
    Serial.println(" -> Successful");
  } else {
    Serial.println(" -> Failed");
  }
#endif
} // end

/*
 * Server Functions
 */
void Start_Server_Advertising(void)
{
#if Serial_Monitor 
  Serial.println("Start Advertising Wahoo Simulated as Wahoo Sim... pair with Zwift!");
#endif
  if (Bluefruit.Advertising.isRunning())
    Bluefruit.Advertising.stop();
//
  delay(100);
  Bluefruit.Advertising.clearData();
  Bluefruit.ScanResponse.clearData();
  delay(100);
// 
  Bluefruit.setName("Wahoo Sim");
  // Prepare a full Advertising packet
  Bluefruit.Advertising.addAppearance(Server_appearance);
  Bluefruit.Advertising.setType(BLE_GAP_ADV_TYPE_CONNECTABLE_SCANNABLE_UNDIRECTED); // Type used by the TACX Neo
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.setStopCallback(Periph_adv_stop_callback);
  // Include Server CPS no others
  Bluefruit.Advertising.addService(server_cps);  // defined above
  // Include Name
  // Bluefruit.Advertising.addName();
  // Secondary Scan Response packet (optional)
  // Since there is no room for 'Name' in Advertising packet
  // A.K.A. secondary response packet
  Bluefruit.ScanResponse.addName();
  /* Start Advertising
   * - Enable auto advertising if disconnected
   * - Interval:  fast mode = 20 ms, slow mode = 152.5 ms
   * - Timeout for fast mode is 30 seconds
   * - Start(timeout) with timeout = 0 will advertise forever (until connected)
   * 
   * For recommended advertising interval
   * https://developer.apple.com/library/content/qa/qa1931/_index.html   
   */
  Bluefruit.Advertising.restartOnDisconnect(true); // true is default --> auto restart
  Bluefruit.Advertising.setInterval(32, 244);    // in unit of 0.625 ms
  Bluefruit.Advertising.setFastTimeout(30);      // number of seconds in fast mode
  Bluefruit.Advertising.start(0);                // 0 = Don't stop advertising after n seconds  
}

void Setup_Server_CPS(void)
{
#if Serial_Monitor 
  Serial.println("Configuring the (Zwift) Server Cycle Power Service");
#endif
// Default and fixed settings
// Server CPWT Cycling Power Wahoo Trainer Char data field -------------------------------------------------------------------------------------
const uint16_t CPWT_MAX_DATALEN = 20;  // Stay on the safe side: it usually is no longer than 4 or 6 bytes 
// Server CPFC Cycle Power Feature characteristic
const uint16_t CPFC_FIXED_DATALEN = 4;  // Notice: set to fixed for this Characteristic 4 bytes = 32 bits only !!!!!
// Server CPCP Cyling Power Control Point characteristic
const uint16_t CPCP_MAX_DATALEN = 5;
// Server CPMC Cycling Power Measurement Characteristic
const uint16_t cpmcDef = 0x00; // Cycle power measurement config flags field is zero
const uint16_t CPMC_MAX_DATALEN = 20; // Notice: set to MAX for this Characteristic, the flags field value determines the actual cpmcDataLen !!!!!
const uint16_t Power_out = 50; // Power in Watts ( an arbitrary value, just to start with or to show our presence ;-) )
const uint8_t cpmcDataLen = 4; // First dataset contains cpmcDef = zero and only Instantaneous power data, so len = 4 !!!
const uint8_t cpmcData[cpmcDataLen] = {
                (uint8_t)(cpmcDef & 0xff),  // Flags Field
                (uint8_t)(cpmcDef >> 8),    // Flags Field
                (uint8_t)(Power_out & 0xff),// Instantaneous Power
                (uint8_t)(Power_out >> 8)}; // Instantaneous Power                               
// ---------------------------------------------------------------------------------------------------------------------------------------------
 
  server_cps.begin();

  // Configure the Cycle Power Measurement characteristic
  // See: https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.characteristic.cycling_power_measurement.xml
  // Properties = Notify
  // Min Len    = 2
  // Max Len    = 34
  //    B0:1    = UINT16  - Flag (MANDATORY)
  //      b0    = Pedal power balance present; 0 = false, 1 = true
  //      b1    = Pedal power balance reference; 0 = unknown, 1 = left
  //      b2    = Accumulated torque present; 0 = false, 1 = true
  //      b3    = Accumulated torque source; 0 = wheel, 1 = crank
  //      b4    = Wheel revolution data present; 0 = false, 1 = true
  //      b5    = Crank revolution data present; 0 = false, 1 = true
  //      b6    = Extreme force magnitudes present; 0 = false, 1 = true
  //      b7    = Extreme torque magnitues present; 0 = false, 1 = true
  //      b8    = Extreme angles present; 0 = false, 1 = true
  //      b9    = Top dead angle present; 0 = false, 1 = true
  //      b10   = Bottom dead angle present; 0 = false, 1 = true
  //      b11   = Accumulated energy present; 0 = false, 1 = true
  //      b12   = Offset compensation indicator; 0 = false, 1 = true
  //      b13   = Reseved
  //      b14   = n/a 
  //      b15   = n/a
  //    B2:3    = SINT16 - Instantaneous power, Watts (decimal)
  //    B4      = UINT8 -  Pedal power balance, Percent (binary) 1/2
  //    B5:6    = UINT16 - Accumulated torque, Nm; res (binary) 1/32
  //    B7:10   = UINT32 - Cumulative wheel revolutions, (decimal)
  //    B11:12  = UINT16 - Last wheel event time, second (binary) 1/2048
  //    B13:14  = UINT16 - Cumulative crank revolutions, (decimal)
  //    B15:16  = UINT16 - Last crank event time, second (binary) 1/1024 
  //    B17:18  = SINT16 - Max force magnitude, Newton (decimal)
  //    B19:20  = SINT16 - Min force magnitude, Newton (decimal)
  //    B21:22  = SINT16 - Max torque magnitude, Nm (binary) 1/1024
  //    B23:24  = SINT16 - Min torque magnitude, Nm (binary) 1/1024
  //    B25:26  = UINT12 - Max angle, degree (decimal)
  //    B27:28  = UINT12 - Min angle, degree (decimal)
  //    B29:30  = UINT16 - Top dead spot angle, degree (decimal)
  //    B31:32  = UINT16 - Bottom dead spot angle, degree (decimal)
  //    B33:34  = UINT16 - Accumulated energy, kJ (decimal)
  
  server_cpmc.setProperties(CHR_PROPS_NOTIFY);  // type is set to "notify"
  server_cpmc.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS); // readAccess, writeAccess
  server_cpmc.setMaxLen(CPMC_MAX_DATALEN); // Notice that this value is set to MAX but the actual length varies!
  server_cpmc.setCccdWriteCallback(server_cccd_callback);  // Optionally capture CCCD updates
  server_cpmc.begin();
  server_cpmc.notify(cpmcData, cpmcDataLen); // Just send a first value string to the server to show we are there!

  // Wahoo Trainer hidden characteristic ------------------------------------------------------------------
  server_cpwt.setProperties(CHR_PROPS_INDICATE | CHR_PROPS_WRITE); // Indicate and Write !!
  server_cpwt.setPermission(SECMODE_OPEN, SECMODE_OPEN); // readAccess, writeAccess DO NOT SET: SECMODE_NO_ACCESS !
  server_cpwt.setMaxLen(CPWT_MAX_DATALEN); // The charactersitic's data set varies in length
  server_cpwt.setCccdWriteCallback(server_cccd_callback);  // Optionally capture CCCD updates
  server_cpwt.begin();
  server_cpwt.setWriteCallback(server_cpwt_callback); // Respond to events with "Write with Response" !!

  server_cpcp.setProperties(CHR_PROPS_INDICATE | CHR_PROPS_WRITE); // Indicate and Write !!
  server_cpcp.setPermission(SECMODE_OPEN, SECMODE_OPEN); // readAccess, writeAccess DO NOT SET: SECMODE_NO_ACCESS !
  server_cpcp.setMaxLen(CPCP_MAX_DATALEN); // The charactersitic's data set varies in length
  server_cpcp.setCccdWriteCallback(server_cccd_callback);  // Optionally capture CCCD updates
  server_cpcp.begin();
  server_cpcp.setWriteCallback(server_cpcp_callback); // Respond to events with "Write with Response" !!

 // Configure the Cycle Power Feature characteristic
  // See: https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.characteristic.cycling_power_feature.xml
  // Properties = Read
  // Min Len    = 1
  // Max Len    = 32
  //    B0:3    = UINT8 - Cycling Power Feature (MANDATORY)
  //      b0    = Pedal power balance supported; 0 = false, 1 = true
  //      b1    = Accumulated torque supported; 0 = false, 1 = true
  //      b2    = Wheel revolution data supported; 0 = false, 1 = true
  //      b3    = Crank revolution data supported; 0 = false, 1 = true
  //      b4    = Extreme magnatudes supported; 0 = false, 1 = true
  //      b5    = Extreme angles supported; 0 = false, 1 = true
  //      b6    = Top/bottom dead angle supported; 0 = false, 1 = true
  //      b7    = Accumulated energy supported; 0 = false, 1 = true
  //      b8    = Offset compensation indicator supported; 0 = false, 1 = true
  //      b9    = Offset compensation supported; 0 = false, 1 = true
  //      b10   = Cycling power measurement characteristic content masking supported; 0 = false, 1 = true
  //      b11   = Multiple sensor locations supported; 0 = false, 1 = true
  //      b12   = Crank length adj. supported; 0 = false, 1 = true
  //      b13   = Chain length adj. supported; 0 = false, 1 = true
  //      b14   = Chain weight adj. supported; 0 = false, 1 = true
  //      b15   = Span length adj. supported; 0 = false, 1 = true
  //      b16   = Sensor measurement context; 0 = force, 1 = torque
  //      b17   = Instantaineous measurement direction supported; 0 = false, 1 = true
  //      b18   = Factory calibrated date supported; 0 = false, 1 = true
  //      b19   = Enhanced offset compensation supported; 0 = false, 1 = true
  //   b20:21   = Distribtue system support; 0 = legacy, 1 = not supported, 2 = supported, 3 = RFU
  //   b22:32   = Reserved

  // Notice: client_cpfcDef is set by the client_cpfc
  uint8_t cpfcData[CPFC_FIXED_DATALEN] = {(uint8_t)(client_cpfcDef & 0xff), (uint8_t)(client_cpfcDef >> 8), 
                                          (uint8_t)(client_cpfcDef >> 16), (uint8_t)(client_cpfcDef >> 24)};
  server_cpfc.setProperties(CHR_PROPS_READ);
  server_cpfc.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  server_cpfc.setFixedLen(CPFC_FIXED_DATALEN);
  server_cpfc.begin();
  server_cpfc.write(cpfcData, CPFC_FIXED_DATALEN);

  // Configure the Sensor Location characteristic
  // See: https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.characteristic.sensor_location.xml
  // Properties = Read
  // Min Len    = 1
  // Max Len    = 1
  //  B0:1      = UINT16 - Sensor Location
  //  Dec.
  //      0     = Other
  //      1     = Top of shoe
  //      2     = In shoe
  //      3     = Hip
  //      4     = Front wheel
  //      5     = Left crank
  //      6     = Right crank
  //      7     = Left pedal
  //      8     = Right pedal
  //      9     = Front hub
  //      10    = Rear dropout
  //      11    = Chainstay
  //      12    = Rear wheel
  //      13    = Rear hub
  //      14    = Chest
  //      15    = Spider
  //      16    = Chain ring
  //  17:255    = Reserved
   
  server_cplc.setProperties(CHR_PROPS_READ);
  server_cplc.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  server_cplc.setFixedLen(1);
  server_cplc.begin();
  server_cplc.write16(12);  // Set the characteristic to 'Rear wheel' (12) write8 --> write16

  if (Bluefruit.setAppearance(Server_appearance))
  {
#if Serial_Monitor
     Serial.printf("Setting Appearance to [%d] Generic: Cycling\n", Server_appearance);
#endif
  }

  // Set the connect/disconnect callback handlers for Peripherals
  Bluefruit.Periph.setConnectCallback(Periph_connect_callback); 
  Bluefruit.Periph.setDisconnectCallback(Periph_disconnect_callback);

  // Configure and Start the Device Information Service
#if Serial_Monitor
  Serial.println("Configuring the Device Information Service");
#endif
  bledis.setManufacturer("Wahoo Fitness"); 
  bledis.setModel("KICKR");
  // Notice that the Firmware Revision string is default set to 
  // the value of the Feather-nRF52 Board being used!
  bledis.setFirmwareRev("0.0.1");
  bledis.setSerialNum("1234");
  bledis.setHardwareRev("0.0.1");
  bledis.setSoftwareRev("0.0.1");
  bledis.begin();
} // End

void server_cpwt_callback(uint16_t conn_hdl, BLECharacteristic* chr, uint8_t* data, uint16_t len)
{
  // Server CPWT Cycling Power Wahoo Trainer data field
  // Transfer CPWT data from the Server (Zwift) to the Client (Wahoo)
  client_cpwt.write_resp(data, len); // Just pass on and process later!
  // Process to extract critical data values
  uint8_t cpwtDataLen = (uint8_t)len;    // Get the actual length of data bytes and type cast to (uint8_t)
  uint8_t cpwtData[cpwtDataLen];
  memset(cpwtData, 0, cpwtDataLen); // set to zero
  // Display the raw request packet actual length
#if Serial_Monitor
  Serial.printf("Server CPWT Data [Len: %d] [Data:  ", len);
#endif
  // Transfer the contents of data to cpwtData for further processing!
  for (int i = 0; i < cpwtDataLen; i++) {
    if ( i <= sizeof(cpwtData)) {
      cpwtData[i] = *data++;
      // Display the raw request packet byte by byte in HEX
#if Serial_Monitor
      Serial.printf("%02X ", cpwtData[i], HEX);
#endif
    }
  }
#if Serial_Monitor
  Serial.print(" ]  "); 
#endif
  // do something ---------------- with cpwtData ---------
  // Notice that the Unlock command is {0x20, 0xEE, 0xFC} --> is NOT treated
#if Serial_Monitor
  if (cpwtData[0] == setSimMode) {
    uint16_t tmp = ( cpwtData[1] + (cpwtData[2]*256) );  // This works perfect !!!
    float weight = (float(tmp) / 100); // Rider weight in Kg
    tmp = ( cpwtData[3] + (cpwtData[4]*256) );  // This works perfect !!!
    float rrc = (float(tmp) / 1000);    // Rolling Resistance Coefficient
    tmp = ( cpwtData[5] + (cpwtData[6]*256) );  // This works perfect !!!
    float wrc = (float(tmp) / 1000);    // Wind Resistance Coefficient
    Serial.printf(" Weight: %0.1f RRC: %f WRC: %f", weight, rrc, wrc);
  }
#endif
  if (cpwtData[0] == setSimGrade) {
    uint16_t gr = ( cpwtData[1] + (cpwtData[2]*256) ); // This works perfect !!!
    float SimGrade = 100 * float( ((gr * 2.0 / 65535) - 1.0) ); // Percentage of road grade --> range: between +1 and -1 (!)
    SetNewRawGradeValue(SimGrade);
    ShowSlopeTriangleOnOled();
  }
#if Serial_Monitor
  Serial.println();
#endif
}

void SetNewRawGradeValue(float ZwiftGrade)
{
        // ----- Recalculate to relevant values for this project ------
        // Take into account the allowed Increase Percentage of the inclination
        // 100% has no effect, 50% means every increase or decrease is divided by 2
        // --> the increase or decrease of inclination is in 50% smaller steps...
        gradePercentValue = ZwiftGrade;
        RawgradeValue = (long)(ZwiftGrade*100) + 20000;
#if Serial_Monitor
        Serial.printf("Grade percentage: %02.1f %% RawgradeValue: %05d \n", gradePercentValue, RawgradeValue);
#endif

}

void server_cpcp_callback(uint16_t conn_hdl, BLECharacteristic* chr, uint8_t* data, uint16_t len)
{
  /*
       {
        OPCODE_SET_CUMULATIVE_VALUE               =  1,
        OPCODE_UPDATE_SENSOR_LOCATION             =  2,
        OPCODE_REQUEST_SUPPORTED_SENSOR_LOCATIONS =  3,
        OPCODE_SET_CRANK_LENGTH                   =  4,
        OPCODE_REQUEST_CRANK_LENGTH               =  5,
        OPCODE_SET_CHAIN_LENGTH                   =  6,
        OPCODE_REQUEST_CHAIN_LENGTH               =  7,
        OPCODE_SET_CHAIN_WEIGHT                   =  8,
        OPCODE_REQUEST_CHAIN_WEIGHT               =  9,
        OPCODE_SET_SPAN_LENGTH                    = 10,
        OPCODE_REQUEST_SPAN_LENGTH                = 11,
        OPCODE_START_OFFSET_COMPENSATION          = 12,
        OPCODE_MASK_CYCLING_POWER_MEASUREMENT_CHARACTERISTIC_CONTENT = 13,
        OPCODE_REQUEST_SAMPLING_RATE              = 14,
        OPCODE_REQUEST_FACTORY_CALIBRATION_DATE   = 15,
        OPCODE_RESPONSE_CODE                      = 32,
    };
*/
  // Server CPCP Cycling Power Control Point data field
  // NO TREATMENT OF COMMAND !!!
  // Transfer cpcp data from the Server (Zwift) to the Client (Wahoo)
  client_cpcp.write_resp(data, len); // Just pass on and process later!
  
#if Serial_Monitor
  uint8_t cpcpDataLen = (uint8_t)len;    // Get the actual length of data bytes and type cast to (uint8_t)
  uint8_t cpcpData[cpcpDataLen];
  memset(cpcpData, 0, cpcpDataLen); // set to zero
  // Display the raw request packet actual length
  Serial.printf("Server CPCP Data [Len: %d] [Data:  ", len);
  // Transfer the contents of data to cpcpData
  for (int i = 0; i < cpcpDataLen; i++) {
    if ( i <= sizeof(cpcpData)) {
      cpcpData[i] = *data++;
      // Display the raw request packet byte by byte in HEX
      Serial.printf("%02X ", cpcpData[i], HEX);
    }
  }
  Serial.print(" ]  "); 
#endif 
}

#if Serial_Monitor
void PrintPeerAddress(uint8_t addr[6])
{
  for (int i = 1; i < 6; i++) {
      // Display byte by byte in HEX reverse: little Endian
//    if ( i <= sizeof(addr)) {
      Serial.printf("%02X:",addr[(6-i)], HEX);
//    }
  }
   Serial.printf("%02X ",addr[0], HEX);
}
#endif

void Periph_connect_callback(uint16_t conn_handle) 
{
  char central_name[32] = { 0 };
  uint8_t central_addr[6] = {0};
  // Get the reference to current connection
  BLEConnection* connection = Bluefruit.Connection(conn_handle);
  connection->getPeerName(central_name, sizeof(central_name));
  ble_gap_addr_t peer_address = connection->getPeerAddr();
  memcpy(central_addr, peer_address.addr, 6);
#if Serial_Monitor
  Serial.printf("Peripheral connected to Central: %s conn handle [%d] MAC Address: ", central_name, conn_handle);
  PrintPeerAddress(central_addr);
#endif 
  // Laptop is connecting
  if (memcmp(central_addr, Laptop.DevAddr, 6) == 0 ) { // Check MAC address
    // Laptop/PC is connecting !
    memcpy(Laptop.PeerName, central_name, sizeof(central_name));
    Laptop.conn_handle = conn_handle;
    Laptop.IsConnected = true;
    ShowOnOledLarge("Pairing", (const char*)Laptop.DevName, "Done!", 500);
#if Serial_Monitor
    Serial.printf("Zwift connected: %2X\n", Laptop.IsConnected); 
#endif
#if Serial_Monitor
    Serial.printf("Devname: %s Peername: %s conn_handle %d Isconnected: %2X\n", Laptop.DevName, Laptop.PeerName, Laptop.conn_handle, Laptop.IsConnected);
#endif
  } else Bluefruit.disconnect(conn_handle);
}

/*
#define   BLE_HCI_STATUS_CODE_COMMAND_DISALLOWED   0x0C
#define   BLE_HCI_STATUS_CODE_INVALID_BTLE_COMMAND_PARAMETERS   0x12
#define   BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION   0x13
#define   BLE_HCI_REMOTE_DEV_TERMINATION_DUE_TO_LOW_RESOURCES   0x14
#define   BLE_HCI_REMOTE_DEV_TERMINATION_DUE_TO_POWER_OFF   0x15
#define   BLE_HCI_LOCAL_HOST_TERMINATED_CONNECTION   0x16
#define   BLE_HCI_UNSUPPORTED_REMOTE_FEATURE   0x1A
#define   BLE_HCI_STATUS_CODE_INVALID_LMP_PARAMETERS   0x1E
#define   BLE_HCI_STATUS_CODE_UNSPECIFIED_ERROR   0x1F
#define   BLE_HCI_STATUS_CODE_LMP_RESPONSE_TIMEOUT   0x22
#define   BLE_HCI_STATUS_CODE_LMP_ERROR_TRANSACTION_COLLISION   0x23
#define   BLE_HCI_STATUS_CODE_LMP_PDU_NOT_ALLOWED   0x24
#define   BLE_HCI_INSTANT_PASSED   0x28
#define   BLE_HCI_PAIRING_WITH_UNIT_KEY_UNSUPPORTED   0x29
#define   BLE_HCI_DIFFERENT_TRANSACTION_COLLISION   0x2A
#define   BLE_HCI_PARAMETER_OUT_OF_MANDATORY_RANGE   0x30
#define   BLE_HCI_CONTROLLER_BUSY   0x3A
#define   BLE_HCI_CONN_INTERVAL_UNACCEPTABLE   0x3B
#define   BLE_HCI_DIRECTED_ADVERTISER_TIMEOUT   0x3C
#define   BLE_HCI_CONN_TERMINATED_DUE_TO_MIC_FAILURE   0x3D
#define   BLE_HCI_CONN_FAILED_TO_BE_ESTABLISHED   0x3E
*/

/*
 * Callback invoked when a connection is dropped
 * @param conn_handle connection where this event happens
 * @param reason is a BLE_HCI_STATUS_CODE which can be found in ble_hci.h
 * https://github.com/adafruit/Adafruit_nRF52_Arduino/blob/master/cores/nRF5/nordic/softdevice/s140_nrf52_6.1.1_API/include/ble_hci.h
 */
void Periph_disconnect_callback(uint16_t conn_handle, uint8_t reason)
{
  char central_name[32] = { 0 };
  char disc_reason[10] = {0};
  sprintf(disc_reason, "Reason: %2X", reason);
  if(Laptop.conn_handle == conn_handle ) { // Laptop/Desktop is disconnected
     Laptop.conn_handle = BLE_CONN_HANDLE_INVALID;
     Laptop.IsConnected = false;
     memcpy(central_name, Laptop.PeerName, sizeof(Laptop.PeerName));
     ShowOnOledLarge("Lost!", (const char*)Laptop.DevName, (const char*)disc_reason, 500);
#if Serial_Monitor
    Serial.printf("Devname: %s Peername: %s conn_handle %d Isconnected: %2X\n", Laptop.DevName, Laptop.PeerName, Laptop.conn_handle, Laptop.IsConnected);
#endif
     // Start advertising: to be picked up by Laptop/Desktop --> Zwift!
     Start_Server_Advertising();
  }
#if Serial_Monitor
  Serial.printf("Peripheral disconnected from %s [%d] reason = %02X \n", central_name, conn_handle, reason, HEX);
#endif

}

void server_cccd_callback(uint16_t conn_handle, BLECharacteristic* chr, uint16_t cccd_value)
{
    // When changed, display the Notify Status for all NOTIFY charcteristics
#if Serial_Monitor
    Serial.printf("Server CCCD Updated to: [%d] --> ", cccd_value);
    // Check the characteristic UUID this CCCD callback is associated with,
    // in case this handler is used for multiple CCCD records.
    if (chr->uuid == server_cpmc.uuid) { 
        if (chr->notifyEnabled(conn_handle)) { 
            Serial.print("Server CPMC 'Notify' enabled");
        } else {
            Serial.print("Server CPMC 'Notify' disabled");
        }
    }
    if (chr->uuid == server_cpwt.uuid) { 
        if (chr->indicateEnabled(conn_handle)) { 
            Serial.print("Server CPWT 'Indicate' enabled");
        } else {
            Serial.print("Server CPWT 'Indicate' disabled");
        }
    }
    if (chr->uuid == server_cpcp.uuid) { 
        if (chr->indicateEnabled(conn_handle)) { 
            Serial.print("Server CPCP 'Indicate' enabled");
        } else {
            Serial.print("Server CPCP 'Indicate' disabled");
        }
    }
    Serial.println();
#endif    
} // end CCCD callback

void loop() {
 // Rarely called --> Callbacks dominate!!!!  
} // END of Loop and program
