/*
  ESPCaptiveImagePortal

  Copyright (c) 2020 Dale Giancono. All rights reserved..
  This file is a an application for a captive image portal with upload page.
  WRITE MORE STUFF HERE.


  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "ESP8266WiFi.h"
#include "ESPAsyncTCP.h"
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include "FS.h"
#include "ESPStringTemplate.h"

#define DEFAULT_SSID                  "ESPCaptiveImagePortal"
#define HTTP_USER_AUTH                "supersecretadmin"
#define HTTP_PASS_AUTH                "yomamarama"
#define SSID_FILEPATH                 "/ssid.txt"
#define VISIT_COUNTER_FILEPATH        "/visitcounter.txt"

#define IMAGES_DIRECTORY              "/images"

void handleCaptiveImagePortal(AsyncWebServerRequest *request);
void handleUploadPage(AsyncWebServerRequest *request);

String getSSIDString(void);
String getVisitCounter(void);
void updateSSIDString(const String& content);
void updateVisitCounter(const String& counter);
void incrementVisitCounter();

/* Store different HTML elements in flash. Descriptions of the various
  tokens are included above each element that has tokens.*/
static const char _PAGEHEADER[]   PROGMEM = "<!DOCTYPE html><html><head><style>body{background-color: black; color:white;}</style></head><body>";
static const char _TITLE[]        PROGMEM = "<hr><h1>%TITLE%</h1><hr>";
static const char _SUBTITLE[]     PROGMEM = "<hr><h2>%SUBTITLE%</h2><hr>";

/* %IMAGEURI% equals the uri of the image to be served. */
/* %WIDTH% equals the width of the image on the webpage. */
/* %HEIGHT% equals the height of the image on the webpage. */
static const char _EMBEDIMAGE[]   PROGMEM = "<img src=\"%IMAGEURI%\" alt=\"%IMAGEURI%\" width=\"100%\">";
static const char _DELETEIMAGE[]  PROGMEM = "<form action=\"/delete\"> <img src=\"%IMAGEURI%\" alt=\"%IMAGEURI%\" width=\"20%\"> <input type=\"submit\" value=\"delete\" name=\"%IMAGEURI%\"></form>";
static const char _ADDIMAGE[]     PROGMEM = "<form action=\"/upload\" method=\"post\" enctype=\"multipart/form-data\">Image to Upload:<input type=\"file\" value=\"uploadFile\" name=\"uploadFile\" accept=\"image/*\"><input type=\"submit\" value=\"Upload Image\" name=\"submit\"></form>";
static const char _SSIDEDIT[]     PROGMEM = "<form action=\"/ssidedit\">New SSID (You will be disconnected from WiFi): <input type=\"text\" value=\"\" name=\"SSID\"><input type=\"submit\" name=\"submit\"></form>";

static const char _BREAK[]        PROGMEM = "<br>";
static const char _PAGEFOOTER[]   PROGMEM = "</body></html>";

/* Declare buffer that will be used to fill with the string template. Care \
  must be taken to ensure the buffer is large enough.*/
static char pageBuffer[1500];

/* Create DNS server instance to enable captive portal. */
DNSServer dnsServer;
/* Create webserver instance for serving the StringTemplate example. */
AsyncWebServer server(80);
/* Soft AP network parameters */
IPAddress apIP(192, 168, 4, 1);
IPAddress netMsk(255, 255, 255, 0);

Dir imagesDirectory;

void setup()
{
  /* Start the serial so we can debug with it*/
  Serial.begin(115200);
  SPIFFS.begin();
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, true);
  /* Open the SPIFFS root directory and serve each file with a uri of the filename */
  imagesDirectory = SPIFFS.openDir(IMAGES_DIRECTORY);
  while (imagesDirectory.next())
  {
    server.serveStatic(
      imagesDirectory.fileName().c_str(),
      SPIFFS,
      imagesDirectory.fileName().c_str());
      Serial.print("sERVING STATICALLY: ");
      Serial.println(imagesDirectory.fileName().c_str());
  }

  if(!SPIFFS.exists(SSID_FILEPATH))
  {
    updateSSIDString(DEFAULT_SSID);
    Serial.println("ssid.txt does not exist, creating with default value");
    Serial.print("DEFAULT_SSID: ");
    Serial.println(DEFAULT_SSID);
  } 

  if(!SPIFFS.exists(VISIT_COUNTER_FILEPATH))
  {
    updateVisitCounter(String(0));
    Serial.println("visitCounter.txt does not exist, creating with default value");
    Serial.println(0);
  } 
  
  /* Configure access point with static IP address */
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, netMsk);
  WiFi.softAP(getSSIDString().c_str());
  Serial.println("WiFi started...");

  /* Start DNS server for captive portal. Route all requests to the ESP IP address. */
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(53, "*", apIP);
  Serial.println("DNS Server started...");


  server.on("/supersecretpage", HTTP_GET, handleUploadPage);
  server.on("/delete", HTTP_GET, handleDelete);
  server.on("/ssidedit", HTTP_GET, handleSsidEdit);

  server.onFileUpload(handleFileUpload);

  /* Define the handler for when the server receives a GET request for the root uri. */
  server.onNotFound(handleCaptiveImagePortal);

  /* Begin the web server */
  server.begin();
}

void loop()
{
  static int previousNumberOfStations = -1;
  
  dnsServer.processNextRequest();

  int numberOfStations = WiFi.softAPgetStationNum();
  if(numberOfStations != previousNumberOfStations)
  {
    if(numberOfStations < previousNumberOfStations)
    {
      Serial.println("Turning off led");
      Serial.println(numberOfStations);
      digitalWrite(LED_BUILTIN, true);
    }
    else
    {
      Serial.println("Turning on led");
      Serial.println(numberOfStations);

      digitalWrite(LED_BUILTIN, false);
      incrementVisitCounter();
    }

    previousNumberOfStations = numberOfStations;
  }

  
}

void handleCaptiveImagePortal(AsyncWebServerRequest *request)
{
  Serial.println("handleCaptiveImagePortal");
  
  String filename;
  ESPStringTemplate pageTemplate(pageBuffer, sizeof(pageBuffer));
  /* This handler is supposed to get the next file in the SPIFFS root directory and
    server it as an embedded image on a HTML page. It does that by getting the filename
    from the next file in the rootDirectory Dir instance, and setting it as the IMAGEURI
    token in the _EMBEDIMAGE HTML element.*/

  /* Get next file in the root directory. */
  if(!imagesDirectory.next())
  {
    imagesDirectory.rewind();
    imagesDirectory.next();
  }

  filename = imagesDirectory.fileName();  
  pageTemplate.add_P(_PAGEHEADER);
  if(filename != "")
  {
    pageTemplate.add_P(_EMBEDIMAGE, "%IMAGEURI%", filename.c_str());
  }
  else
  {
    pageTemplate.add_P(PSTR("<h1>NO IMAGES UPLOADED!!!</h1>"));
  }
  pageTemplate.add_P(_PAGEFOOTER);
  const char* page =  pageTemplate.get();
  request->send(200, "text/html", page);
}

void handleUploadPage(AsyncWebServerRequest *request)
{
  Serial.println("handleUploadPage");
  if(!request->authenticate(HTTP_USER_AUTH, HTTP_PASS_AUTH))
  {
    return request->requestAuthentication();
  }
  String filename;
  ESPStringTemplate pageTemplate(pageBuffer, sizeof(pageBuffer));
  pageTemplate.add_P(_PAGEHEADER);
  pageTemplate.add_P(_TITLE, "%TITLE%", "Super Secret Page");
  pageTemplate.add_P(_SUBTITLE, "%SUBTITLE%", "Visit Count");
  pageTemplate.add(getVisitCounter().c_str());
  pageTemplate.add_P(_SUBTITLE, "%SUBTITLE%", "Delete Files");

  imagesDirectory.rewind();
  while (imagesDirectory.next())
  {
    filename = imagesDirectory.fileName();
    if(filename != "")
    {
      pageTemplate.add_P(_DELETEIMAGE, "%IMAGEURI%", filename.c_str());
    }
  }

  pageTemplate.add_P(_SUBTITLE, "%SUBTITLE%", "Add File");
  pageTemplate.add_P(_ADDIMAGE);
  pageTemplate.add_P(_SUBTITLE, "%SUBTITLE%", "Change SSID");
  pageTemplate.add_P(_SSIDEDIT);
  pageTemplate.add_P(_PAGEFOOTER);
  request->send(200, "text/html", pageTemplate.get());
}

void handleDelete(AsyncWebServerRequest *request)
{
  Serial.println("handleDelete");
  if(!request->authenticate(HTTP_USER_AUTH, HTTP_PASS_AUTH))
  {
    return request->requestAuthentication();
  }
  for (int i = 0; i < request->params(); i++)
  {
    AsyncWebParameter* p = request->getParam(i);
    if((p->name().c_str() != ""))
    {
      SPIFFS.remove(p->name().c_str());
      Serial.print("Deleted ");
      Serial.println(p->name().c_str());
    }
  }
  request->redirect("/supersecretpage");
}

void handleFileUpload(AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final)
{
  static String rootFileName;
  File uploadfile;

  if (!index)
  {
    if(!request->authenticate(HTTP_USER_AUTH, HTTP_PASS_AUTH))
    {
      return request->requestAuthentication();
    }
    rootFileName = "/images/" + filename;
    uploadfile = SPIFFS.open(rootFileName.c_str(), "w+");
    Serial.print("Starting handleFileUpload for: ");
    Serial.println(rootFileName);

  }
  else
  {
     uploadfile = SPIFFS.open(rootFileName.c_str(), "a+");   
  }

  uploadfile.write(data, len);
  
  if (final)
  {
    Serial.println("Ending handleFileUpload");
    uploadfile.close();
    server.serveStatic(
      rootFileName.c_str(),
      SPIFFS,
      rootFileName.c_str());
    request->redirect("/supersecretpage");
  }
  else
  {
    uploadfile.close();
  }
}

void handleSsidEdit(AsyncWebServerRequest *request)
{
  Serial.println("handleSsidEdit");
  if(!request->authenticate(HTTP_USER_AUTH, HTTP_PASS_AUTH))
  {
    return request->requestAuthentication();
  }

  request->send(200, "text/html", "Changing SSID. You will be disconnected. Please reconnect.");
  AsyncWebParameter* p = request->getParam("SSID");
  if(p->value() != "")
  {
    updateSSIDString(p->value());
    WiFi.softAPdisconnect(true);
    WiFi.softAP(p->value().c_str());  
    Serial.print("set new ssid for WiFi: ");
    Serial.println(p->value().c_str());
  }
  else
  {
    request->redirect("/supersecretpage");
  }
}

String getSSIDString(void)
{
  String ssid_string;
  File ssid = SPIFFS.open(SSID_FILEPATH, "r");
  while(ssid.available())
  {
    ssid_string += (char)ssid.read();
  }
  ssid.close();

  return ssid_string;
}

String getVisitCounter(void)
{
  String value;
  File counter = SPIFFS.open(VISIT_COUNTER_FILEPATH, "r");
  while(counter.available())
  {
    value += (char)counter.read();
  }
  counter.close();  
  return value;
}

void updateSSIDString(const String& content)
{
  File ssid = SPIFFS.open(SSID_FILEPATH, "w");
  ssid.print(content);      
  ssid.close();  
}

void updateVisitCounter(const String& counter)
{
  File visitCounter = SPIFFS.open(VISIT_COUNTER_FILEPATH, "w");
  visitCounter.print(counter);      
  visitCounter.close();
}

void incrementVisitCounter()
{  
  Serial.println("incrementVisitCounter");
  String counter = getVisitCounter();
  Serial.println(counter);

  int int_count = counter.toInt();
    Serial.println(int_count);

  int_count++;
      Serial.println(int_count);

  updateVisitCounter(String(int_count));
}
