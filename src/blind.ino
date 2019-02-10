/**************************************************************
 * ROLLER BLINDS
 * 
 * Build options:
 *  Board:          Lolin(Wemos) D1 R2 & mini
 *  Upload speed:   921600
 *  Cpu frequency:  80MHz
 *  Flash size:     4M (1M spiffs)
 *  Debug port:     disabled
 *  Debug level:    none
 *  IwIP variant:   v2 Lower Memory
 *  VTables:        Flash
 *  Erase flash:    only sketch
 *  
 * -----------------------------------------------------------
 * VERSION
 * 0.1.0    20181203  Initial Version
 * 0.2.0    20181207  Added doClose will stop an opening blind
 *                          doOpen will stop a closing blind
 * 0.3.0    20181209  Added master enable/disable in config
 * 0.4.0    20181224  Added pages for state, current state 
 *                    & buttons for open & close
 * 0.5.0    20181225  Added mqtt control, added  mqtt stop cmd
 * 0.6.0    20181226  Reworked statemachine
 * 0.7.0    20181227  Added mqtt LWT
 *                    Added hostname support (mqtt_server)
 * 0.8.0    20181228  Added rsyslog
 * 0.9.0    20181229  Finetuned mqtt / HA integration (state retaining)
 * 0.9.1    20181229  Added Info page
 *                    Added links to all libraries used
 *                    Optimized comments
 * 0.9.2    20190104  Publish without retaining; always publish current state at connect.
 *                    Mqtt or webinterface close or open will always close or open, 
 *                     independent of the current state. Open or Close via switch, 
 *                     doOpen will open or stop / doClose will close or stop 
 *                     (depending on the current state).
 *                    digital in pins now require external pullup (opened/closed and doOpen and doClose)
 * 0.9.3    20190108  Use DebouncedSwitch library for debounding and EMI prevention.
 *                    Store lastConnectAttempt time at the end of the PubSubClient connect attempt
 *                    Correctly set and use hostname for dhcp / WiFi access
 * 0.9.4    20190122  Added Build options
 *                    Implemented WiFi reconnect loop, to allow WiFi to reconnect after failure.
 *                    WiFi reconnect: https://github.com/esp8266/Arduino/issues/4166
 * 0.9.5    20190210  Resend LWT every 5 minutes...
 **************************************************************/
  const char* VERSION = "0.9.5 (20190210)";
  const char* COPYRIGHT = "(c) Janssen Development, 2019.";
/***********************se***************************************
 *    
 **************************************************************/
 
#include <FS.h>                   // this needs to be first, or it all crashes and burns...
#include <ESP8266WiFi.h>          // https://github.com/esp8266/Arduino/tree/master/libraries/ESP8266WiFi
#include <DNSServer.h>            // https://github.com/esp8266/Arduino/tree/master/libraries/DNSServer
#include <Dns.h>
#include <ESP8266WebServer.h>     // https://github.com/esp8266/Arduino/tree/master/libraries/ESP8266WebServer
#include <WiFiManager.h>          // https://github.com/tzapu/WiFiManager
#include <ArduinoJson.h>          // https://github.com/bblanchon/ArduinoJson
#include <PubSubClient.h>         // https://pubsubclient.knolleary.net/api.html
#include <WiFiUdp.h>              // https://www.arduino.cc/en/Reference/WiFiUDPConstructor
#include <Syslog.h>               // https://github.com/arcao/Syslog
#include <DebouncedSwitch.h>

// **************************************************
// CONSTANTS:

// state constants
const int STATE_UNKNOWN = 0;
const int STATE_OPEN = 1;
const int STATE_CLOSED = 2;
const int STATE_OPENING = 3;
const int STATE_CLOSING = 4;

//MQTT topic endings:
const char * MQTT_STATE = "/state";
const char * MQTT_DO = "/do";
const char * MQTT_LWT = "/LWT";

const char * MQTT_LWT_OFFLINE = "Offline";
const char * MQTT_LWT_ONLINE = "Online";

// state names
const char * NAME_UNKNOWN = "UNKNOWN";
const char * NAME_OPEN =    "OPEN";
const char * NAME_CLOSED =  "CLOSED";
const char * NAME_OPENING = "OPENING";
const char * NAME_CLOSING = "CLOSING";

const char* STATE_NAMES[5] = { NAME_UNKNOWN, NAME_OPEN, NAME_CLOSED, NAME_OPENING, NAME_CLOSING };

// for config save/retrieve (json parameter names):
const char* ENABLE = "enable";                // master enable/disable
const char* HOSTNAME = "hostname";            // hostname
const char* PORT = "port";                    // webserver port
const char* SYSLOG_ENABLE = "syslog_enable";  // syslog enable/disable
const char* SYSLOG_SERVER = "syslog_server";  // syslog hostname
const char* SYSLOG_PORT = "syslog_port";      // syslogport
const char* MQTT_ENABLE = "mqtt_enable";      // enable mqtt
const char* MQTT_SERVER = "mqtt_server";      // mqtt_server name
const char* MQTT_PORT = "mqtt_port";          // port for mqtt server
const char* MQTT_USER = "mqtt_user";          // user for mqtt server
const char* MQTT_PWD = "mqtt_pwd";            // password for mqtt server

const char* PIN_IN1 = "pin_in1";              // pin to signal IN2 to Hbridge I298n
const char* PIN_IN2 = "pin_in2";              // pin to signal IN2 to Hbridge I298n
const char* PIN_OPENED = "pin_opened";        // pin/switch to inidcate the blind is opened
const char* PIN_CLOSED = "pin_closed";        // pin/switch to inidcate the blind is closed
const char* PIN_DOOPEN = "pin_doopen";        // pin/switch indicating that we want to open the blind
const char* PIN_DOCLOSE = "pin_doclose";      // pin/switch indicating that we want to close the blind

const char* TIMEOUT = "timeout";              // timeout in seconds for opening or closing the blinds

// define your default values here, if there are different values in config.json.
const bool DEF_ENABLE = false;
const char* DEF_HOSTNAME = "blind";
const int DEF_PORT = 80;
const bool DEF_SYSLOG_ENABLE = false;
const char* DEF_SYSLOG_SERVER = "";
const int DEF_SYSLOG_PORT = 514;
const char* DEF_MQTT_SERVER = "mqttserver";
const bool DEF_MQTT_ENABLE = false;
const int DEF_MQTT_PORT = 1883;
const int DEF_IN1 = D0;
const int DEF_IN2 = D1;
const int DEF_OPENED = D2;
const int DEF_CLOSED = D3;
const int DEF_DOOPEN = D4;
const int DEF_DOCLOSE = D5;
const int DEF_TIMEOUT = 20;
// **************************************************

// **************************************************
// Global variables
// --------------------------------------------------
DebouncedSwitch *openedSW;  // opened switch
DebouncedSwitch *closedSW;  // opened switch
DebouncedSwitch *doOpenSW;  // opened switch
DebouncedSwitch *doCloseSW;  // opened switch

std::unique_ptr<ESP8266WebServer> server;   // Set web server

// A UDP instance to let us send and receive packets over UDP
WiFiUDP udpClient;

Syslog syslog(udpClient, SYSLOG_PROTO_BSD); // syslog utility
bool syslogReady = false;                   // true indicates, syslog can be used

WiFiClient espClient;                       // WiFi client for mqtt connection
PubSubClient client(espClient);             // MQTT PubSubCLient

// Configuration that we'll store on disk:
struct Config {
  bool enable;             // master enable switch
  String hostname;         // hostname
  int port;                // http server port number
  bool syslog_enable;      // syslog enable
  String syslog_server;    // syslog server
  int syslog_port;         // syslog port
  bool mqtt_enable;        // enable mqtt?
  String mqtt_server;      // mqtt server
  IPAddress mqtt_ip;       // mqtt server IP address
  int mqtt_port;           // mqtt port
  String mqtt_user;        // mqtt username
  String mqtt_pwd;         // mqtt password
  String mqtt_state;       // <hostname>/state
  String mqtt_do;          // <hostname>/do OPEN/CLOSE/STOP
  String mqtt_lwt;         // <hostname>/LWT Online/Offline
  int timeout;             // timout in seconds for opening or closing the blind
  int pin_in1;             // IN1/2 pin for i298n hBridge
  int pin_in2;             // IN2/4 pin for i298n hBridge
  int pin_opened;          // OPENED pin 
  int pin_closed;          // CLOSED pin
  int pin_doopen;          // DO OPEN pin
  int pin_doclose;         // DO CLOSE pin
};
Config config;             // <- global configuration object

/**** uptime ***/
long Day=0;
int Hour =0;
int Minute=0;
int Second=0;
int HighMillis=0;
int Rollover=0;
/***************/

/**** state related ***/
int curState = STATE_UNKNOWN;
unsigned long timeoutStart = 0;            // the time the delay started
bool timeoutRunning = false;               // true if still waiting for delay to finish                                       
unsigned long lastConnectAttempt = 0;      // the last time we tried to connect to mqtt server
unsigned long lastWiFIConnectAttempt = 0;  // the last time we tried to connect to WiFi
unsigned long lastLWTSent = 0;             // the last time we sent the LWT message

//Booleans to allow control via webpage & mqtt:
bool do_stop = false;                   // stop motor running
bool do_open = false;                   // open blind
bool do_close = false;                  // close blind
/**********************/

/*************************
 * Log function to output
 * to Serial and syslog
 *************************/
void log(uint16_t pri, char *fmt, ...)
{
  // print to serial:
  va_list va;
  va_start(va, fmt);
  char buf[vsnprintf(NULL, 0, fmt, va) + 1];
  vsprintf(buf, fmt, va);
  buf[vsnprintf(NULL, 0, fmt, va) + 1] = '\0';
  Serial.println(buf);

  if (syslogReady && config.syslog_enable) 
  {
    syslog.log(pri, buf);
  }
  
  va_end(va);
} 

/**************************
 * Save config to file
 **************************/
void SaveConfig() 
{
  log(LOG_INFO, "Saving config...");
  DynamicJsonBuffer jsonBuffer;
  JsonObject& json = jsonBuffer.createObject();
  
  File configFile = SPIFFS.open("/config.json", "w");
  if (configFile) 
  {
    // Set the values
    json[ENABLE] = config.enable;
    json[HOSTNAME] = config.hostname;
    json[PORT] = config.port;
    json[SYSLOG_ENABLE] = config.syslog_enable;
    json[SYSLOG_SERVER] = config.syslog_server;
    json[SYSLOG_PORT] = config.syslog_port;
    json[MQTT_SERVER] = config.mqtt_server;
    json[MQTT_PORT] = config.mqtt_port;
    json[MQTT_USER] = config.mqtt_user;
    json[MQTT_PWD] = config.mqtt_pwd;
    json[MQTT_ENABLE] = config.mqtt_enable;
    json[TIMEOUT] = config.timeout;
    json[PIN_IN1] = config.pin_in1;
    json[PIN_IN2] = config.pin_in2;
    json[PIN_OPENED] = config.pin_opened;
    json[PIN_CLOSED] = config.pin_closed;
    json[PIN_DOOPEN] = config.pin_doopen;
    json[PIN_DOCLOSE] = config.pin_doclose;
  
    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
    log(LOG_INFO, "Saved config.");
  }
  else
  {
    log(LOG_ERR, "ERROR: Failed to open config file for writing.");
  }
  //end save
}


/**************************
 * Read config from file
 **************************/
void ReadConfig() 
{
  // set default config:
  config.enable = DEF_ENABLE;
  config.hostname = DEF_HOSTNAME;
  config.port = DEF_PORT;
  config.syslog_enable = DEF_SYSLOG_ENABLE;
  config.syslog_server = DEF_SYSLOG_SERVER;
  config.syslog_port = DEF_SYSLOG_PORT;
  config.mqtt_enable = DEF_MQTT_ENABLE;
  config.mqtt_server = DEF_MQTT_SERVER;
  config.mqtt_port = DEF_MQTT_PORT;
  config.mqtt_user = "";
  config.mqtt_pwd = "";
  config.timeout = DEF_TIMEOUT;
  config.pin_in1 = DEF_IN1;
  config.pin_in2 = DEF_IN2;
  config.pin_opened = DEF_OPENED;
  config.pin_closed = DEF_CLOSED;
  config.pin_doopen  = DEF_DOOPEN;
  config.pin_doclose = DEF_DOCLOSE;
  
  //read configuration from FS json
  log(LOG_INFO, "Mounting FS...");
  if (SPIFFS.begin()) 
  {
    // Uncomment next line to clean FS, for testing
    // SPIFFS.format();
    log(LOG_INFO, "Mounted file system.");
    if (SPIFFS.exists("/config.json")) 
    {
      //file exists, reading and loading
      log(LOG_INFO, "Reading config file...");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) 
      {
        log(LOG_INFO, "Opened config file.");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) 
        {
          log(LOG_INFO, "Parsed json.");
      
          // Copy values from the JsonObject to the Config

          config.enable = json[ENABLE] | DEF_ENABLE;
          
          if (json.containsKey(HOSTNAME) && strlen(json[HOSTNAME])>0) {
            config.hostname = json[HOSTNAME] | DEF_HOSTNAME;
          }
          config.port = json[PORT] | DEF_PORT;
          
          config.syslog_enable = json[SYSLOG_ENABLE] | DEF_SYSLOG_ENABLE;
          config.syslog_server = json[SYSLOG_SERVER] | DEF_SYSLOG_SERVER;
          config.syslog_port = json[SYSLOG_PORT] | DEF_SYSLOG_PORT;
          
          config.mqtt_enable = json[MQTT_ENABLE] | DEF_MQTT_ENABLE;
          config.mqtt_server = json[MQTT_SERVER] | DEF_MQTT_SERVER;
          config.mqtt_port = json[MQTT_PORT] | DEF_MQTT_PORT;
          if (json.containsKey(MQTT_USER) && strlen(json[MQTT_USER])>0) {
            config.mqtt_user = json[MQTT_USER] | "";
          }
          if (json.containsKey(MQTT_PWD) && strlen(json[MQTT_PWD])>0) {
            config.mqtt_pwd = json[MQTT_PWD] | "";
          }
          config.timeout = json[TIMEOUT] | DEF_TIMEOUT;
          config.pin_in1 = json[PIN_IN1] | DEF_IN1;
          config.pin_in2 = json[PIN_IN2] | DEF_IN2;
          config.pin_opened = json[PIN_OPENED] | DEF_OPENED;
          config.pin_closed = json[PIN_CLOSED] | DEF_CLOSED;
          config.pin_doopen = json[PIN_DOOPEN] | DEF_DOOPEN;
          config.pin_doclose = json[PIN_DOCLOSE] | DEF_DOCLOSE;
        } else {
          log(LOG_ERR, "Failed to load json config.");
        }
      }
    }
  } else {
    log(LOG_ERR, "ERROR: Failed to mount FS.");
    log(LOG_INFO, "INFO: formatting SPIFFS filesystem...");
    //clean FS, for testing
    SPIFFS.format();
  }

  if (config.syslog_server.length() == 0 || config.syslog_port < 1 )
  {
    config.syslog_enable = false; 
  }
  config.mqtt_state = config.hostname;
    config.mqtt_state += MQTT_STATE;
  config.mqtt_do = config.hostname;
    config.mqtt_do += MQTT_DO;
  config.mqtt_lwt = config.hostname;
    config.mqtt_lwt += MQTT_LWT;    
  //end read
}

/**********************
 * Setup 
 **********************/
void setup() 
{
  Serial.begin(115200);

  ReadConfig();                                         // read config from json file
  NetworkInit();                                        // Initialize WiFi
  SysLogInit();                                         // setup syslog
  WebServerInit();                                      // start webserver
  
  //----------------------------------------------
  // Initialize the output variables as outputs
  pinMode(config.pin_in1, OUTPUT);
  pinMode(config.pin_in2, OUTPUT);

  pinMode(config.pin_opened, INPUT);
  pinMode(config.pin_closed, INPUT);
  pinMode(config.pin_doopen, INPUT);
  pinMode(config.pin_doclose, INPUT);

  openedSW  = new DebouncedSwitch(config.pin_opened);   // opened switch
  closedSW  = new DebouncedSwitch(config.pin_closed);   // closed switch
  doOpenSW  = new DebouncedSwitch(config.pin_doopen);   // doOpen switch
  doCloseSW = new DebouncedSwitch(config.pin_doclose);  // doClose switch
  
  // Set outputs to LOW
  digitalWrite(config.pin_in1, LOW);;
  digitalWrite(config.pin_in2, LOW);;
  //----------------------------------------------
  
  PubSubInit();                                         // Initialize PubSubClient
}

/****************************
 * Initialize PubSubClient
 ****************************/
void PubSubInit() 
{
  if (config.mqtt_enable) 
  {
    log(LOG_INFO, "Enabling mqtt...");
    IPAddress ip;
      
    char charBuf[config.mqtt_server.length() + 1];
    config.mqtt_server.toCharArray(charBuf, config.mqtt_server.length()+1);
    charBuf[config.mqtt_server.length() + 1] = '\0';
    
    // try to do dns resolution:
    WiFi.hostByName(charBuf, ip);
    
    log(LOG_INFO, " server:port: %u.%u.%u.%u:%d", ip[0],ip[1],ip[2],ip[3], config.mqtt_port);
    
    client.setServer(ip, config.mqtt_port);
    client.setCallback(callback);
  }
}


/****************************
 * Connect PubSubClient
 ****************************/
void PubSubConnect() 
{
  if (config.mqtt_enable && !client.connected() && (millis() - lastConnectAttempt) > 5000 ) 
  {
    log(LOG_INFO, "Connecting mqtt...");
    
    char cUser[config.mqtt_user.length() + 1];
    char cPwd[config.mqtt_pwd.length() + 1];
    char cHost[config.hostname.length() + 1];
    char cLWT[config.mqtt_lwt.length() + 1];

    char cDo[config.mqtt_do.length() + 1];
    char cState[config.mqtt_state.length() + 1];

    config.hostname.toCharArray(cHost, config.hostname.length() + 1);
    config.mqtt_do.toCharArray(cDo, config.mqtt_do.length() + 1);
    config.mqtt_state.toCharArray(cState, config.mqtt_state.length() + 1);
    config.mqtt_lwt.toCharArray(cLWT, config.mqtt_lwt.length() + 1);
    
    if (config.mqtt_user.length() > 0)
    {
      config.mqtt_user.toCharArray(cUser, config.mqtt_user.length() + 1);
    }
    if (config.mqtt_pwd.length() > 0)
    {
      config.mqtt_pwd.toCharArray(cPwd, config.mqtt_pwd.length() + 1);
    }
    if (config.mqtt_user.length() > 0 || config.mqtt_pwd.length() > 0)
    {
      log(LOG_INFO, "Connecting with user/pwd...");
      //boolean connect (clientID, username, password, willTopic, willQoS, willRetain, willMessage)
      if (client.connect(cHost, cUser, cPwd, cLWT, 1, true, MQTT_LWT_OFFLINE)) {
        log(LOG_INFO, "Connected");  
        
        client.subscribe(cDo);
        client.publish(cLWT, MQTT_LWT_ONLINE, true);
        lastLWTSent = millis();
        client.publish(cState, getStateName(), false);
      } else {
        log(LOG_WARNING, "Failed, rc=%d; try again in 5 seconds", client.state());
      }
    } 
    else 
    {
      log(LOG_INFO, "Connecting without user/pwd...");  
      if (client.connect(cHost, cLWT, 1, 1, MQTT_LWT_OFFLINE)) {
        log(LOG_INFO, "Connected");  
        
        client.subscribe(cDo);
        client.publish(cLWT, MQTT_LWT_ONLINE, true);
        client.publish(cState, getStateName(), false);
      } else {
        log(LOG_WARNING, "Failed, rc=%d; try again in 5 seconds", client.state());
      }
    }
    
    lastConnectAttempt = millis();
  }
}

/****************************
 * Return name of curState
 ****************************/
const char * getStateName() 
{
  return STATE_NAMES[curState];
  /*switch (curState)
  {
    case STATE_OPEN:
      return NAME_OPEN;
    case STATE_CLOSED:
      return NAME_CLOSED;
    case STATE_OPENING:
      return NAME_OPENING;
    case STATE_CLOSING:
      return NAME_CLOSING;
    default:
      return NAME_UNKNOWN;
  }*/
}

/****************************
 * Loop for PubSubClient
 ****************************/
void PubSubLoop() 
{
  if (config.mqtt_enable && client.connected()) 
  {
    if ( (millis() - lastLWTSent) > 300000)
    {
      // resend the LWT message
      char cLWT[config.mqtt_lwt.length() + 1];
      config.mqtt_lwt.toCharArray(cLWT, config.mqtt_lwt.length() + 1);

      log(LOG_INFO, "Resending LWT...");
      client.publish(cLWT, MQTT_LWT_ONLINE, true);
      lastLWTSent = millis();
    }
    client.loop();
  }
}

/****************************
 * Initialize SysLog
 ****************************/
void SysLogInit()
{
  // initialize syslog instance:
  log(LOG_INFO, "Creating syslog...");

  if (config.syslog_enable && config.syslog_server.length()>0 && config.syslog_port >0)
  {
    IPAddress addr;
    
    char cServer[config.syslog_server.length() + 1];
    config.mqtt_server.toCharArray(cServer, config.syslog_server.length()+1);
    cServer[config.syslog_server.length() + 1] = '\0';

    char cHost[config.hostname.length() + 1];
    config.hostname.toCharArray(cHost, config.hostname.length()+1);
    cHost[config.hostname.length() + 1] = '\0';
    
    // try to do dns resolution:
    WiFi.hostByName(cServer, addr);
    
    syslog.server(addr, config.syslog_port);
    syslog.deviceHostname(cHost);
    syslog.appName("blind");
    syslog.defaultPriority(LOG_KERN);

    syslogReady = true;
  }
  else
  {
    config.syslog_enable = false;
  }
}

/****************************
 * Initialize the webserver
 ****************************/
void WebServerInit() 
{
  log(LOG_INFO, "Starting webserver on port: %d", config.port);
  
  server.reset(new ESP8266WebServer(WiFi.localIP(), config.port));
  server->onNotFound(HandleNotFound);
  server->on("/", HandleRoot);            // get root page
  server->on("/config", HandleConfig);    // get config page
  server->on("/do_open", HandleDoOpen);   // open blind
  server->on("/do_close", HandleDoClose); // close (or stop)
  server->on("/do_stop", HandleDoStop);   // stop
  server->on("/info", HandleInfo);        // get info page
  server->on("/state", HandleState);      // get state page
  server->on("/states", HandleStates);    // get state json
  server->on("/reset", HandleReset);      // reset wifi
  server->on("/restart", HandleRestart);  // restart device
  server->on("/submit", HandleSubmit);    // save config

  server->begin();
  log(LOG_INFO, "HTTP server started.");
}

/***************************
 * Initiallizing the WiFi  *
 ***************************/
void NetworkInit() 
{
  // WiFiManager
  // Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;
  // Uncomment and run it once, if you want to erase all the stored information
  // wifiManager.resetSettings();
  
  WiFi.persistent(true);
  WiFi.setAutoConnect(true);
  WiFi.setAutoReconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.hostname(config.hostname);
  
  // sets timeout until configuration portal gets turned off
  // useful to make it all retry or go to sleep
  // in seconds
  wifiManager.setTimeout(240);
  //wifiManager.setAPCallback(NetworkConfigMode);

  //set minimum quality of signal so it ignores AP's under that quality
  //defaults to 8%
  //wifiManager.setMinimumSignalQuality();
  
  // fetches ssid and pass and tries to connect
  // if it does not connect it starts an access point with the specified name
  // here  "AutoConnectAP"
  // and goes into a blocking loop awaiting configuration
  String apName = config.hostname;
    apName += "AP";
  char cApName[apName.length() +1];
  apName.toCharArray(cApName, apName.length()+1);
    
  wifiManager.autoConnect(cApName);

  // if you get here you have connected to the WiFi
  IPAddress ip = WiFi.localIP();
  log(LOG_INFO, "Connected to WiFi; IP: %u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
}

/****************************
 *  Create the default html
 *  header of the page.
 ****************************/
String HtmlHead(String pagename, String script, String onload) 
{
  String message = "<!DOCTYPE html><html lang=\"en\" class=\"\">";
  message += "<head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1,user-scalable=no'/>";
  message += "<title>Blind - ";
    message += pagename;
    message += "</title>\n" ;
  message += "<style>div,fieldset,input,select{padding:5px;font-size:1em;}input{width:100%;box-sizing:border-box;-webkit-box-sizing:border-box;-moz-box-sizing:border-box;}select{width:100%;}textarea{resize:none;width:98%;height:318px;padding:5px;overflow:auto;}body{text-align:center;font-family:verdana;}td{padding:0px;}button{border:0;border-radius:0.3rem;background-color:#1fa3ec;color:#fff;line-height:2.4rem;font-size:1.2rem;width:100%;-webkit-transition-duration:0.4s;transition-duration:0.4s;cursor:pointer;}button:hover{background-color:#0e70a4;}.bwht{background-color:#aaaaaa;}.bwht:hover{background-color:#999999;}.bred{background-color:#d43535;}.bred:hover{background-color:#931f1f;}.bgrn{background-color:#47c266;}.bgrn:hover{background-color:#5aaf6f;}a{text-decoration:none;}.p{float:left;text-align:left;}.q{float:right;text-align:right;}</style>";

  if (script.length()>1) 
  {
    message += "<script type=\"text/javascript\">";
    message += script;
    message += "</script>";
  }
  if (onload.length()>1){
    message += "</head><body onload=\"";
    message += onload;
    message += "\">\n";
  } 
  else
  {
    message += "</head><body>\n";  
  }
  
  message += "<div style='text-align:left;display:inline-block;min-width:340px;'>";
  message += "<div style='text-align:center;'><h2>";
    message += pagename;
    message += "</h2></div>\n";
  return message;
}

/****************************
 * Create the default html
 * header of the page.
 ****************************/
String HtmlHead(String pagename) 
{
  return HtmlHead(pagename, "", "");
}

/****************************
 * Create the default html
 * footer of the page.
 ****************************/
String HtmlFoot() 
{
  String message = "<br/><div style='text-align:right;font-size:11px;'><hr/>\n";
  message += "Blind ";
    message += VERSION;
    message += "&nbsp;";
    message += COPYRIGHT;
    message += "</div></div></body></html>\n";
  return message;
}

/****************************
 * Handle restart request
 ****************************/
void HandleRestart() 
{
  RestartDevice();
}


/****************************
 * Handle DoOpen request
 ****************************/
void HandleDoOpen() 
{
  log(LOG_INFO, "HandleDoOpen");
  do_open = true;
  do_close= false;
  do_stop = false;
  
  server->send ( 200, "text/plain", "");
}

/****************************
 * Handle DoStop request
 ****************************/
void HandleDoStop() 
{
  log(LOG_INFO, "HandleDoStop");
  do_open = false;
  do_close= false;
  do_stop = true;

  server->send ( 200, "text/plain", "");
}

/****************************
 * Handle DoClose request
 ****************************/
void HandleDoClose() 
{
  log(LOG_INFO, "HandleDoClose");
  do_open = false;
  do_close= true;
  do_stop = false;
  
  server->send ( 200, "text/plain", "");
}

/****************************
 * Handle root-page request
 ****************************/
void HandleRoot() {
  log(LOG_INFO, "HandleRoot...");
  
  String script = getStateScript();
  script += "\nfunction render(data) {\n";
  script += "  var d = document.getElementById(\"curstate\");";
  script += "  if (d != null) {\n";
  script += "    d.innerHTML = data.state.name;\n";
  script += "  }\n";
  script += "}\n";
  
  String message = HtmlHead("Main", script, "getHttpState();");
  message += "<div id=\"curstate\" style=\"text-align:center;font-weight:bold;font-size:48px\"></div>";
  message += "<button class='button bgrn' onclick='doOpen();' >Open</button><br><br>\n";
  message += "<button class='button bgrn' onclick='doClose();' >Close</button><br><br>\n";
  message += "<button class='button bgrn' onclick='doStop();' >Stop</button><br><br>\n";
  message += "<form action='/info' method='get'><button class='button'>Information</button></form><br>\n";
  message += "<form action='/state' method='get'><button class='button'>Status</button></form><br>\n";
  message += "<form action='/config' method='get'><button class='button'>Configuration</button></form><br>\n";
  message += "<form action='/restart' method='get'><button class='button bred'>Restart</button></form><br>\n";
  
  message += HtmlFoot();
  server->send(200, "text/html", message);
}

/****************************
 * Get HLML code for PIN
 * option (one option of select)
 ****************************/
String htmlPinOption(int id, String caption, bool selected)
{
  String message = "<option value=\"";
    message += id;
    message += "\"";
    if (selected) {
      message += "selected";
    }
    message += ">";
    message += caption;
    message += "</option>\n";

    return message;
}


/************************************************
 * Return html SELECT element with all d1-mini
 * GPIO ports.
 * Parameters: 
 *   caption: name of the html element
 *   selected: id (gpio number) to preselect)
 ************************************************/
String htmlPinSelect(String caption, int selected)
{
  String message = "<SELECT name=\"";
      message += caption;
      message += "\" >\n";
    message += htmlPinOption(16, "D0 (GPIO16)", selected == 16);
    message += htmlPinOption(5, "D1 (GPIO5)", selected == 5);
    message += htmlPinOption(4, "D2 (GPIO4)", selected == 4);
    message += htmlPinOption(0, "D3 (GPIO0)", selected == 0);
    message += htmlPinOption(2, "D4 (GPIO2)", selected == 2);
    message += htmlPinOption(14, "D5 (GPIO14)", selected == 14);
    message += htmlPinOption(12, "D6 (GPIO12)", selected == 12);
    message += htmlPinOption(13, "D7 (GPIO13)", selected == 13);
    message += htmlPinOption(15, "D8 (GPIO15)", selected == 15);
    message += htmlPinOption(3, "RX (GPIO3)", selected == 3);
    message += htmlPinOption(1, "TX (GPIO1)", selected == 1);
    message += "</SELECT>\n";

    return message;
}

/***********************************
 * Process the save config request
 ***********************************/
void HandleSubmit()
{
  log(LOG_INFO, "HandleSubmit...");
  
  bool save = false;
  if (server->args() > 0 ) 
  {
    // disable; will enable if parameter is set....
    config.syslog_enable = false;
    config.mqtt_enable = false;
    config.enable = false;
      
    for ( uint8_t i = 0; i < server->args(); i++ ) 
    {
      //log(LOG_INFO, server->argName(i));
      char cArgI[server->arg(i).length() + 1];
      server->arg(i).toCharArray(cArgI, server->arg(i).length()+1);
      cArgI[server->arg(i).length() + 1] = '\0';

      if (server->argName(i) == ENABLE) 
      {
        log(LOG_INFO, "Setting enable: %s", cArgI);
        config.enable = (server->arg(i) == "1");
        save = true;
      }
      else if (server->argName(i) == HOSTNAME) 
      {
        // do something here with value from server.arg(i);
        log(LOG_INFO, "Setting hostname: %s", cArgI);
        config.hostname = server->arg(i);
        save = true;
      }
      else if (server->argName(i) == PORT) 
      {
        // do something here with value from server.arg(i);
        log(LOG_INFO, "Setting port: %d", server->arg(i).toInt());
        config.port = server->arg(i).toInt();
        save = true;
      }
      else if (server->argName(i) == SYSLOG_ENABLE) 
      {
        log(LOG_INFO, "Setting syslog enable: %s", cArgI);
        config.syslog_enable = (server->arg(i) == "1");
        save = true;
      }
      else if (server->argName(i) == SYSLOG_SERVER) 
      {
        log(LOG_INFO, "Setting syslog server: %s", cArgI);
        config.syslog_server = server->arg(i);
        save = true;
      }
      else if (server->argName(i) == SYSLOG_PORT) 
      {
        log(LOG_INFO, "Setting syslog port: %d", server->arg(i).toInt());
        config.syslog_port = server->arg(i).toInt();
        save = true;
      }
      else if (server->argName(i) == MQTT_ENABLE) 
      {
        log(LOG_INFO, "Setting mqtt enable: %s", cArgI);
        config.mqtt_enable = (server->arg(i) == "1");
        save = true;
      }
      else if (server->argName(i) == MQTT_SERVER) 
      {
        log(LOG_INFO, "Setting mqtt server: %s", cArgI);
        Serial.println();
        config.mqtt_server = server->arg(i);
        save = true;
      }
      else if (server->argName(i) == MQTT_PORT) 
      {
        log(LOG_INFO, "Setting mqtt port: %d", server->arg(i).toInt());
        config.mqtt_port = server->arg(i).toInt();
        save = true;
      }
      else if (server->argName(i) == MQTT_USER) 
      {
        log(LOG_INFO, "Setting mqtt user: %s", cArgI);
        config.mqtt_user = server->arg(i);
        save = true;
      }
      else if (server->argName(i) == MQTT_PWD) 
      {
        log(LOG_INFO, "Setting mqtt pwd: %s", cArgI);
        config.mqtt_pwd = server->arg(i);
        save = true;
      }
      else if (server->argName(i) == PIN_IN1) 
      {
        log(LOG_INFO, "Setting in1: %d", server->arg(i).toInt());
        config.pin_in1 = server->arg(i).toInt();
        save = true;
      }
      else if (server->argName(i) == PIN_IN2) 
      {
        log(LOG_INFO, "Setting in2: %d", server->arg(i).toInt());
        config.pin_in2 = server->arg(i).toInt();
        save = true;
      }
      else if (server->argName(i) == PIN_OPENED) 
      {
        log(LOG_INFO, "Setting opened: %d", server->arg(i).toInt());
        config.pin_opened = server->arg(i).toInt();
        save = true;
      }
      else if (server->argName(i) == PIN_CLOSED) 
      {
        log(LOG_INFO, "Setting closed: %d", server->arg(i).toInt());
        config.pin_closed = server->arg(i).toInt();
        save = true;
      }
      else if (server->argName(i) == PIN_DOOPEN) 
      {
        log(LOG_INFO, "Setting doopen: %d", server->arg(i).toInt());
        config.pin_doopen = server->arg(i).toInt();
        save = true;
      }
      else if (server->argName(i) == PIN_DOCLOSE) 
      {
        log(LOG_INFO, "Setting doclose: %d", server->arg(i).toInt());
        config.pin_doclose = server->arg(i).toInt();
        save = true;
      }
    }
    if (save) 
    {
      SaveConfig();
    }
  }
  
  if (save) {
    RestartDevice();
  }
}

/***********************************
 * Restart the device
 * and show restart page to web client
 ***********************************/
void RestartDevice() {
  log(LOG_INFO, "Restarting device...");
  
  // redirect to the root
  String message = HtmlHead("Restarting...");
  message += "<p>Restarting device to apply new settings. This page will refresh in 25 seconds. (If you changed the hostname or port, you might need to manually change the url.)</p>";
  message += "<p>If the page does not refresh automatically, click <a href=\"/\">here</a>.</p>";
  message += HtmlFoot();
  
  server->sendHeader("Refresh","25; url=/", true);
  server->send(200, "text/html", message); 

  delay(2000);
  WiFi.forceSleepBegin(); 
  //wdt_reset(); 
  ESP.restart(); 
  while(1) wdt_reset();
}

/***********************************
 * Produce a 404 page
 ***********************************/
void HandleNotFound() {
  log(LOG_INFO, "HandleNotFound...");
  
  String message = HtmlHead("File Not Found");
  message += "<ul><li>URL: ";
    message += server->uri();
  message += "\n<li>Method: ";
    message += (server->method() == HTTP_GET) ? "GET" : "POST";
  message += "\n<li>Arguments: ";
    message += server->args();
    message += "<ul>\n";
      //  list arguments
      for (uint8_t i = 0; i < server->args(); i++) {
        message += "<li>" + server->argName(i) + ": " + server->arg(i) + "\n";
      }
    message += "</ul>\n";
  message += "</ul>\n";
  message += "<br><p>Click <a href=\"/\">here</a> to return.</p>\n";
  
  message += HtmlFoot();
  server->send(404, "text/html", message);
}

/***********************************
 * Handle a reset to reset
 * the WiFi settings.
 ***********************************/
void HandleReset() {
  log(LOG_INFO, "HandleReset...");

  String message = HtmlHead("Resetting WiFi...");
  message += "<p>Resetting WiFi settings; Please connect your device to BlindAP directly to reconfigure the WiFI settings.</p>";
  message += HtmlFoot();

  server->sendHeader("Refresh","25; url=/", true);
  server->send(200, "text/html", message);
  
  delay(5000);                    // delay to allow the page to be sent and shown in the browser
  WiFiManager wifiManager;        // reset (forget) WiFi settings
  wifiManager.resetSettings();
  delay(2000);
  ESP.restart();                  // restart
  while(1) wdt_reset();
}

/***********************************
 * Handle /config url
 ***********************************/
void HandleConfig() {
  log(LOG_INFO, "HandleConfig...");

  String pagename = "Configuration";
  String message = HtmlHead(pagename);

  
  message += "<form method='post' action='/submit'>\n";
  
  message += "<fieldset><legend><b>&nbsp;General&nbsp;</b></legend><table>\n";
    message += "<tr><td style='width:190px'> <b>Hostname</b></td><td style='width:160px'><INPUT type=\"text\" name=\"hostname\" value=\"";
      message += config.hostname;
      message += "\" maxlength=\"64\"></td></tr>\n";
    message += "<tr><td style='width:190px'> <b>Port</b> </td><td style='width:160px'><INPUT type=\"number\" name=\"port\" value=\"";
      message += config.port;
      message += "\" maxlength=\"6\"></td></tr>\n";
  message += "</table></fieldset>";

  message += "<fieldset><legend><b>&nbsp;MQTT&nbsp;</b></legend><table>\n";
    message += "<tr><td style='width:190px'> <b>Enable?</b> </td><td style='width:160px'><INPUT  style=\"margin:0;width:13px;height:13px;overflow:hidden;\" type=\"checkbox\" name=\"mqtt_enable\" value=\"1\" ";
      if (config.mqtt_enable) 
      {
        message += " checked=\"checked\" ";
      }
      message += "><label>&nbsp;</label></td></tr>\n";
    message += "<tr><td style='width:190px'> <b>Server</b> </td><td style='width:160px'><INPUT type=\"text\" name=\"mqtt_server\" value=\"";
      message += config.mqtt_server;
      message += "\" maxlength=\"64\"></td></tr>\n";
    message += "<tr><td style='width:190px'> <b>Port</b> </td><td style='width:160px'><INPUT type=\"number\" name=\"mqtt_port\" value=\"";
      message += config.mqtt_port;
      message += "\" maxlength=\"6\"></td></tr>\n";
    message += "<tr><td style='width:190px'> <b>Username</b> </td><td style='width:160px'><INPUT type=\"text\" name=\"mqtt_user\" value=\"";
      message += config.mqtt_user;
      message += "\" maxlength=\"64\"></td></tr>\n";
    message += "<tr><td style='width:190px'> <b>Password</b> </td><td style='width:160px'><INPUT type=\"text\" name=\"mqtt_pwd\" value=\"";
      message += config.mqtt_pwd;
      message += "\" maxlength=\"64\"></td></tr>\n";
    message += "<tr><td style='width:190px'> <b>State-topic</b> </td><td style='width:160px'>";
      message += config.mqtt_state;
      message += "</td></tr>\n";
    message += "<tr><td style='width:190px'> <b>Do-topic</b> </td><td style='width:160px'>";
      message += config.mqtt_do;
      message += "</td></tr>\n";
    message += "<tr><td style='width:190px'> <b>LWT-topic</b> </td><td style='width:160px'>";
      message += config.mqtt_lwt;
      message += "</td></tr>\n";
  message += "</table></fieldset>";

  message += "<fieldset><legend><b>&nbsp;Syslog&nbsp;</b></legend><table>\n";
    message += "<tr><td style='width:190px'> <b>Enable?</b> </td><td style='width:160px'><INPUT  style=\"margin:0;width:13px;height:13px;overflow:hidden;\" type=\"checkbox\" name=\"syslog_enable\" value=\"1\" ";
      if (config.syslog_enable) 
      {
        message += " checked=\"checked\" ";
      }
      message += "><label>&nbsp;</label></td></tr>\n";
    message += "<tr><td style='width:190px'> <b>Server</b> </td><td style='width:160px'><INPUT type=\"text\" name=\"syslog_server\" value=\"";
      message += config.syslog_server;
      message += "\" maxlength=\"64\"></td></tr>\n";
    message += "<tr><td style='width:190px'> <b>Port</b> </td><td style='width:160px'><INPUT type=\"number\" name=\"syslog_port\" value=\"";
      message += config.syslog_port;
      message += "\" maxlength=\"6\"></td></tr>\n";
  message += "</table></fieldset>";

  
  message += "<fieldset><legend><b>&nbsp;Pins&nbsp;</b></legend><table>\n";
    message += "<tr><td style='width:190px'> <b>Enable?</b> </td><td style='width:160px'><INPUT  style=\"margin:0;width:13px;height:13px;overflow:hidden;\" type=\"checkbox\" name=\"enable\" value=\"1\" ";
      if (config.enable) 
      {
        message += " checked=\"checked\" ";
      }
      message += "><label>&nbsp;</label></td></tr>\n";
    message += "<tr><td style='width:190px'> <b>IN1/IN3</b> </td><td style='width:160px'>";
      message += htmlPinSelect(PIN_IN1, config.pin_in1);
      message += "</td></tr>\n";
    message += "<tr><td style='width:190px'> <b>IN2/IN4</b> </td><td style='width:160px'>";
      message += htmlPinSelect(PIN_IN2, config.pin_in2);
      message += "</td></tr>\n";
    message += "<tr><td style='width:190px'> <b>Opened</b> </td><td style='width:160px'>";
      message += htmlPinSelect(PIN_OPENED, config.pin_opened);
      message += "</td></tr>\n";
    message += "<tr><td style='width:190px'> <b>Closed</b> </td><td style='width:160px'>";
      message += htmlPinSelect(PIN_CLOSED, config.pin_closed);
      message += "</td></tr>\n";
    message += "<tr><td style='width:190px'> <b>DoOpen</b> </td><td style='width:160px'>";
      message += htmlPinSelect(PIN_DOOPEN, config.pin_doopen);
      message += "</td></tr>\n";
    message += "<tr><td style='width:190px'> <b>DoClose</b> </td><td style='width:160px'>";
      message += htmlPinSelect(PIN_DOCLOSE, config.pin_doclose);
      message += "</td></tr>\n";
    
  message += "</table></fieldset>";
  
  
  message += "<br/><button name='save' type='submit' class='button bgrn'>Save</button></form><br/><br/>\n";
  message += "<form action='/reset' method='get'><button class='button bred' onclick=\"return confirm('This will erase the current WiFi config and start the device in AP Mode.');\">WiFiReset</button></form></br>\n";
  message += "<form action='/' method='get'><button class='button'>Main</button></form>\n";

  message += HtmlFoot();
  server->send(200, "text/html", message);
}

/***********************************
 * Handle /info url
 ***********************************/
void HandleInfo() {
  log(LOG_INFO, "HandleInfo...");

  String pagename = "Information";
  String msg= HtmlHead(pagename);

  
  msg+= "<table border=\"0\" width=\"100%\">\n";
  msg+= "<tr><th>Version</th><td>";
    msg += VERSION;
    msg += "</td></tr>";
  msg+= "<tr><th>Core/SDK Version</th><td>";
    msg += ESP.getCoreVersion();
    msg += "/";
    msg += ESP.getSdkVersion();
    msg += "</td></tr>";
  msg+= "<tr><th>Uptime</th><td>";
    msg += uptime2String();
    msg += "</td></tr>";
  msg += "<tr><th>&nbsp;</th><td>&nbsp;</td></tr>";
  
  msg+= "<tr><th>SSID</th><td>";
    msg += WiFi.SSID(); 
    msg += " (";
    msg +=  (-1*WiFi.RSSI());
    msg += "%)</td></tr>";
  msg+= "<tr><th>BSSID</th><td>";
    msg += WiFi.BSSIDstr();
    msg += "</td></tr>";
  msg+= "<tr><th>Hostname</th><td>";
    msg += config.hostname;
    msg += " (";
    msg += WiFi.hostname();
    msg += ")</td></tr>";
  msg+= "<tr><th>IP</th><td>";
    msg += WiFi.localIP().toString(); 
    msg += "</td></tr>";
  msg+= "<tr><th>Subnet</th><td>";
    msg += WiFi.subnetMask().toString(); 
    msg += "</td></tr>";
  msg+= "<tr><th>Gateway</th><td>";
    msg += WiFi.gatewayIP().toString(); 
    msg += "</td></tr>";
  msg+= "<tr><th>DNS Server</th><td>";
    msg += WiFi.dnsIP(0).toString(); 
    msg += "</td></tr>";
  byte mac[6];                     // the MAC address of your Wifi shield
  WiFi.macAddress(mac);
  msg+= "<tr><th>MAC address</th><td>";
    msg += mac2String(mac);
    msg += "</td></tr>";
  msg += "<tr><th>&nbsp;</th><td>&nbsp;</td></tr>";

  msg+= "<tr><th>ESP Chip Id</th><td>";
      msg += ESP.getChipId(); 
    msg += "</td></tr>";
  msg+= "<tr><th>Flash Chip Id</th><td>";
      msg += ESP.getFlashChipId(); 
    msg += "</td></tr>";
  msg+= "<tr><th>Flash Size</th><td>";
      msg += ESP.getFlashChipRealSize();
    msg += "</td></tr>";
  msg+= "<tr><th>Program Flash Size</th><td>";
      msg += ESP.getFlashChipSize();
    msg += "</td></tr>";
  msg+= "<tr><th>Program Size</th><td>";
      msg += ESP.getSketchSize();
    msg += "</td></tr>";
  msg+= "<tr><th>Free Program Size</th><td>";
      msg += ESP.getFreeSketchSpace();
    msg += "</td></tr>";
  msg+= "<tr><th>Free Heap</th><td>";
      msg += ESP.getFreeHeap();
    msg += "</td></tr>";
    
  msg += "</table>\n";
  
  msg += "<br><form action='/' method='get'><button class='button'>Main</button></form>\n";

  msg += HtmlFoot();
  server->send(200, "text/html", msg);
}

/***********************************
 * Return the state script.
 * Rest of script must contain a 
 * - render(data) furnction
 * - use onload='getHttpState();'
 ***********************************/
String getStateScript() 
{
  String script = "\nfunction getHttpState() {\n";
  script += "  var xmlHttp = new XMLHttpRequest();\n";
  script += "  xmlHttp.onreadystatechange = function() {\n";
  script += "    if (xmlHttp.readyState == 4) {\n";
  script += "      if (xmlHttp.status == 200) {\n";
  script += "        var data = JSON.parse(this.responseText);\n";
  script += "        render(data);\n";
  script += "      }\n";
  script += "      setTimeout(getHttpState, 5000);\n";
  script += "    }\n";
  script += "  }\n"; 
  script += "  xmlHttp.open(\"GET\", window.location.protocol + \"//\" + window.location.host + \"/states\", true); /* true for asynchronous */ \n";
  script += "  xmlHttp.send(null);\n";
  script += "}\n";
  script += "\nfunction doClose() {\n";
  script += "  var xmlHttp = new XMLHttpRequest();\n";
  script += "  xmlHttp.open(\"GET\", window.location.protocol + \"//\" + window.location.host + \"/do_close\", true); /* true for asynchronous */ \n";
  script += "  xmlHttp.send(null);\n";
  script +="}\n";  
  script += "\nfunction doOpen() {\n";
  script += "  var xmlHttp = new XMLHttpRequest();\n";
  script += "  xmlHttp.open(\"GET\", window.location.protocol + \"//\" + window.location.host + \"/do_open\", true); /* true for asynchronous */ \n";
  script += "  xmlHttp.send(null);\n";
  script +="}\n";  
  script += "\nfunction doStop() {\n";
  script += "  var xmlHttp = new XMLHttpRequest();\n";
  script += "  xmlHttp.open(\"GET\", window.location.protocol + \"//\" + window.location.host + \"/do_stop\", true); /* true for asynchronous */ \n";
  script += "  xmlHttp.send(null);\n";
  script +="}\n";  
    
  return script;
}

/***********************************
 * Handle State url
 ***********************************/
void HandleState() {
  log(LOG_INFO, "HandleState...");

  String pagename = "Current State";  
  String script = getStateScript();
  script += "\nfunction render(data) {\n";
  script += "  document.getElementById(\"curstate\").innerHTML = data.state.name ;\n";
  
  script += "  document.getElementById(\"IN1\").innerHTML = GetPinButton('IN1', data.state.in1, 1); \n";
  script += "  document.getElementById(\"IN2\").innerHTML = GetPinButton('IN2', data.state.in2, 1); \n";
  script += "  document.getElementById(\"OPENED\").innerHTML = GetPinButton('Opened', data.state.opened, 0); \n";
  script += "  document.getElementById(\"CLOSED\").innerHTML = GetPinButton('Closed', data.state.closed, 0); \n";
  script += "  document.getElementById(\"DOOPEN\").innerHTML = GetPinButton('Open', data.state.do_open, 0); \n";
  script += "  document.getElementById(\"DOCLOSE\").innerHTML = GetPinButton('Close', data.state.do_close, 0); \n";
  script += "}\n";
  script += "\nfunction GetPinButton(text, value, trueValue) {\n";
  script += "  if (value == trueValue) {\n";
  script += "    return \"<button class='button bgrn'>\" + text + \"</button>\";\n";
  script += "  } else {\n";
  script += "    return \"<button class='button bwht'>\" + text + \"</button>\";\n";
  script += "  }\n";
  script += "}\n";

  String message = HtmlHead(pagename, script, "getHttpState();");
  message += "<div id=\"curstate\" style=\"text-align:center;font-weight:bold;font-size:48px\"></div>";
  message += "<table border=0 width=\"100%\">\n";
  message += "<tr><td id=\"IN1\"></td><td id=\"IN2\"></td></tr>\n";
  message += "<tr><td id=\"OPENED\"></td><td id=\"CLOSED\"></td></tr>\n";
  message += "<tr><td id=\"DOOPEN\"></td><td id=\"DOCLOSE\"></td></tr>\n";
  message += "</table><br>\n";
  message += "<button class='button bgrn' onclick='doOpen();' >Open</button><br><br>\n";
  message += "<button class='button bgrn' onclick='doClose();' >Close</button><br><br>\n";
  message += "<button class='button bgrn' onclick='doStop();'>Stop</button><br><br>\n";
  message += "<form action='/' method='get'><button class='button'>Main</button></form>\n";

  message += HtmlFoot();
  server->send(200, "text/html", message);
}

/***********************************
 * Handle states json url
 ***********************************/
void HandleStates() {
  log(LOG_INFO, "HandleStates...");
  
  String msg = "{ \"config\" : {  \"enable\" : \"";
  msg += config.enable;
  msg += "\", \"hostname\": \"";
  msg += config.hostname;
  msg += "\", \"port\": ";
  msg += config.port;
  msg += ", \"mqtt\": { \"enable\" : ";
  msg += config.mqtt_enable;
  msg += ", \"server\" : \"";
  msg += config.mqtt_server;
  msg += "\", \"ip\" : \"";
  msg += config.mqtt_ip;
  msg += "\", \"port\" : ";
  msg += config.mqtt_port;
  msg += ", \"username\" : \"";
  msg += config.mqtt_user;
  msg += "\", \"state_topic\" : \"";
  msg += config.mqtt_state;
  msg += "\", \"do_topic\" : \"";
  msg += config.mqtt_do;
  msg += "\", \"lwt_topic\" : \"";
  msg += config.mqtt_lwt;
  msg += "\" } , \"timeout\" : ";
  msg += config.timeout;
  msg += ", \"pin\": { \"in1\" : ";
  msg += config.pin_in1;
  msg += ", \"in2\" : ";
  msg += config.pin_in2;
  msg += ", \"opened\" : ";
  msg += config.pin_opened;
  msg += ", \"closed\" : ";
  msg += config.pin_closed;
  msg += ", \"do_open\" : ";
  msg += config.pin_doopen;
  msg += ", \"do_close\" : ";
  msg += config.pin_doclose;
  msg += " } }, \"state\" : { \"name\" : \"";
  msg += getStateName();
  msg += "\", \"number\" : ";
  msg += curState;
  msg += ", \"in1\" : ";
    msg += digitalRead(config.pin_in1);
  msg += ", \"in2\" : ";
    msg += digitalRead(config.pin_in2);
  msg += ", \"opened\" : ";
      msg += digitalRead(config.pin_opened);
  msg += ", \"closed\" : ";
    msg += digitalRead(config.pin_closed);
  msg += ", \"do_open\" : ";
    msg += digitalRead(config.pin_doopen);
  msg += ", \"do_close\" : ";
    msg += digitalRead(config.pin_doclose);
  msg += "} }";

  server->send(200, "application/json", msg);
}


/***********************************
 * Stop the motor by setting in1, 
 * in2 to low
 ***********************************/
void motorStop() 
{
  timeoutRunning = false;
  
  digitalWrite(config.pin_in1, LOW);
  digitalWrite(config.pin_in2, LOW); 
}

/***********************************
 * Start the motor to close the blind
 ***********************************/
void motorClose()
{
  timeoutStart = millis();
  timeoutRunning = true;

  digitalWrite(config.pin_in1, LOW);
  digitalWrite(config.pin_in2, HIGH); 
}

/***********************************
 * Start the motor to open the blind
 ***********************************/
void motorOpen()
{
  timeoutStart = millis();
  timeoutRunning = true;

  digitalWrite(config.pin_in1, HIGH);
  digitalWrite(config.pin_in2, LOW); 
}

/***********************************
 * Set current state = Closed
 ***********************************/
void setStateClosed()
{
  log(LOG_INFO, "setStateClosed...");

  do_close = false;
  do_open = false;
  do_stop = false;
  
  motorStop(); 
  if (curState != STATE_CLOSED)
  {
    log(LOG_INFO, "Blind is closed.");
    curState = STATE_CLOSED;
    if (client.connected() )
    {
      char cState[config.mqtt_state.length() +1];
      config.mqtt_state.toCharArray(cState, config.mqtt_state.length()+1);
      client.publish(cState, getStateName(), false);
    }
  }  
}

/***********************************
 * Set current state = Closing
 ***********************************/
void setStateClosing() 
{
  log(LOG_INFO, "setStateClosing...");

  do_close = false;
  do_open = false;
  do_stop = false;
  
  motorClose(); 
  if (curState != STATE_CLOSING)
  {
    log(LOG_INFO, "Blind will close...");
    curState = STATE_CLOSING;
    if (client.connected() )
    {
      char cState[config.mqtt_state.length() +1];
      config.mqtt_state.toCharArray(cState, config.mqtt_state.length()+1);
      client.publish(cState, getStateName(), false);
    }
  }  
}

/***********************************
 * Set current state = Open
 ***********************************/
void setStateOpen() 
{
  log(LOG_INFO, "setStateOpen...");
  
  do_close = false;
  do_open = false;
  do_stop = false;
  
  motorStop(); 
  if (curState != STATE_OPEN)
  {
    log(LOG_INFO, "Blind is open.");
    curState = STATE_OPEN;
    if (client.connected() )
    {
      char cState[config.mqtt_state.length() +1];
      config.mqtt_state.toCharArray(cState, config.mqtt_state.length()+1);
      client.publish(cState, getStateName(), false);
    }
  }
}

/***********************************
 * Set current state = Opening
 ***********************************/
void setStateOpening() 
{
  log(LOG_INFO, "setStateOpening...");

  do_close = false;
  do_open = false;
  do_stop = false;
  
  motorOpen(); 
  if (curState != STATE_OPENING)
  {
    log(LOG_INFO, "Blind will open...");
    curState = STATE_OPENING;
    if (client.connected() )
    {
      char cState[config.mqtt_state.length() +1];
      config.mqtt_state.toCharArray(cState, config.mqtt_state.length()+1);
      client.publish(cState, getStateName(), false);
    }
  }
}

/***********************************
 * Set current state = Unknown
 ***********************************/
void setStateUnknown() 
{
  log(LOG_INFO, "setStateUnknown...");

  do_close = false;
  do_open = false;
  do_stop = false;

  motorStop(); 
  if (curState != STATE_UNKNOWN)
  {
    log(LOG_INFO, "Blind will stop...");
    curState = STATE_UNKNOWN;
    if (client.connected() )
    {
      char cState[config.mqtt_state.length() +1];
      config.mqtt_state.toCharArray(cState, config.mqtt_state.length()+1);
      client.publish(cState, getStateName(), false);
    }
  }
}

/***********************************
 * Handle inputs and outputs
 ***********************************/
void HandlePins() 
{
 /* -----------------------------------------------------------
  * -----------------------------------------------------------*/ 
  openedSW->update();  // call this every loop to update switch state
  closedSW->update();  // call this every loop to update switch state
  doOpenSW->update();  // call this every loop to update switch state
  doCloseSW->update(); // call this every loop to update switch state
  
  //log(LOG_INFO, "HandlePins: openedSW:closedSW:doOpenSW:doCloseSW - %i:%i:%i:%i", openedSW->isDown(), closedSW->isDown(), doOpenSW->isDown(), doCloseSW->isDown());
  
  // ****************************************************************
  // check for timeout:
  if (timeoutRunning && ((millis() - timeoutStart) >= (config.timeout*1000))) 
  {
    log(LOG_INFO, "Timeout! stopping motor...");
    setStateUnknown();
  }
  // ****************************************************************
  // state machine:
  switch (curState)
  {  
    case STATE_OPEN:
      if ((doCloseSW->isChanged() && doCloseSW->isDown()) || do_close) 
      {
        log(LOG_INFO, "OPEN: doClose!");
        setStateClosing();
      }
      if (do_stop)
      {
        log(LOG_INFO, "OPEN: doStop!");
        setStateOpen();
      }
      break;
    case STATE_OPENING:
      if (openedSW->isChanged() && openedSW->isDown())
      {
        log(LOG_INFO, "OPENING: opened!");
        setStateOpen();
      }
      else if ( (doCloseSW->isChanged() && doCloseSW->isDown()) || do_stop)
      {
        log(LOG_INFO, "OPENING: doStop or doClose!");
        setStateUnknown();
      }
      else if (do_close)
      {
        log(LOG_INFO, "OPENING: do_close!");
        setStateUnknown();    // stop motor running
        setStateClosing();    // reverse motor
      }
      break;
    case STATE_CLOSING:
      if (closedSW->isChanged() && closedSW->isDown())
      {
        log(LOG_INFO, "CLOSING: closed!");
        setStateClosed();
      }
      else if ( (doOpenSW->isChanged() && doOpenSW->isDown()) || do_stop)
      { 
        log(LOG_INFO, "CLOSING: doOpen or doStop!");
        setStateUnknown();
      }
      else if (do_open)
      { 
        log(LOG_INFO, "CLOSING: do_open !");
        setStateUnknown();    // stop motor running
        setStateOpening();    // reverse motor
      }
      break;
    case STATE_CLOSED:
      if ( (doOpenSW->isChanged() && doOpenSW->isDown()) || do_open)
      {
        log(LOG_INFO, "CLOSED: doOpen!");
        setStateOpening();
      }
      break;
    case STATE_UNKNOWN:
      if ( (doOpenSW->isChanged() && doOpenSW->isDown()) || do_open)
      {
        log(LOG_INFO, "UNKNOWN: doOpen!");
        setStateOpening();
      }
      else if ( (doCloseSW->isChanged() && doCloseSW->isDown()) || do_close)
      {
        log(LOG_INFO, "UNKNOWN: doClose!");
        setStateClosing();
      }
      break;
  }
  // ****************************************************************
}

/***********************************
 * MQTT Callback hook
 ***********************************/
void callback(char* topic, byte* payload, unsigned int length) 
{
  payload[length] = '\0';
  String strTopic = String((char*)topic);
  if (strTopic == config.mqtt_do) 
  {
    char * msg = (char*)payload;
    log(LOG_INFO, "mqtt_do: %s", msg);
    
    if (strcmp(msg, "open")==0)
    {
      do_close = false;
      do_open = true;
      do_stop = false;
    }
    if (strcmp(msg, "close") == 0)
    {
      do_close = true;
      do_open = false;
      do_stop = false;
    }
    if (strcmp(msg, "stop") == 0)
    {
      do_close = false;
      do_open = false;
      do_stop = true;
    }
  }
}

/***********************************
 * Makes a count of the total uptime 
 * since last start 
 ***********************************/
void uptime()
{
  /** Making Note of an expected rollover *****/   
  if(millis()>=3000000000){ 
    HighMillis=1;
  }

  /** Making note of actual rollover **/
  if(millis()<=100000&&HighMillis==1){
    Rollover++;
    HighMillis=0;
  }

  long secsUp = millis()/1000;
  Second = secsUp%60;
  Minute = (secsUp/60)%60;
  Hour = (secsUp/(60*60))%24;
  Day = (Rollover*50)+(secsUp/(60*60*24));  // First portion takes care of a rollover [around 50 days]                     
}

/***********************************
 * Create the uptime as string
 ***********************************/
String uptime2String()
{
  char  buffer [20]="";
  sprintf(buffer, "%d D %02d:%02d:%02d", Day, Hour, Minute, Second);

  return String(buffer);
}

/***********************************
 * Create the mac as string
 ***********************************/
String mac2String(byte ar[]){
  String s;
  for (byte i = 0; i < 6; ++i)
  {
    char buf[3];
    sprintf(buf, "%2X", ar[i]);
    s += buf;
    if (i < 5) s += ':';
  }
  return s;
}

/***********************************
 * main Loop
 ***********************************/
void loop() 
{
  // put your main code here, to run repeatedly:
  uptime(); //Runs the uptime script located below the main loop and reenters the main loop

  if (WiFi.status() != WL_CONNECTED || WiFi.localIP() == IPAddress(0,0,0,0)) {
    if ( (millis() - lastWiFIConnectAttempt)>5000)
    {
      log(LOG_INFO, "WiFi is disconnected, trying reconnect...");
      WiFi.reconnect();
      delay(1000);
      lastWiFIConnectAttempt = millis();     
    }
  }
  if (WiFi.status() == WL_CONNECTED )
  {
    PubSubConnect();
    PubSubLoop();
  }
  if (config.enable) 
  {
    HandlePins();
  }
  
  if (WiFi.status() == WL_CONNECTED )
  {
    // ****************************************************************
    // handle web requests:
    server->handleClient();       // handle webServer
  }

}
