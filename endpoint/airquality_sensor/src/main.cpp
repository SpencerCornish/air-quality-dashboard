#include <ESP8266WiFiMulti.h>
#define DEVICE "Indoor_Air_Sensor"

#include <InfluxDbClient.h>
#include <InfluxDbCloud.h>
#include <AirGradient.h>
#include <WiFiManager.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <Wire.h>
#include <SSD1306Wire.h>
#include <Credentials.h>
#include <fonts.h>

ESP8266WiFiMulti wifiMulti;

// Set timezone string according to https://www.gnu.org/software/libc/manual/html_node/TZ-Variable.html
// Examples:
//  Pacific Time: "PST8PDT"
//  Eastern: "EST5EDT"
//  Japanesse: "JST-9"
//  Central Europe: "CET-1CEST,M3.5.0,M10.5.0/3"
#define TZ_INFO "MST7MDT"

// InfluxDB client instance with preconfigured InfluxCloud certificate
InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);

AirGradient ag = AirGradient();

SSD1306Wire display(0x3c, SDA, SCL);

// Data point
Point sensor(DEVICE);

void showTextRectangle(String ln1, String ln2, int progress)
{
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setFont(Roboto_Mono_10);

  display.drawString(64, 16, ln1);
  display.drawString(64, 35, ln2);
  display.drawProgressBar(32, 50, 62, 10, progress);

  display.display();
}

void showStatusText(int co2, int pm25, int temp, int rh, bool writeOccurred)
{
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setFont(Roboto_Mono_10);
  display.drawString(64, 16, "CO2: " + String(co2));
  display.drawString(64, 29, "PM2.5: " + String(pm25));
  display.drawString(64, 42, String(temp) + "C " + String(rh) + "% RH");
  // display.drawIco16x16
  display.display();
}

void setup()
{
  Serial.begin(115200);

  display.init();
  display.flipScreenVertically();

  showTextRectangle("Sensor Init", "Loading", 0);
  ag.CO2_Init();
  ag.PMS_Init();
  ag.TMP_RH_Init(0x44);

  showTextRectangle("Wifi Init", "Loading", 25);

  // Setup wifi
  WiFi.mode(WIFI_STA);
  wifiMulti.addAP(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Connecting to wifi");
  int tick = 0;
  while (wifiMulti.run() != WL_CONNECTED)
  {
    Serial.print(".");
    delay(100);
    tick++;

    char str[100];
    sprintf(str, "%.*s", tick % 3, "..................");
    showTextRectangle("Wifi Init", "Loading" + String(str), 25);
  }

  showTextRectangle("Connected!", WiFi.localIP().toString(), 50);
  delay(1000);

  // Add tags
  sensor.addTag("device", DEVICE);
  sensor.addTag("SSID", WiFi.SSID());
  sensor.addTag("LocalIP", WiFi.localIP().toString());

  showTextRectangle("Time Sync", "", 75);
  timeSync(TZ_INFO, "pool.ntp.org", "time.nis.gov");

  // Check server connection
  if (client.validateConnection())
  {
    Serial.print("Connected to InfluxDB: ");
    Serial.println(client.getServerUrl());
    showTextRectangle("InfluxDB", "Connected", 100);
  }
  else
  {
    Serial.print("InfluxDB connection failed: ");
    Serial.println(client.getLastErrorMessage());
    Serial.println(client.getLastStatusCode());
    showTextRectangle("InfluxDB", "failed!", 0);
    exit(1);
  }
}

long loopmilis = 0;
long lastTransmit = -1;
long transmitInterval = 60000;
long dataRefreshInterval = 2000;
void loop()
{
  int curMillis = millis();
  int co2 = ag.getCO2_Raw();
  int pm25 = ag.getPM2_Raw();
  ;
  TMP_RH trh = ag.periodicFetchData();

  showStatusText(co2, pm25, trh.t, trh.rh, false);

  if (curMillis >= transmitInterval + lastTransmit)
  {
    sensor.clearFields();
    sensor.addField("wifi_rssi", WiFi.RSSI());
    sensor.addField("co2_ppm", co2);
    sensor.addField("pm2.5", pm25);
    sensor.addField("temp", trh.t);
    sensor.addField("rh", trh.rh);
    Serial.print("Writing: ");
    Serial.println(sensor.toLineProtocol());

    // Check WiFi connection and reconnect if needed
    if (wifiMulti.run() != WL_CONNECTED)
    {
      Serial.println("Wifi connection lost");
    }

    // Write point
    if (!client.writePoint(sensor))
    {
      Serial.print("InfluxDB write failed: ");
      Serial.println(client.getLastErrorMessage());
    }
    lastTransmit = curMillis;

    showStatusText(co2, pm25, trh.t, trh.rh, true);
  }

  delay(dataRefreshInterval);
}
