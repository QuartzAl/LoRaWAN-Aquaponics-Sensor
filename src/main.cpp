// -----------------------------------------------------------------------------
// Seeed XIAO ESP32C3 Firmware (Updated for Web UI Ro Calibration & Advanced Settings)
//
// Features:
// - WiFiManager for easy Wi-Fi configuration.
// - Web server to send LoRa messages with UI feedback on ACK.
// - Web server can update OLED title, sensor interval, Gas Sensor Ro, and temp mode.
// - Collapsible "Advanced Settings" section in UI.
// - Toggle to use live temperature or a default value for DO calculation.
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


// --- NTC Thermistor Object Configuration ---
#define R0 10000.0
#define Tn 25 // nominal temperature in Celsius
#define BETA 3950.0
#define KELVIN_CONVERSION 273.15

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
#define LoRa_APPKEY              "19aee7bedec56509a9c66a44b7956b6f" /*Custom key for this App*/
#define LoRa_FREQ_standard       AS923                                     /*International frequency band. see*/
#define LoRa_DR                  DR4                                       /*DR5=5.2kbps //data rate. see at https://www.thethingsnetwork.org/docs/lorawan/regional-parameters/  */
#define LoRa_DEVICE_CLASS        CLASS_C                                   /*CLASS_A for power restriction/low power nodes. Class C for other device applications */
#define LoRa_PORT_BYTES          8                                         /*node Port for binary values to send, allowing the app to know it is recieving bytes*/
#define LoRa_PORT_STRING         7                                         /*Node Port for string messages to send, allowing the app to know it is recieving characters/text */
#define LoRa_POWER               14                                        /*Node Tx (Transmition) power*/
#define LoRa_CHANNEL             0                                         /*Node selected Tx channel. Default is 0, we use 2 to show only to show how to set up*/
#define LoRa_ADR_FLAG            true                                      /*ADR(Adaptative Dara Rate) status flag (True or False). Use False if your Node is moving*/
/*Time to wait for transmiting a packet again*/
#define Tx_delay_s               9.5 /*delay between transmitions expressed in seconds*/
/*Packet information*/
#define PAYLOAD_FIRST_TX         10  /*bytes to send into first packet*/
#define Tx_and_ACK_RX_timeout 6000 /*6000 for SF12,4000 for SF11,3000 for SF11, 2000 for SF9/8/, 1500 for SF7. All examples consering 50 bytes payload and BW125*/
/*******************************************************************/
/*Set up the LoRa module with the desired configuration */
void LoRa_setup(void) {
    lora.setDeviceMode(LWOTAA); /*LWOTAA or LWABP. We use LWOTAA in this example*/
    lora.setDataRate((_data_rate_t)LoRa_DR, (_physical_type_t)LoRa_FREQ_standard);
    lora.setKey(NULL, NULL, LoRa_APPKEY);               /*Only App key is seeted when using OOTA*/
    lora.setClassType((_class_type_t)LoRa_DEVICE_CLASS); /*set device class*/
    lora.setPort(LoRa_PORT_BYTES);                       /*set the default port for transmiting data*/
    lora.setPower(LoRa_POWER);                           /*sets the Tx power*/
    lora.setChannel(LoRa_CHANNEL);                       /*selects the channel*/
    lora.setAdaptiveDataRate(LoRa_ADR_FLAG);             /*Enables adaptative data rate*/
}

#define AP_DEFAULT_NAME "XIAO-ESP32C3-AP" // Access Point name
#define AP_DEFAULT_PASSWORD "Access@Sensor" // Access Point password
#define DEFAULT_SENSOR_INTERVAL 120 * 1000 // Default sensor interval in milliseconds
#define DEFAULT_OLED_TITLE "Petra DO Sensor"
#define DEFAULT_RO 30000.0 // Default Ro value for gas sensor
#define DEFAULT_WATER_TEMP 25.0 // Default water temperature if not using live reading

// --- Preference Keys ---
#define AP_NAME_KEY "ap_name"
#define AP_PASSWORD_KEY "ap_password"
#define SENSOR_INTERVAL_KEY "sensor_interval"
#define OLED_TITLE_KEY "oled_title"
#define RO_KEY "gas_ro"
#define USE_LIVE_TEMP_KEY "use_live_temp"
#define DEFAULT_TEMP_KEY "default_temp"

// Define channels for each sensor to differentiate them in the payload
#define DISSOLVED_OXYGEN_CHANNEL 1
#define AIR_QUALITY_CHANNEL      2
#define TEMPERATURE_CHANNEL      3

// --- Global Variables ---
unsigned long previousSensorMillis = 0;
long sensorInterval = DEFAULT_SENSOR_INTERVAL;
String oledTitle = DEFAULT_OLED_TITLE;
float gasSensorRo = DEFAULT_RO;
bool useLiveTemperature = true;
float defaultWaterTemperature = DEFAULT_WATER_TEMP;
char buffer[128];
bool useWiFiManager; // Set to true to use WiFi Manager, false for Soft AP mode
Preferences preferences;

// --- Function Prototypes ---
void handleRoot();
void handleSend();
void handleStatus();
void handleSetTitle();
void handleSetInterval();
void handleSetRo();
void handleSetTempToggle();
void handleSetDefaultTemp();
void handleGetSettings();
void readAndDisplaySensorData(float gasPPM, float oxygen, float temperature);
void sendSensorDataLora(float gasPPM, float oxygen, float temperature);
void processLoraSend();
float processGasData();
float processOxygenData(double temperature);
float processTemperatureData();
float calculate_ppm(float voltage, const char* sensor_type);
float readDO(float voltage_mv, double temperature_c);

void setup() {
    Serial.begin(115200);
    while (!Serial);
    Wire.begin(); 

    delay(10000);

    String apName;
    String apPassword;

    // Load in preferences from EEPROM
    preferences.begin("my-app", false);
    apName = preferences.getString(AP_NAME_KEY, AP_DEFAULT_NAME);
    apPassword = preferences.getString(AP_PASSWORD_KEY, AP_DEFAULT_PASSWORD);
    sensorInterval = preferences.getUInt(SENSOR_INTERVAL_KEY, DEFAULT_SENSOR_INTERVAL);
    oledTitle = preferences.getString(OLED_TITLE_KEY, DEFAULT_OLED_TITLE);
    gasSensorRo = preferences.getFloat(RO_KEY, DEFAULT_RO);
    useLiveTemperature = preferences.getBool(USE_LIVE_TEMP_KEY, true);
    defaultWaterTemperature = preferences.getFloat(DEFAULT_TEMP_KEY, DEFAULT_WATER_TEMP);
    preferences.end();
    
    lora.init(WIO_TX_PIN, WIO_RX_PIN);
    LoRa_setup(); // Set up LoRa module with desired configuration
    while (lora.setOTAAJoin(JOIN, 10000) == 0) {
        // Retry join
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
    useWiFiManager = false; // Set to false to hardcode WiFi credentials and use Soft AP
    bool res = false;
    if (useWiFiManager) {
      display.println("Connect to AP for WiFi config");
      display.println("AP Name: " + String(AP_DEFAULT_NAME));
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
      // Hardcoded WiFi credentials / Soft AP mode
      display.println("Starting Personal Hotspot...");
      display.display();
      WiFi.softAP(AP_DEFAULT_NAME, AP_DEFAULT_PASSWORD);
      display.println("Personal Hotspot started!");
      display.println("SSID: " + String(AP_DEFAULT_NAME));
      res = true;
    }

    if (res) {
        Serial.println("WiFi Ready!");
        server.on("/", HTTP_GET, handleRoot);
        server.on("/send", HTTP_POST, handleSend);
        server.on("/status", HTTP_GET, handleStatus);
        server.on("/settitle", HTTP_POST, handleSetTitle);
        server.on("/setinterval", HTTP_POST, handleSetInterval);
        server.on("/setro", HTTP_POST, handleSetRo);
        server.on("/settogletemp", HTTP_POST, handleSetTempToggle);
        server.on("/setdefaulttemp", HTTP_POST, handleSetDefaultTemp);
        server.on("/getsettings", HTTP_GET, handleGetSettings);
        server.begin();
        Serial.println("HTTP server started");
        display.println("HTTP server started! :D");
        display.display();
    }
    delay(5000);
    display.clearDisplay();
    display.setCursor(0, 0);

    // Initial sensor read
    float liveTemperature = processTemperatureData();
    float tempForDO = useLiveTemperature ? liveTemperature : defaultWaterTemperature;
    float gasPPM = processGasData();
    float oxygen = processOxygenData(tempForDO);
    readAndDisplaySensorData(gasPPM, oxygen, liveTemperature);
    sendSensorDataLora(gasPPM, oxygen, liveTemperature);
}

void loop() {
    server.handleClient();
    processLoraSend(); // Check if we need to send a LoRa message
    ADS.setGain(ADS1X15_GAIN_2048MV);

    unsigned long currentMillis = millis();
    if (currentMillis - previousSensorMillis >= sensorInterval) {
      previousSensorMillis = currentMillis;

      float gasPPM = processGasData();
      float liveTemperature = processTemperatureData();
      
      // Decide which temperature to use for the DO calculation based on the setting
      float tempForDO = useLiveTemperature ? liveTemperature : defaultWaterTemperature;

      float oxygen = processOxygenData(tempForDO);
      
      // Always display the LIVE temperature, but indicate if a default is being used for calculation
      readAndDisplaySensorData(gasPPM, oxygen, liveTemperature);
      
      // Send data via LoRa
      sendSensorDataLora(gasPPM, oxygen, liveTemperature);
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
            
            bool sentOk = lora.transferPacket((unsigned char*)(messageToSend.c_str()), messageToSend.length(), Tx_and_ACK_RX_timeout);

            if (sentOk) { 
                loraStatus = ACK_SUCCESS;
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
    CayenneLPP lpp(51);
    lpp.addAnalogInput(DISSOLVED_OXYGEN_CHANNEL, oxygen);
    lpp.addAnalogInput(AIR_QUALITY_CHANNEL, gasPPM);
    lpp.addTemperature(TEMPERATURE_CHANNEL, temperature);

    uint8_t* payload_buffer = lpp.getBuffer();
    uint8_t payload_size = lpp.getSize();
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
        case SENDING:      statusMessage = "SENDING"; break;
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

void handleSetInterval() {
    if (server.hasArg("interval")) {
        long newInterval = server.arg("interval").toInt();
        if (newInterval >= 90) {
            sensorInterval = newInterval * 1000; // Convert seconds to milliseconds
            preferences.begin("my-app", false);
            preferences.putUInt(SENSOR_INTERVAL_KEY, sensorInterval);
            preferences.end();
            server.send(200, "text/plain", "Interval updated to " + String(newInterval) + "s.");
        } else {
            server.send(400, "text/plain", "Invalid interval. Must be >= 90s.");
        }
    } else {
        server.send(400, "text/plain", "400: Invalid Request");
    }
}

void handleSetRo() {
    if (server.hasArg("ro")) {
        float newRo = server.arg("ro").toFloat();
        if (newRo > 0) {
            gasSensorRo = newRo;
            preferences.begin("my-app", false);
            preferences.putFloat(RO_KEY, gasSensorRo);
            preferences.end();
            server.send(200, "text/plain", "Ro updated to " + String(gasSensorRo, 0));
        } else {
            server.send(400, "text/plain", "Invalid Ro. Must be > 0.");
        }
    } else {
        server.send(400, "text/plain", "400: Invalid Request");
    }
}

void handleSetTempToggle() {
    if (server.hasArg("useLive")) {
        useLiveTemperature = (server.arg("useLive") == "true");
        preferences.begin("my-app", false);
        preferences.putBool(USE_LIVE_TEMP_KEY, useLiveTemperature);
        preferences.end();
        server.send(200, "text/plain", "Temperature mode updated.");
    } else {
        server.send(400, "text/plain", "400: Invalid Request");
    }
}

void handleSetDefaultTemp() {
    if (server.hasArg("defaultTemp")) {
        float temp = server.arg("defaultTemp").toFloat();
        if (temp >= 0 && temp <= 40) {
            defaultWaterTemperature = temp;
            preferences.begin("my-app", false);
            preferences.putFloat(DEFAULT_TEMP_KEY, defaultWaterTemperature);
            preferences.end();
            server.send(200, "text/plain", "Default temp updated to " + String(temp, 1) + "C.");
        } else {
            server.send(400, "text/plain", "Invalid temp. Must be 0-40C.");
        }
    } else {
        server.send(400, "text/plain", "400: Invalid Request");
    }
}

void handleGetSettings() {
    String json = "{";
    json += "\"title\":\"" + oledTitle + "\",";
    json += "\"interval\":" + String(sensorInterval / 1000) + ",";
    json += "\"ro\":" + String(gasSensorRo) + ",";
    json += "\"useLiveTemp\":" + String(useLiveTemperature ? "true" : "false") + ",";
    json += "\"defaultTemp\":" + String(defaultWaterTemperature);
    json += "}";
    server.send(200, "application/json", json);
}

// --- Display Functions ---
void readAndDisplaySensorData(float gasPPM, float oxygen, float temperature) {
    display.clearDisplay();
    display.setCursor(0, 0);

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
    display.println(useWiFiManager ? WiFi.localIP().toString() : WiFi.softAPIP().toString());
    
    display.print("Air: ");
    display.print(gasPPM, 3);
    display.println(" PPM ");
    
    display.print("Oxygen: ");
    display.print(oxygen, 3);
    display.println(" mg/L");

    display.print("Temp: ");
    display.print(temperature, 2);
    display.print(" C");
    if (!useLiveTemperature) {
        display.print(" (D)"); // Indicate default temp is used for DO calc
    }
    display.println();

    display.display();
}

// --- Sensor Data Processing Functions ---
float processGasData() {
    int16_t gasValue = ADS.readADC(0);
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
    double temperatureResistance = (double)((3.3) / temperatureVoltage - 1.0) * 10000.0;
    double temperature = (1.0 / (1.0 / (Tn + KELVIN_CONVERSION) + log(temperatureResistance / R0) / BETA)) - KELVIN_CONVERSION;
    Serial.println("Thermistor Resistance: " + String(temperatureResistance, 2) + " Ω");
    Serial.println("Temperature: " + String(temperature, 2) + " C");

    return temperature;
}


float processOxygenData(double temperature) {
    int16_t oxygenValue = ADS.readADC(3);
    float oxygenVoltage = ADS.toVoltage(1) * oxygenValue;
    Serial.println("Oxygen sensor voltage: " + String(oxygenVoltage, 3) + " V");

    float oxygen = readDO(oxygenVoltage, temperature);
    Serial.println("Dissolved Oxygen: " + String(oxygen, 2) + " mg/L");

    return oxygen;
}


/**
 * @param voltage_mv - The voltage in millivolts.
 * @param temperature_c - The temperature in degrees Celsius.
 * @returns The calculated dissolved oxygen (DO) value.
 */
float readDO(float voltage_mv, double temperature_c) {
    const float CAL1_V = 456.0; // mV - CALIBRATE THIS
    const float CAL1_T = 26.5; // °C - CALIBRATE THIS

    const float DO_Table[] = {
        14.46, 14.22, 13.82, 13.44, 13.09, 12.74, 12.42, 12.11, 11.81, 11.53,
        11.26, 11.01, 10.77, 10.53, 10.30, 10.08, 9.86,  9.66,  9.46,  9.27,
        9.08,  8.90,  8.73,  8.57,  8.41,  8.25,  8.11,  7.96,  7.82,  7.69,
        7.56,  7.43,  7.30,  7.18,  7.07,  6.95,  6.84,  6.73,  6.63,  6.53, 6.41
    };

    double V_saturation = CAL1_V + 35.0 * (temperature_c - CAL1_T);

    int temp_index = (int)temperature_c;
    if (temp_index < 0 || temp_index >= sizeof(DO_Table) / sizeof(DO_Table[0])) {
        Serial.println("Error: Temperature is too hot or too cold, please remove sensor immediately\n");
        return 0.0;
    }
    
    // Note: The original values seemed to be off by a factor of 1000 (e.g., 14460.0). 
    // Standard DO tables are in mg/L (e.g., 14.46). I've adjusted them.
    // If your sensor output requires the larger values, change the table back.
    return (voltage_mv * DO_Table[temp_index] * 1000) / V_saturation;
}

float calculate_ppm(float voltage, const char* sensor_type) {
    const float VC = 3.3;
    const float RL = 10000.0;
    float slope = -0.1109;
    float intercept = 0.0;

    float calculate_rs = (VC / voltage - 1.0) * RL;
    float calculate_rs_ro = calculate_rs / gasSensorRo;

    if (calculate_rs_ro >= 1.0) {
        calculate_rs_ro = 1.0;
    }

    float log_rs_ro = log10f(calculate_rs_ro);
    float log_ppm = (log_rs_ro - intercept) / slope;
    return powf(10.0, log_ppm);
}
