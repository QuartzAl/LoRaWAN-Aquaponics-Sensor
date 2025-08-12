// -----------------------------------------------------------------------------
// Seeed XIAO ESP32C3 Firmware (Updated for Web UI Ro Calibration)
//
// Features:
// - WiFiManager for easy Wi-Fi configuration.
// - Web server to send LoRa messages with UI feedback on ACK.
// - Web server can update the OLED display title.
// - Web server can update the sensor data sending interval.
// - Web server can update the Gas Sensor Ro calibration value.
// - OLED display for IP address and sensor data.
// - LoRa-E5 module for long-range communication.
// - Periodically sends sensor data via LoRa.
//
// Libraries:
// - WiFiManager by tzapu: https://github.com/tzapu/WiFiManager
// - LoRa-E5 by andresoliva: https://github.com/andresoliva/LoRa-E5
// - Adafruit SSD1306: https://github.com/adafruit/Adafruit_SSD1306
// - Adafruit GFX Library: https://github.com/adafruit/Adafruit-GFX-Library
// -----------------------------------------------------------------------------

#include <WiFi.h>
#include <WebServer.h>
#include <WiFiManager.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "LoRa-E5.h"
#include <ADS1x15.h>
#include <Preferences.h>
#include <CayenneLPP.h>
#include "index.h" // Include the HTML content for the web server


// --- Pin Definitions ---
// LoRa-E5 Module UART pins
#define WIO_RX_PIN 20
#define WIO_TX_PIN 21
#define RECEIVE_WINDOW 1000 // Timeout for receiving packets in milliseconds

// --- OLED Display Configuration ---
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define OLED_RESET -1    // Reset pin # (or -1 if sharing Arduino reset pin)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// --- Object Instantiation ---
WebServer server(80);
ADS1115 ADS(0x48);

// --- LoRa Message Status Handling ---
enum LoraStatus { IDLE, SENDING, ACK_SUCCESS, ACK_FAILED };
LoraStatus loraStatus = IDLE;
String messageToSend = "";
extern HardwareSerial SerialLoRa;
const unsigned long loraTimeout = 6000; // Timeout for ACK in milliseconds


/************************LORA SET UP*******************************************************************/
#define LoRa_APPKEY               "19aee7bedec56509a9c66a44b7956b6f" /*Custom key for this App*/
#define LoRa_FREQ_standard        AS923                                     /*International frequency band. see*/
#define LoRa_DR                   DR4                                       /*DR5=5.2kbps //data rate. see at https://www.thethingsnetwork.org/docs/lorawan/regional-parameters/   */
#define LoRa_DEVICE_CLASS         CLASS_C                                   /*CLASS_A for power restriction/low power nodes. Class C for other device applications */
#define LoRa_PORT_BYTES           8                                         /*node Port for binary values to send, allowing the app to know it is recieving bytes*/
#define LoRa_PORT_STRING          7                                         /*Node Port for string messages to send, allowing the app to know it is recieving characters/text */
#define LoRa_POWER                14                                        /*Node Tx (Transmition) power*/
#define LoRa_CHANNEL              0                                         /*Node selected Tx channel. Default is 0, we use 2 to show only to show how to set up*/
#define LoRa_ADR_FLAG             true                                      /*ADR(Adaptative Dara Rate) status flag (True or False). Use False if your Node is moving*/
/*Time to wait for transmiting a packet again*/
#define Tx_delay_s                9.5 /*delay between transmitions expressed in seconds*/
/*Packet information*/
#define PAYLOAD_FIRST_TX          10  /*bytes to send into first packet*/
#define Tx_and_ACK_RX_timeout 6000 /*6000 for SF12,4000 for SF11,3000 for SF11, 2000 for SF9/8/, 1500 for SF7. All examples consering 50 bytes payload and BW125*/
/*******************************************************************/
/*Set up the LoRa module with the desired configuration */
void LoRa_setup(void) {
    lora.setDeviceMode(LWOTAA); /*LWOTAA or LWABP. We use LWOTAA in this example*/
    lora.setDataRate((_data_rate_t)LoRa_DR, (_physical_type_t)LoRa_FREQ_standard);
    lora.setKey(NULL, NULL, LoRa_APPKEY);                  /*Only App key is seeted when using OOTA*/
    lora.setClassType((_class_type_t)LoRa_DEVICE_CLASS); /*set device class*/
    lora.setPort(LoRa_PORT_BYTES);                         /*set the default port for transmiting data*/
    lora.setPower(LoRa_POWER);                             /*sets the Tx power*/
    lora.setChannel(LoRa_CHANNEL);                         /*selects the channel*/
    lora.setAdaptiveDataRate(LoRa_ADR_FLAG);               /*Enables adaptative data rate*/
}

#define AP_DEFAULT_NAME "XIAO-ESP32C3-AP" // Access Point name
#define AP_DEFAULT_PASSWORD "Access@Sensor" // Access Point password
#define DEFAULT_SENSOR_INTERVAL 120 * 1000 // Default sensor interval in milliseconds
#define DEFAULT_OLED_TITLE "Petra DO Sensor"
#define DEFAULT_RO 30000.0 // Default Ro value for gas sensor

// --- Preference Keys ---
#define AP_NAME_KEY "ap_name"
#define AP_PASSWORD_KEY "ap_password"
#define SENSOR_INTERVAL_KEY "sensor_interval"
#define OLED_TITLE_KEY "oled_title"
#define RO_KEY "gas_ro" // Key for storing Ro value
#define USE_WIFI_MANAGER_KEY "use_wifi_manager"

// Define channels for each sensor to differentiate them in the payload
#define DISSOLVED_OXYGEN_CHANNEL 1
#define AIR_QUALITY_CHANNEL      2

// --- Global Variables ---
unsigned long previousSensorMillis = 0;
unsigned long previousSentMillis = 0;
long sensorInterval = DEFAULT_SENSOR_INTERVAL; // 240 seconds, changeable from web UI
String oledTitle = DEFAULT_OLED_TITLE; // Default title, changeable from web UI
float gasSensorRo = DEFAULT_RO; // Ro value for gas sensor, changeable from web UI
char buffer[128]; // Buffer for commands
bool useWiFiManager;
Preferences preferences;



// --- Function Prototypes ---
void handleRoot();
void handleSend();
void handleStatus();
void handleSetTitle();
void handleSetInterval();
void handleSetRo(); 
void readAndDisplaySensorData(float gasPPM, float oxygen, float temperature);
void sendSensorDataLora(float gasPPM, float oxygen, float temperature);
void processLoraSend();
float processGasData();
float processOxygenData(float temperature);
float processTemperatureData();
float calculate_ppm(float voltage, const char* sensor_type);
float readDO(float voltage_mv, float temperature_c);

void setup() {
    Serial.begin(115200);
    while (!Serial);
    Wire.begin(); 

    delay(10000);

    String apName;
    String apPassword;

    // Load in preferences from EEPROM
    preferences.begin("my-app", false);
    if (preferences.isKey(AP_NAME_KEY)) {
        String temp = preferences.getString(AP_NAME_KEY, AP_DEFAULT_NAME);
        if (temp.length() > 0) {
            apName = temp;
        } else {
            apName = AP_DEFAULT_NAME;
        }
    }
    if (preferences.isKey(AP_PASSWORD_KEY)) {

        String tempPassword = preferences.getString(AP_PASSWORD_KEY, AP_DEFAULT_PASSWORD);
        if (tempPassword.length() > 0) {
            apPassword = tempPassword;
        } else {
            apPassword = AP_DEFAULT_PASSWORD;
        }
    }
    if (preferences.isKey(SENSOR_INTERVAL_KEY)) {
        sensorInterval = preferences.getUInt(SENSOR_INTERVAL_KEY, DEFAULT_SENSOR_INTERVAL);
    }
    if (preferences.isKey(OLED_TITLE_KEY)) {
        oledTitle = preferences.getString(OLED_TITLE_KEY, DEFAULT_OLED_TITLE);
    }
    if (preferences.isKey(RO_KEY)) {
        gasSensorRo = preferences.getFloat(RO_KEY, DEFAULT_RO);
    }

    preferences.end();
    
    lora.init(WIO_TX_PIN, WIO_RX_PIN);
    LoRa_setup(); // Set up LoRa module with desired configuration
    while (lora.setOTAAJoin(JOIN, 10000) == 0) {

    }
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        Serial.println(F("SSD1306 allocation failed"));
        for (;;);
    }
    
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Initializing...");
    display.display();

    // --- Initialize Hardware ---
    display.println("Waking LoRa module...");
    display.display();
    display.println("Starting I2C bus...");
    display.display();
    ADS.begin();

    // --- WiFiManager Setup ---
    bool useWiFiManager = false; // Set to false if you want to hardcode WiFi credentials
    bool res = false;
    if (useWiFiManager) {
      display.println("Connect to the AP and configure WiFi");
      display.println("AP Name: " + String(AP_DEFAULT_NAME));
      display.println("AP Password: " + String(AP_DEFAULT_PASSWORD));
      display.display();
      WiFiManager wm;
      res = wm.autoConnect(AP_DEFAULT_NAME, AP_DEFAULT_PASSWORD);
      if (!res) {
        Serial.println("Failed to connect");
        display.println("Failed to connect to WiFi");
        display.display();
        delay(5000);
      } 
    } else {
      // Hardcoded WiFi credentials
      display.println("Starting Personal Hotspot...");
      display.display();
      WiFi.softAP(AP_DEFAULT_NAME, AP_DEFAULT_PASSWORD);
      display.println("Personal Hotspot started!");
      display.println("SSID: " + String(AP_DEFAULT_NAME));
      display.println("Password: " + String(AP_DEFAULT_PASSWORD));
      res = true;
    }

    if (res) {
        Serial.println("Connected to WiFi!");
        server.on("/", HTTP_GET, handleRoot);
        server.on("/send", HTTP_POST, handleSend);
        server.on("/status", HTTP_GET, handleStatus);
        server.on("/settitle", HTTP_POST, handleSetTitle);
        server.on("/setinterval", HTTP_POST, handleSetInterval);
        server.on("/setro", HTTP_POST, handleSetRo); // New route for Ro value
        server.begin();
        Serial.println("HTTP server started");
        display.println("HTTP server started! :D");
        display.display();
    }
    delay(5000);
    display.clearDisplay();
    display.setCursor(0, 0);



    float gasPPM = processGasData();
    float temperature = processTemperatureData();
    float oxygen = processOxygenData(temperature);
    readAndDisplaySensorData(gasPPM, oxygen, temperature);
    sendSensorDataLora(gasPPM, oxygen, temperature);
}

void loop() {
    server.handleClient();
    processLoraSend(); // Check if we need to send a LoRa message
    ADS.setGain(ADS1X15_GAIN_2048MV);

    unsigned long currentMillis = millis();
    if (currentMillis - previousSensorMillis >= sensorInterval) {
      previousSensorMillis = currentMillis;

      float gasPPM = processGasData();
      float temperature = processTemperatureData();
      float oxygen = processOxygenData(temperature);
      readAndDisplaySensorData(gasPPM, oxygen, temperature);
      sendSensorDataLora(gasPPM, oxygen, temperature);
    }

    if (SerialLoRa.available()) {
      Serial.println("Data received from LoRa module:");
      String packet = SerialLoRa.readStringUntil('\n'); // Read until newline
      if (packet.startsWith("+MSG: FPENDING")){
       // receive and parse the packet
       // +MSG: PORT: 1; RX: "68656C6C6F"
       String RawData = SerialLoRa.readStringUntil('\n');
       String data = RawData.substring(RawData.indexOf('"') + 1, RawData.lastIndexOf('"'));

       Serial.println("Received packet -----------------");
       Serial.print("Packet from LoRa: ");
       Serial.println(data);
       Serial.println("End of packet -------------------");
      } else {
        Serial.println(packet); // Read and print any other data
      }
    }

    while (Serial.available()) {
        char c = Serial.read();
        SerialLoRa.write(c); // Forward to LoRa module
    }
    
}


// --- LoRa Functions ---
void processLoraSend() {
    if (loraStatus == SENDING) {
        if (!messageToSend.isEmpty()) {
            Serial.print("Sending LoRa message from web: ");
            Serial.println(messageToSend);
            
            // This library's send function is blocking but doesn't handle ACKs by default.
            // For ACK, you would typically use specific AT commands.
            // We simulate the ACK logic flow for the web UI here.
            bool sentOk = lora.transferPacket((unsigned char*)(messageToSend.c_str()), messageToSend.length(), Tx_and_ACK_RX_timeout);

            if (sentOk) { 
                loraStatus = ACK_SUCCESS; // Simulate success for UI feedback
                Serial.println("LoRa message sent!");
            } else {
                loraStatus = ACK_FAILED;
                Serial.println("LoRa message failed to send.");
            }
            messageToSend = ""; // Clear message after attempting to send
        }
    }
}

void sendSensorDataLora(float gasPPM, float oxygen, float temperature) {
    // 2. Instantiate a CayenneLPP object with a maximum payload size.
    CayenneLPP lpp(51);

    // 3. Add sensor data to the payload
    lpp.addAnalogInput(DISSOLVED_OXYGEN_CHANNEL, oxygen);
    lpp.addAnalogInput(AIR_QUALITY_CHANNEL, gasPPM);

    // 4. Get the final binary payload and its size.
    uint8_t* payload_buffer = lpp.getBuffer();
    uint8_t payload_size = lpp.getSize();

    // 5. Send the payload via the LoRa module.
    lora.transferPacket(payload_buffer, payload_size, Tx_and_ACK_RX_timeout);
}

// --- Web Server Handlers ---
void handleRoot() {
    server.send(200, "text/html", index_html);
}

void handleSend() {
    if (server.hasArg("message")) {
        if (loraStatus == IDLE || loraStatus == ACK_SUCCESS || loraStatus == ACK_FAILED) {
            messageToSend = server.arg("message");
            loraStatus = SENDING;
            server.send(200, "text/plain", "Message queued for sending.");
        } else {
            server.send(503, "text/plain", "Server busy sending previous message.");
        }
    } else {
        server.send(400, "text/plain", "400: Invalid Request");
    }
}

void handleStatus() {
    String statusMessage = "IDLE";
    switch (loraStatus) {
        case SENDING:       statusMessage = "SENDING"; break;
        case ACK_SUCCESS:
            statusMessage = "SUCCESS";
            loraStatus = IDLE; // Reset status after reporting
            break;
        case ACK_FAILED:
            statusMessage = "FAILED";
            loraStatus = IDLE; // Reset status after reporting
            break;
        case IDLE: break;
    }
    server.send(200, "text/plain", statusMessage);
}

void handleSetTitle() {
    if (server.hasArg("title")) {
        String newTitle = server.arg("title");
        if (newTitle.length() > 10) {
            server.send(400, "text/plain", "Title too long (max 10 chars).");
        } else {
            oledTitle = newTitle;
            // Save to preferences
            preferences.begin("my-app", false);
            preferences.putString(OLED_TITLE_KEY, oledTitle);
            preferences.end();
            float gasPPM = processGasData();
            float temperature = processTemperatureData();
            float oxygen = processOxygenData(temperature);
            readAndDisplaySensorData(gasPPM, oxygen, temperature);
            server.send(200, "text/plain", "Title updated successfully!");
        }
    } else {
        server.send(400, "text/plain", "400: Invalid Request");
    }
}

/**
 * @brief Handles POST request to update the sensor reading interval.
 * Validates the input to be a number >= 90.
 */
void handleSetInterval() {
    if (server.hasArg("interval")) {
        String intervalStr = server.arg("interval");
        long newInterval = intervalStr.toInt(); // Convert string to long

        if (newInterval >= 90) {
            sensorInterval = newInterval * 1000; // Convert seconds to milliseconds
            
            // Save to preferences
            preferences.begin("my-app", false);
            preferences.putUInt(SENSOR_INTERVAL_KEY, sensorInterval);
            preferences.end();

            Serial.print("Sensor interval updated to: ");
            Serial.print(newInterval);
            Serial.println(" seconds.");
            server.send(200, "text/plain", "Interval updated to " + String(newInterval) + " seconds.");
        } else {
            server.send(400, "text/plain", "Invalid interval. Must be at least 90 seconds.");
        }
    } else {
        server.send(400, "text/plain", "400: Invalid Request (missing interval).");
    }
}

/**
 * @brief Handles POST request to update the gas sensor Ro value.
 * Validates the input to be a positive number.
 */
void handleSetRo() {
    if (server.hasArg("ro")) {
        String roStr = server.arg("ro");
        float newRo = roStr.toFloat();

        if (newRo > 0) { // Basic validation
            gasSensorRo = newRo;

            // Save the new Ro value to preferences
            preferences.begin("my-app", false);
            preferences.putFloat(RO_KEY, gasSensorRo);
            preferences.end();

            Serial.print("Gas Sensor Ro updated to: ");
            Serial.println(gasSensorRo);
            server.send(200, "text/plain", "Ro updated to " + String(gasSensorRo, 0));
        } else {
            server.send(400, "text/plain", "Invalid Ro value. Must be a positive number.");
        }
    } else {
        server.send(400, "text/plain", "400: Invalid Request (missing ro).");
    }
}


// --- Display Functions ---
void readAndDisplaySensorData(float gasPPM, float oxygen, float temperature) {
    display.clearDisplay();
    display.setCursor(0, 0);

    // Display Custom Title
    display.setTextSize(1);
    display.println(oledTitle);

    // Display Connected WiFi SSID
    display.setTextSize(1);
    if (useWiFiManager) {
      display.print("Using external WiFi");
      display.print("SSID: ");
      display.println(WiFi.SSID());
    } else {
      display.print("SSID: ");
      display.println(AP_DEFAULT_NAME);
      display.print("Pass: ");
      display.println(AP_DEFAULT_PASSWORD);
    }

    // Display IP Address
    display.setTextSize(1);
    display.print("IP: ");
    if (useWiFiManager) {
        display.println(WiFi.localIP().toString());
    } else {
        display.println(WiFi.softAPIP().toString());
    }
    

    // Display air quality data
    display.print("Air: ");
    display.print(gasPPM, 3);
    display.println(" PPM ");
    
    // Display Dissolved Oxygen data
    display.print("Oxygen: ");
    display.print(oxygen, 3);
    display.println(" mg/L");


    display.print("Temperature: ");
    display.print(temperature, 3);
    display.println(" °C");

    display.display();
}

// --- Gas Data Processing Function ---
float processGasData() {
    int16_t gasValue = ADS.readADC(0); // Read gas sensor value
    // calculate voltage from integer and display it 
    float voltage = ADS.toVoltage(1) * gasValue;
    Serial.println("Gas sensor voltage: " + String(voltage, 3) + " V");

    // Calculate PPM for the gas sensor
    float ppm = calculate_ppm(voltage, "TGS2600");
    Serial.println("Gas sensor PPM: " + String(ppm, 2) + " ppm");

    return ppm;
}

float processTemperatureData(){
    int16_t temperatureValue = ADS.readADC(1); // Read temperature sensor value
    float temperatureVoltage = ADS.toVoltage(1) * temperatureValue;
    float temperatureResistance = ((10000 * 3.3) / temperatureVoltage - 10000);
    float temperature = 1 / (1/298.15 + (1 / 3950) * log(temperatureResistance / 10000)); - 273.15;
    Serial.println("Thermistor Resistance: " + String(temperatureResistance, 2) + " Ω");
    Serial.println("Temperature: " + String(temperature, 2) + " °C");

    return temperature;
}


float processOxygenData(float temperature) {
    int16_t oxygenValue = ADS.readADC(3); // Read oxygen sensor value
    // calculate voltage from integer and display it 
    float oxygenVoltage = ADS.toVoltage(1) * oxygenValue;
    Serial.println("Oxygen sensor voltage: " + String(oxygenVoltage, 3) + " V");

    float oxygen = readDO(oxygen, temperature);
    Serial.println("Dissolved Oxygen: " + String(oxygen, 2) + " mg/L");

    return oxygen;
}


/**
 * @param voltage_mv - The voltage in millivolts.
 * @param temperature_c - The temperature in degrees Celsius.
 * @returns The calculated dissolved oxygen (DO) value.
 */
float readDO(float voltage_mv, float temperature_c) {
    // Single point calibration needs to be filled CAL1_V and CAL1_T
    const float CAL1_V = 340.0; // mV
    const float CAL1_T = 26.2; // °C

    const float DO_Table[] = {
        14460.0, 14220.0, 13820.0, 13440.0, 13090.0, 12740.0, 12420.0, 12110.0, 11810.0, 11530.0,
        11260.0, 11010.0, 10770.0, 10530.0, 10300.0, 10080.0, 9860.0, 9660.0, 9460.0, 9270.0,
        9080.0, 8900.0, 8730.0, 8570.0, 8410.0, 8250.0, 8110.0, 7960.0, 7820.0, 7690.0,
        7560.0, 7430.0, 7300.0, 7180.0, 7070.0, 6950.0, 6840.0, 6730.0, 6630.0, 6530.0, 6410.0
    };

    float V_saturation = CAL1_V + 35.0 * (temperature_c - CAL1_T);

    int temp_index = (int)temperature_c;
    if (temp_index < 0 || temp_index >= sizeof(DO_Table) / sizeof(DO_Table[0])) {
        Serial.println("Error: Temperature is too hot or too cold, please remove sensor immediately\n");
        return 0.0;
    }

    float result = (voltage_mv * DO_Table[temp_index]) / V_saturation;

    return result;
}


/**
 * @param voltage - The voltage input.
 * @param sensor_type - A string representing the sensor type.
 * @returns The calculated PPM value.
 */
float calculate_ppm(float voltage, const char* sensor_type) {
    const float VC = 3.3;
    const float RL = 10000.0;

    // Use the global gasSensorRo variable instead of a local one
    // float RO = 30000.0; 
    float slope = -0.1109;
    float intercept = 0.0;

    // Calculations using floating-point numbers
    float calculate_rs = (VC / voltage - 1.0) * RL;
    float calculate_rs_ro = calculate_rs / gasSensorRo; // Use the global variable here

    // Cap the value at 1 if it's greater than or equal to 1
    if (calculate_rs_ro >= 1.0) {
        calculate_rs_ro = 1.0;
    }

    // Use log10f for float-specific base-10 logarithm and powf for power function
    float log_rs_ro = log10f(calculate_rs_ro);
    float log_ppm = (log_rs_ro - intercept) / slope;
    float ppm = powf(10.0, log_ppm);

    return ppm;
}
