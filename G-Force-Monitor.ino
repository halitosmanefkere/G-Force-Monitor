#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <MPU6050.h>
#include <WiFi.h>
#include <WebServer.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1  // No reset pin
#define LED_PIN 2         // GPIO2 LED for acceleration blinking

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
MPU6050 mpu;

int16_t ax, ay, az;
float gForceX, gForceY, gForceZ;
float maxGForceX = 0, maxGForceY = 0;
float esp32TempCelsius;
float offsetX = 0.0, offsetY = 0.0, offsetZ = 0.0;
unsigned long previousMillis = 0;

// Wi-Fi credentials (Set ESP32 as Access Point)
const char *ssid = "G-Force Monitor";
const char *password = "123456789";  // You can make it open or set your own password

WebServer server(80); // Web server on port 80

// Reads ESP32's internal temperature
extern "C" uint8_t temprature_sens_read();
float readESP32Temperature() {
    return (((temprature_sens_read() - 32) * 5.0 / 9.0)-30);
}

// Function to calibrate sensor at startup
void calibrateSensor() {
    // Read the current raw accelerometer data
    mpu.getAcceleration(&ax, &ay, &az);

    // Set the offsets to current readings
    offsetX = ax / 16384.0;
    offsetY = -ay / 16384.0;  // Invert the Y-axis as done in calculations
    offsetZ = az / 16384.0;

    Serial.println("Sensor calibrated.");
}

// Function to display G-force graph on OLED
void displayGForceGraph(float gForceX, float gForceY) {
    int centerX = SCREEN_WIDTH / 2;
    int centerY = SCREEN_HEIGHT / 2;

    // Draw concentric circles representing G-force levels
    display.drawCircle(centerX, centerY, 10, WHITE);  // 0.25G
    display.drawCircle(centerX, centerY, 20, WHITE);  // 0.50G
    display.drawCircle(centerX, centerY, 30, WHITE);  // 0.75G
    display.drawCircle(centerX, centerY, 40, WHITE);  // 1.00G

    // Draw X and Y axis
    display.drawLine(centerX - 50, centerY, centerX + 50, centerY, WHITE);  // X axis
    display.drawLine(centerX, centerY - 50, centerX, centerY + 50, WHITE);  // Y axis

    // Scale G-force values to fit within graph range
    int markerX = centerX + (int)(gForceX * 40);  // Scale factor to fit within the circle
    int markerY = centerY + (int)(gForceY * 40);

    // Draw marker showing current G-force
    display.fillCircle(markerX, markerY, 4, WHITE);

    // Display G-force values numerically on top-left
    display.setCursor(0, 0);
    display.setTextSize(1);
    display.print("X: ");
    display.print(gForceX, 1);  // Display one decimal place
    display.setCursor(0, 10);
    display.print("Y: ");
    display.print(gForceY, 1);  // Display one decimal place

    // Show temperature in Celsius in the bottom-right corner, integer part only
    display.setCursor(90, 54);
    display.print("T:");
    display.print((int)esp32TempCelsius);  // Display only the integer part
    display.println("C");
}

// Function to display ACC/BRAKE/TURN/IDLE label on the top-right corner
void displayAccelerationStatus(float gForceX, float gForceY) {
    display.setCursor(90, 0);  // Top-right position
    display.setTextSize(1);    // Make the text larger

    // Show "ACC", "BRAKE", "TURN" or "IDLE" based on G-force values
    if (gForceY > 0.1) {                 // Positive Y-axis indicates braking
        display.println("BRAKE");  
    } else if (gForceY < -0.1) {         // Negative Y-axis indicates acceleration
        display.println("ACC");  
    } else if (gForceX > 0.1) {           // Positive X-axis indicates a left turn
        display.println("TURN LEFT");   
    } else if (gForceX < -0.1) {          // Negative X-axis indicates a right turn
        display.println("TURN RIGHT");  
    } else {
        display.println("IDLE");            // Idle state when not accelerating, braking, or turning
    }
}

// Function to send JSON data for real-time updates
void handleData() {
    String json = "{";
    json += "\"gForceX\":" + String(gForceX, 1) + ",";  // One decimal place
    json += "\"gForceY\":" + String(gForceY, 1) + ",";
    json += "\"temperature\":" + String((int)esp32TempCelsius) + ",";
    json += "\"maxGForceX\":" + String(maxGForceX, 1) + ",";
    json += "\"maxGForceY\":" + String(maxGForceY, 1) + ",";

    // Calculate the marker's position for visualization on the web page
    int markerXWeb = 100 + (int)(gForceX * 40);  // X-axis scaled for web
    int markerYWeb = 100 - (int)(gForceY * 40);  // Y-axis scaled for web

    json += "\"markerX\":" + String(markerXWeb) + ",";
    json += "\"markerY\":" + String(markerYWeb) + ",";

    // Determine status based on the updated G-force orientation
    if (gForceY < -0.1) {                   // Negative Y-axis indicates acceleration
        json += "\"status\":\"ACC\"";
    } else if (gForceY > 0.1) {             // Positive Y-axis indicates braking
        json += "\"status\":\"BRAKE\"";
    } else if (gForceX > 0.1) {             // Positive X-axis indicates a left turn
        json += "\"status\":\"TURN L\"";
    } else if (gForceX < -0.1) {            // Negative X-axis indicates a right turn
        json += "\"status\":\"TURN R\"";
    } else {
        json += "\"status\":\"IDLE\"";
    }
    json += "}";

    server.send(200, "application/json", json);
}

// Function to serve web page showing G-force data and temperature
void handleRoot() {
    String html = "<html><head><title>G-Force Monitor</title>";
    html += "<style>body { font-family: Arial, sans-serif; background-color: #1c1c1c; color: #fff; margin: 0; padding: 0; }";
    html += "h1 { background-color: #ff0000; padding: 20px 0; margin: 0; font-size: 2em; text-align: center; }";
    html += ".container { display: flex; justify-content: center; align-items: center; flex-direction: column; padding: 20px; }";
    html += ".circle { width: 200px; height: 200px; border: 5px solid #fff; border-radius: 50%; position: relative; }";
    html += ".marker { width: 20px; height: 20px; background-color: red; border-radius: 50%; position: absolute; transform: translate(-50%, -50%); transition: all 0.2s ease; }";
    html += ".cross { position: absolute; width: 100%; height: 100%; top: 0; left: 0; }";
    html += ".cross::before, .cross::after { content: ''; position: absolute; background-color: #fff; }";
    html += ".cross::before { width: 100%; height: 2px; top: 50%; left: 0; }";
    html += ".cross::after { width: 2px; height: 100%; top: 0; left: 50%; }";
    html += ".data { font-size: 1.2em; margin: 10px 0; }";
    html += ".status { font-size: 2.5em; margin-top: 20px; font-weight: bold; color: #ff0000; }";
    html += "</style>";

    // Add JavaScript for dynamic updating every second
    html += "<script>function reloadData() { fetch('/data').then(response => response.json()).then(data => {";
    html += "document.getElementById('gForceX').innerText = data.gForceX.toFixed(1) + ' g';";
    html += "document.getElementById('gForceY').innerText = data.gForceY.toFixed(1) + ' g';";
    html += "document.getElementById('temperature').innerText = data.temperature + ' C';";
    html += "document.getElementById('maxGForceX').innerText = data.maxGForceX.toFixed(1) + ' g';";
    html += "document.getElementById('maxGForceY').innerText = data.maxGForceY.toFixed(1) + ' g';";
    html += "let marker = document.getElementById('marker');";
    html += "marker.style.left = (data.markerX) + 'px'; marker.style.top = (data.markerY) + 'px';";
    html += "let statusElement = document.getElementById('status');";
    html += "statusElement.innerText = data.status;";
    html += "if (data.status === 'ACC') { statusElement.style.color = 'red'; }";
    html += "else if (data.status === 'BRAKE') { statusElement.style.color = 'orange'; }";
    html += "else { statusElement.style.color = '#4CAF50'; }";
    html += "}); } setInterval(reloadData, 1000);</script></head>";

    // Start body content
    html += "<body><h1>G-Force Monitor</h1><div class='container'>";
    html += "<div class='data'>Max G-force X: <span id='maxGForceX'></span></div>";
    html += "<div class='data'>Max G-force Y: <span id='maxGForceY'></span></div>";
    html += "<div class='data'>G-force X: <span id='gForceX'></span></div>";
    html += "<div class='data'>G-force Y: <span id='gForceY'></span></div>";
    html += "<div class='data'>Temperature: <span id='temperature'></span></div>";
    html += "<div class='circle'><div class='cross'></div>";
    html += "<div id='marker' class='marker'></div></div>";
    html += "<div style='text-align: center;' class='status' id='status'></div>";
    html += "</div><footer style='text-align: center; padding: 10px; background-color: #1c1c1c; color: #fff;'>Made with passion by Halit Efkere &copy; 2024</footer></body></html>";

    server.send(200, "text/html", html);  // Send HTML to web browser
}

void setup() {
    // Initialize serial communication for debugging
    Serial.begin(9600);

    // Initialize MPU6050
    Wire.begin();
    mpu.initialize();

    if (!mpu.testConnection()) {
        Serial.println("MPU6050 connection failed!");
        while (1);
    }

    // Initialize OLED display
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        Serial.println(F("SSD1306 allocation failed"));
        while (1);
    }

    // Show welcome screen
    display.clearDisplay();
    display.setTextSize(1, 2);
    display.setTextColor(WHITE);
    display.setCursor(0, 0);
    display.println(F("Made by Halit Efkere"));

    // Add 3-pixel spacing
    display.setCursor(0, 16);  // Spacing added between the name and title

    // Increase the font size for the title
    display.setTextSize(2, 2);
    display.println(F(" G - Force"));
    display.setCursor(0, 32);  // Start next line for "Monitor"
    display.println(F(" Monitor"));
    display.display();
    delay(2000);  // Display welcome screen for 5 seconds

    // Set up LED pin
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    // Set up Wi-Fi as Access Point (AP)
    WiFi.softAP(ssid, password);
    Serial.println("WiFi AP started. Connect to SSID: " + String(ssid));

    // Set up web server routes
    server.on("/", handleRoot);
    server.on("/data", handleData);  // Add route for data updates
    server.begin();

    // Calibrate the sensor to set zero points
    calibrateSensor();
}

void loop() {
    // Read raw accelerometer data
    mpu.getAcceleration(&ax, &ay, &az);

    // Convert raw accelerometer data to G-force and subtract offsets
    gForceX = (ax / 16384.0) - offsetX;
    gForceY = (-ay / 16384.0) - offsetY;
    gForceZ = (az / 16384.0) - offsetZ;

    // Apply a small threshold to treat minor values as zero
    if (abs(gForceX) < 0.05) gForceX = 0.0;
    if (abs(gForceY) < 0.05) gForceY = 0.0;
    if (abs(gForceZ) < 0.05) gForceZ = 0.0;

    // Update max G-forces
    if (abs(gForceX) > abs(maxGForceX)) maxGForceX = gForceX;
    if (abs(gForceY) > abs(maxGForceY)) maxGForceY = gForceY;

    // Get the internal temperature
    esp32TempCelsius = readESP32Temperature();

    // Clear the display
    display.clearDisplay();

    // Display G-force data and visualize in a circular graph
    displayGForceGraph(gForceX, gForceY);

    // Display ACC/BRAKE/TURN/IDLE label based on G-force
    displayAccelerationStatus(gForceX, gForceY);

    // Update the display
    display.display();

    // Handle web server client requests
    server.handleClient();

    // Control LED based on status
    if (gForceY < -0.1) {  // ACC - faster blinking
        digitalWrite(LED_PIN, HIGH);
        delay(100);  // Short delay for faster blinking
        digitalWrite(LED_PIN, LOW);
        delay(100);
    } else if (gForceY > 0.1) {  // BRAKE - slower blinking
        digitalWrite(LED_PIN, HIGH);
        delay(500);  // Longer delay for slower blinking
        digitalWrite(LED_PIN, LOW);
        delay(500);
    } else {  // IDLE - LED always on
        digitalWrite(LED_PIN, HIGH);
    }
}
