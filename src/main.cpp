// -----------------------------------------------------------------------------
// Seeed XIAO ESP32C3 Firmware (Updated for Web UI ACK, Title, and Interval)
//
// Features:
// - WiFiManager for easy Wi-Fi configuration.
// - Web server to send LoRa messages with UI feedback on ACK.
// - Web server can update the OLED display title.
// - Web server can update the sensor data sending interval.
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
#define LoRa_APPKEY              "19aee7bedec56509a9c66a44b7956b6f" /*Custom key for this App*/
#define LoRa_FREQ_standard       AS923                                     /*International frequency band. see*/
#define LoRa_DR                  DR4                                       /*DR5=5.2kbps //data rate. see at https://www.thethingsnetwork.org/docs/lorawan/regional-parameters/   */
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
    lora.setKey(NULL, NULL, LoRa_APPKEY);                     /*Only App key is seeted when using OOTA*/
    lora.setClassType((_class_type_t)LoRa_DEVICE_CLASS); /*set device class*/
    lora.setPort(LoRa_PORT_BYTES);                           /*set the default port for transmiting data*/
    lora.setPower(LoRa_POWER);                               /*sets the Tx power*/
    lora.setChannel(LoRa_CHANNEL);                           /*selects the channel*/
    lora.setAdaptiveDataRate(LoRa_ADR_FLAG);                 /*Enables adaptative data rate*/
}

#define AP_DEFAULT_NAME "XIAO-ESP32C3-AP" // Access Point name
#define AP_DEFAULT_PASSWORD "password" // Access Point password
#define DEFAULT_SENSOR_INTERVAL 5000 // Default sensor interval in milliseconds
#define DEFAULT_OLED_TITLE "Petra DO Sensor"

#define AP_NAME_KEY "ap_name"
#define AP_PASSWORD_KEY "ap_password"
#define SENSOR_INTERVAL_KEY "sensor_interval"
#define OLED_TITLE_KEY "oled_title"
#define USE_WIFI_MANAGER_KEY "use_wifi_manager"

// --- Global Variables ---
unsigned long previousSensorMillis = 0;
unsigned long previousSentMillis = 0;
long sensorInterval = DEFAULT_SENSOR_INTERVAL; // 240 seconds, changeable from web UI
String oledTitle = DEFAULT_OLED_TITLE; // Default title, changeable from web UI
char buffer[128]; // Buffer for commands
bool useWiFiManager;
Preferences preferences;



// --- Function Prototypes ---
void handleRoot();
void handleSend();
void handleStatus();
void handleSetTitle();
void handleSetInterval(); // New handler for setting sensor interval
void readAndDisplaySensorData(String msg= "");
void sendSensorDataLora();
void processLoraSend();
float processGasData();
float processOxygenData();

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
        server.on("/setinterval", HTTP_POST, handleSetInterval); // New route for interval
        server.begin();
        Serial.println("HTTP server started");
        display.println("HTTP server started! :D");
        display.display();
    }
    delay(5000);
    display.clearDisplay();
    display.setCursor(0, 0);


    // Initial display update
    readAndDisplaySensorData();
}

void loop() {
    server.handleClient();
    processLoraSend(); // Check if we need to send a LoRa message
    ADS.setGain(ADS1X15_GAIN_2048MV);

    unsigned long currentMillis = millis();
    if (currentMillis - previousSensorMillis >= sensorInterval) {
      previousSensorMillis = currentMillis;
      readAndDisplaySensorData();
      sendSensorDataLora();

      int16_t tempValue = ADS.readADC(0);
      int16_t tempVoltage = tempValue * ADS.toVoltage();

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
       readAndDisplaySensorData(data); // Display the received packet on OLED
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

void sendSensorDataLora() {
    float gasVoltage = processGasData();
    float oxygenVoltage = processOxygenData();

    String data = "{\"Oxygen\":" + String(oxygenVoltage, 3) + ", \"Methane\":" + String(gasVoltage, 3) + "}";
    Serial.print("Sending sensor data via LoRa: ");
    Serial.println(data);

    lora.transferPacket((unsigned char*)data.c_str(), data.length(), Tx_and_ACK_RX_timeout);
}

// --- Web Server Handlers ---
void handleRoot() {
    String html = R"rawliteral(
<!DOCTYPE HTML><html><head>
<title>ESP32 LoRa Sender</title>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
  body { font-family: Arial, sans-serif; text-align: center; background-color: #f4f4f4; color: #333; }
  .container { max-width: 500px; margin: 30px auto; padding: 20px; background-color: #fff; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }
  h2 { color: #0056b3; }
  input[type=text], input[type=number] { width: calc(100% - 24px); padding: 12px; margin: 10px 0; border: 1px solid #ccc; border-radius: 4px; }
  input[type=submit] { background-color: #007bff; color: white; padding: 14px 20px; margin: 8px 0; border: none; border-radius: 4px; cursor: pointer; width: 100%; font-size: 16px; }
  input[type=submit]:hover { background-color: #0056b3; }
  input[type=submit]:disabled { background-color: #cccccc; }
  #status, #titleStatus, #intervalStatus { margin-top: 15px; font-size: 1.1em; font-weight: bold; min-height: 20px;}
  .success { color: #28a745; }
  .error { color: #dc3545; }
  hr { border: 0; height: 1px; background: #ddd; margin: 30px 0; }
</style>
</head><body>
<div class="container">
  <h2>LoRa Message Sender</h2>
  <form id="sendForm">
    <input type="text" id="message" name="message" placeholder="Enter message to send" required>
    <input type="submit" id="sendButton" value="Send Message">
  </form>
  <div id="status"></div>
  <hr>
  <h2>Update Display Title</h2>
  <form id="titleForm">
    <input type="text" id="newTitle" name="title" placeholder="New title (max 10 chars)" maxlength="10" required>
    <input type="submit" id="titleButton" value="Update Title">
  </form>
  <div id="titleStatus"></div>
  <hr>
  <h2>Update Sensor Interval</h2>
  <form id="intervalForm">
    <input type="number" id="newInterval" name="interval" placeholder="New interval in seconds (min 5)" min="5" required>
    <input type="submit" id="intervalButton" value="Update Interval">
  </form>
  <div id="intervalStatus"></div>
  <hr>
  <h2>Update Water Temperature</h2>
  <form id="waterTempForm">
    <input type="number" id="newWaterTemp" name="waterTemp" placeholder="New water temperature (°C)" required>
    <input type="submit" id="waterTempButton" value="Update Water Temperature">
  </form>
  <div id="waterTempStatus"></div>
</div>
<script>
  const sendForm = document.getElementById('sendForm');
  const sendButton = document.getElementById('sendButton');
  const statusDiv = document.getElementById('status');
  let pollingInterval;

  sendForm.addEventListener('submit', function(e) {
    e.preventDefault();
    const message = document.getElementById('message').value;
    
    sendButton.disabled = true;
    statusDiv.className = '';
    statusDiv.textContent = 'Sending...';

    fetch('/send', {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: 'message=' + encodeURIComponent(message)
    })
    .then(response => {
      if (response.ok) {
        pollingInterval = setInterval(checkStatus, 1000);
      } else { throw new Error('Server error.'); }
    })
    .catch(error => {
      statusDiv.className = 'error';
      statusDiv.textContent = 'Error: Could not send message.';
      sendButton.disabled = false;
    });
  });

  function checkStatus() {
    fetch('/status')
      .then(response => response.text())
      .then(statusText => {
        if (statusText === 'SUCCESS') {
          clearInterval(pollingInterval);
          statusDiv.className = 'success';
          statusDiv.textContent = 'Message Sent Successfully!';
          sendButton.disabled = false;
        } else if (statusText === 'FAILED') {
          clearInterval(pollingInterval);
          statusDiv.className = 'error';
          statusDiv.textContent = 'Failed: Message could not be sent.';
          sendButton.disabled = false;
        } else if (statusText === 'SENDING') {
          statusDiv.textContent = 'Waiting for confirmation...';
        }
      })
      .catch(error => {
        clearInterval(pollingInterval);
        statusDiv.className = 'error';
        statusDiv.textContent = 'Error: Lost connection to server.';
        sendButton.disabled = false;
      });
  }

  const titleForm = document.getElementById('titleForm');
  const titleButton = document.getElementById('titleButton');
  const titleStatusDiv = document.getElementById('titleStatus');

  titleForm.addEventListener('submit', function(e) {
    e.preventDefault();
    const newTitle = document.getElementById('newTitle').value;

    titleButton.disabled = true;
    titleStatusDiv.textContent = 'Updating...';

    fetch('/settitle', {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: 'title=' + encodeURIComponent(newTitle)
    })
    .then(response => response.text().then(text => ({ ok: response.ok, text })))
    .then(({ ok, text }) => {
      if (ok) {
        titleStatusDiv.className = 'success';
        titleStatusDiv.textContent = text;
      } else {
        titleStatusDiv.className = 'error';
        titleStatusDiv.textContent = 'Error: ' + text;
      }
      titleButton.disabled = false;
    })
    .catch(error => {
      titleStatusDiv.className = 'error';
      titleStatusDiv.textContent = 'Error: Could not update title.';
      titleButton.disabled = false;
    });
  });

  const intervalForm = document.getElementById('intervalForm');
  const intervalButton = document.getElementById('intervalButton');
  const intervalStatusDiv = document.getElementById('intervalStatus');

  intervalForm.addEventListener('submit', function(e) {
    e.preventDefault();
    const newInterval = document.getElementById('newInterval').value;

    intervalButton.disabled = true;
    intervalStatusDiv.textContent = 'Updating...';

    fetch('/setinterval', {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: 'interval=' + encodeURIComponent(newInterval)
    })
    .then(response => response.text().then(text => ({ ok: response.ok, text })))
    .then(({ ok, text }) => {
      if (ok) {
        intervalStatusDiv.className = 'success';
        intervalStatusDiv.textContent = text;
      } else {
        intervalStatusDiv.className = 'error';
        intervalStatusDiv.textContent = 'Error: ' + text;
      }
      intervalButton.disabled = false;
    })
    .catch(error => {
      intervalStatusDiv.className = 'error';
      intervalStatusDiv.textContent = 'Error: Could not update interval.';
      intervalButton.disabled = false;
    });
  });
</script>
</body></html>)rawliteral";
    server.send(200, "text/html", html);
}

void handleSend() {
    if (server.hasArg("message")) {
        if (loraStatus == IDLE || loraStatus == ACK_SUCCESS || loraStatus == ACK_FAILED) {
            messageToSend = server.arg("message");
            loraStatus = SENDING;
            readAndDisplaySensorData("Send: " + messageToSend);
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
            readAndDisplaySensorData("Send Success!");
            loraStatus = IDLE; // Reset status after reporting
            break;
        case ACK_FAILED:
            statusMessage = "FAILED";
            readAndDisplaySensorData("Send Failed!");
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
            readAndDisplaySensorData(); // Redraw display with new title
            server.send(200, "text/plain", "Title updated successfully!");
        }
    } else {
        server.send(400, "text/plain", "400: Invalid Request");
    }
}

/**
 * @brief Handles POST request to update the sensor reading interval.
 * Validates the input to be a number >= 5.
 */
void handleSetInterval() {
    if (server.hasArg("interval")) {
        String intervalStr = server.arg("interval");
        long newInterval = intervalStr.toInt(); // Convert string to long

        // Validation: Must be a number and at least 5 seconds.
        // .toInt() returns 0 for non-numeric strings, which fails the check.
        if (newInterval >= 5) {
            sensorInterval = newInterval * 1000; // Convert seconds to milliseconds
            Serial.print("Sensor interval updated to: ");
            Serial.print(newInterval);
            Serial.println(" seconds.");
            readAndDisplaySensorData("New sensor interval: " + String(newInterval) + " s");
            server.send(200, "text/plain", "Interval updated to " + String(newInterval) + " seconds.");
        } else {
            // Send error if validation fails
            server.send(400, "text/plain", "Invalid interval. Must be at least 5 seconds.");
        }
    } else {
        // Send error if argument is missing
        server.send(400, "text/plain", "400: Invalid Request (missing interval).");
    }
}

// --- Display Functions ---
void readAndDisplaySensorData(String msg) {
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
      display.print("Password: ");
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
    display.println(""); // Spacer

    // Display methane gas data
    float gas = processGasData();
    display.print("Methane: ");
    display.print(gas, 2);
    display.println(" V");
    
    // Display Dissolved Oxygen data
    float oxygen= processOxygenData();
    display.print("Oxygen: ");
    display.print(oxygen, 2);
    display.println(" V");
    display.println(""); // Spacer

    if (msg.length() > 0) {
      display.setTextSize(1);
      display.println(msg);
    }

    display.display();
}

// --- Gas Data Processing Function ---
float processGasData() {
    int16_t gasValue = ADS.readADC(0); // Read gas sensor value
    // calculate voltage from integer and display it 
    float voltage = ADS.toVoltage(1) * gasValue;
    Serial.println("Gas sensor voltage: " + String(voltage, 3) + " V");
    return voltage;
}

float processOxygenData() {
    int16_t oxygenValue = ADS.readADC(3); // Read oxygen sensor value
    // calculate voltage from integer and display it 
    float voltage = ADS.toVoltage(1) * oxygenValue;
    Serial.println("Oxygen sensor voltage: " + String(voltage, 3) + " V");
    return voltage;
}