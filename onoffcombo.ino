#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
// #include <ESP8266mDNS.h>
#include <WiFiClient.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

IPAddress ip(0, 0, 0, 0); // La IP de este ESP8266 (LO UNICO A CAMBIAR GENERALMENTE)
IPAddress gateway(10, 0, 0, 1); // La IP del domohub
IPAddress subnet(255, 255, 255, 0);

String version = "1.5.3";

const char* ssid = "pon aqui el ssid";
const char* password = "las pass";

// TODO: sacar esto de BBDD
int myPins[] = {0, 1, 2, 3, 12}; // Declaramos aquí los pines que vamos a usar en la configuración cliente (sender)
int myValues[] = {0, 0, 0, 0, 0}; // Aquí los estados (por defecto todos LOW)
int myPinsSize = 5; // La cantidad de pines para ahorrar CPU

String tipo;
String conf0;
String conf1;
String conf2;
String conf3;

#define DEBUG // Comentar si no queremos imprimir nada (y queremos usar el RX y TX como GPIO)
#ifdef DEBUG
  #define DEBUG_PRINT(x)  Serial.print (x)
#else
  #define DEBUG_PRINT(x);
  void wifi_status_led_uninstall();
#endif

ESP8266WebServer server(80);

void parseBytes(const char* str, char sep, byte* bytes, int maxBytes, int base) {
    for (int i = 0; i < maxBytes; i++) {
        bytes[i] = strtoul(str, NULL, base);
        str = strchr(str, sep);
        if (str == NULL || *str == '\0') {
            break;
        }
        str++;
    }
}

int indice(int i) {
  for (int thisPin = 0; thisPin < myPinsSize; thisPin++) {
    if (myPins[thisPin] == i) {
      return thisPin;
    }
  }
}

void handleRoot() {
  server.send(200, "text/plain", "Hola desde la esp8266!");
}

void handleNotFound(){
  String message = "No encontrado\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET)?"GET":"POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i=0; i<server.args(); i++){
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}

void encender() {
  server.send(200, "text/plain", "Encendiendo!");
  String id = server.arg("pin");
  uint8_t pin = atoi (id.c_str ());
  digitalWrite(pin, HIGH);
  int posicion = indice(pin);
  myValues[posicion] = 1;
  DEBUG_PRINT("Pin ");
  DEBUG_PRINT(pin);
  DEBUG_PRINT(" puesto en HIGH\n");
}

void apagar() {
  server.send(200, "text/plain", "Apagando!");
  String id = server.arg("pin");
  uint8_t pin = atoi (id.c_str ());
  digitalWrite(pin, LOW);
  int posicion = indice(pin);
  myValues[posicion] = 0;
  DEBUG_PRINT("Pin ");
  DEBUG_PRINT(pin);
  DEBUG_PRINT(" puesto en LOW\n");
}

void turn() {
  String id = server.arg("pin");
  int nuevoEstado;
  uint8_t pin = atoi (id.c_str ());
  int posicion = indice(pin);
  int estado = myValues[posicion];
  DEBUG_PRINT("indice: ");
  DEBUG_PRINT(posicion);
  DEBUG_PRINT("\n");
  if (estado == 1) {
    nuevoEstado = 0;
    digitalWrite(pin, LOW);
    myValues[posicion] = nuevoEstado;
  } else {
    nuevoEstado = 1;
    digitalWrite(pin, HIGH);
    myValues[posicion] = nuevoEstado;
  }
  String mensaje;
  mensaje += "Pin ";
  mensaje += pin;
  mensaje += " puesto a ";
  mensaje += nuevoEstado;
  mensaje += "\n";
  server.send(200, "text/plain", mensaje);
}

void getStatus() {
  String estado;
  for (int thisPin = 0; thisPin < myPinsSize; thisPin++) {
    estado += myPins[thisPin];
    estado += ": ";
    estado += myValues[thisPin];
    estado += "\n";
  }
  server.send(200, "text/plain", estado);
}

void getVersion() {
  server.send(200, "text/plain", String(version));
}

void doRestart() { // RECORDATORIO: el primer reset despues de un flasheo siempre falla, mejor hacer uno a mano postflasheo
  server.send(200, "text/plain", "reiniciando");
  ESP.reset();
}

void peticionHTTP(char* host, String accion, char* pin) {
  WiFiClient client;
  const int httpPort = 80;
  const char* ipStr = host;
  byte ip[4];
  parseBytes(ipStr, '.', ip, 4, 10);
  String url = "/" + accion + "?pin=" + pin;
  DEBUG_PRINT("URL: http://" + String(host) + url + "\n");
  if (!client.connect(ip, httpPort)) {
    DEBUG_PRINT("Fallo conexion con webserver\n");
    return;
  }
  client.print(String("GET ") + url + " HTTP/1.1\r\n" +
               "Host: " + host + "\r\n" + 
               "Connection: close\r\n\r\n");
  unsigned long timeout = millis();
  while (client.available() == 0) {
    if (millis() - timeout > 5000) {
      DEBUG_PRINT(">>> Se ha alcanzado el timeout con el webserver!\n");
      client.stop();
      return;
    }
  }
  // Informamos al domohub de la accion que acabamos de hacer para que actualice el estado tambien
  if (!client.connect(gateway, httpPort)) {
    DEBUG_PRINT("Fallo conexion con Domohub\n");
    return;
  }
  String urlUpdate = "/cgi-bin/updateStatus.cgi?alias=" + String(host) + "&status=" + accion + "&pin=" + pin;
  client.print(String("GET ") + urlUpdate + " HTTP/1.1\r\n" +
               "Host: " + gateway.toString() + "\r\n" + 
               "Connection: close\r\n\r\n");
  unsigned long timeout2 = millis();
  while (client.available() == 0) {
    if (millis() - timeout2 > 5000) {
      DEBUG_PRINT(">>> Se ha alcanzado el timeout con el webserver!\n");
      client.stop();
      return;
    }
  }
  // Termina la actualización del domohub
}

void autoconf() {
  DEBUG_PRINT("Lanzando autoconfiguracion\n");
  WiFiClient client;
  const int httpPort = 80;
  while (!client.connect(gateway, httpPort)) {
    DEBUG_PRINT("\nFallo conexion con Domohub. Reintentando...\n");
    delay(5000);
  }
  String url = "/cgi-bin/autoconf.cgi?ip=" + WiFi.localIP().toString();
  DEBUG_PRINT("URL: ");
  DEBUG_PRINT(url);
  DEBUG_PRINT("\n");
  client.print(String("GET ") + url + " HTTP/1.1\r\n" +
               "Host: " + gateway.toString() + "\r\n" + 
               "Connection: close\r\n\r\n");
  unsigned long timeout = millis();
  while (client.available() == 0) {
    if (millis() - timeout > 5000) {
      DEBUG_PRINT(">>> Se ha alcanzado el timeout con Domohub!\n");
      client.stop();
      return;
    }
  }
  while(client.available()){
    String line = client.readStringUntil('\r');
    if (line.substring(1,6) == "CONFT") {
      tipo = line.substring(7);
    }
    if (line.substring(1,6) == "CONF0") {
      conf0 = line.substring(7);
    }
    if (line.substring(1,6) == "CONF1") {
      conf1 = line.substring(7);
    }
    if (line.substring(1,6) == "CONF2") {
      conf2 = line.substring(7);
    }
    if (line.substring(1,6) == "CONF3") {
      conf3 = line.substring(7);
    }
  }

  // Comenzamos aquí la autoconfiguración
  DEBUG_PRINT("Este ESP8266 será configurado como: " + tipo + "\n");
  
  if (tipo == "webclient") {
    #ifndef DEBUG      // Ponemos el TX y RX en modo GPIO si NO estamos en modo DEBUG
      pinMode(1, FUNCTION_3);
      pinMode(3, FUNCTION_3);
      for (int thisPin = 0; thisPin < myPinsSize; thisPin++) {       // Ponemos todos los pines que usamos en modo ENTRADA
        pinMode(myPins[thisPin], INPUT_PULLUP);
      }
    #else
      pinMode(2, INPUT_PULLUP); // En INPUT solo el pin 2 que es el unico que tenemos libre durante la programación
    #endif
  } else if (tipo == "webserver") {
    #ifndef DEBUG     // Ponemos el TX y RX en modo GPIO si no estamos en modo DEBUG
      pinMode(1, FUNCTION_3);
      pinMode(3, FUNCTION_3);
      for (int thisPin = 0; thisPin < myPinsSize; thisPin++) {      // Ponemos todos los pines que usamos en modo SALIDA
        pinMode(myPins[thisPin], OUTPUT);
      }
    #else
      pinMode(0, OUTPUT);
      pinMode(2, OUTPUT);
    #endif
  } else if (tipo == "sonoffbasic") {
      pinMode(14, FUNCTION_3); // El GPIO14
      pinMode(12, FUNCTION_3); // El relé
      pinMode(0, FUNCTION_3);  // El pulsador
      pinMode(14, INPUT_PULLUP);
      pinMode(12, OUTPUT);
      pinMode(0, INPUT_PULLUP);
  }
}

void setup(void){
  #ifdef DEBUG
    Serial.begin(115200);
  #endif

  WiFi.mode(WIFI_STA);
  WiFi.config(ip, gateway, subnet);
  WiFi.begin(ssid, password);
  DEBUG_PRINT("\n");
  while (WiFi.status() != WL_CONNECTED) { // Esperamos conexión WIFI
    delay(500);
    DEBUG_PRINT(".");
  }
  DEBUG_PRINT("\n");
  DEBUG_PRINT("Conectado a ");
  DEBUG_PRINT(ssid);
  DEBUG_PRINT("\n");
  DEBUG_PRINT("IP: ");
  DEBUG_PRINT(WiFi.localIP());
  DEBUG_PRINT("\n");
  autoconf(); // Lanzamos la petición de autoconfiguracion una vez que tenemos IP
  server.on("/", handleRoot);
  server.on("/on", encender);
  server.on("/off", apagar);
  server.on("/turn", turn);
  server.on("/status", getStatus);
  server.on("/restart", doRestart);
  server.on("/version", getVersion);
  server.onNotFound(handleNotFound);
  server.begin();
  DEBUG_PRINT("Servidor HTTP iniciado");
  DEBUG_PRINT("\n");
  ArduinoOTA.begin();
  DEBUG_PRINT("Activado modo OTA");
  DEBUG_PRINT("\n");
}

void loop(void){
  ArduinoOTA.handle();
  if (tipo == "webclient") {
    if (digitalRead(0) == LOW) {
      char buffer[25];
      conf0.toCharArray(buffer, 25);
      char* host = strtok(buffer, " ");
      String accion = strtok(NULL, " ");
      char* pin = strtok(NULL, " ");
      peticionHTTP(host, accion, pin);
      DEBUG_PRINT("Boton 0 pulsado\n");
      DEBUG_PRINT("host: " + String(host) + "\n");
      DEBUG_PRINT("accion: " + accion + "\n");
      DEBUG_PRINT("pin: " + String(pin) + "\n");
      digitalWrite(0, HIGH);
      delay(1000);
    }
    if (digitalRead(2) == LOW) {
      char buffer[25];
      conf2.toCharArray(buffer, 25);
      char* host = strtok(buffer, " ");
      String accion = strtok(NULL, " ");
      char* pin = strtok(NULL, " ");
      peticionHTTP(host, accion, pin);
      DEBUG_PRINT("Boton 2 pulsado\n");
      DEBUG_PRINT("host: " + String(host) + "\n");
      DEBUG_PRINT("accion: " + accion + "\n");
      DEBUG_PRINT("pin: " + String(pin) + "\n");
      digitalWrite(2, HIGH);
      delay(1000);
    }
    #ifndef DEBUG // Solo escuchamos por el pin 1 y el 3 cuando no estamos en modo debug
      if (digitalRead(1) == LOW) {
        char buffer[25];
        conf1.toCharArray(buffer, 25);
        char* host = strtok(buffer, " ");
        String accion = strtok(NULL, " ");
        char* pin = strtok(NULL, " ");
        peticionHTTP(host, accion, pin);
        DEBUG_PRINT("Boton 1 pulsado\n");
        DEBUG_PRINT("host: " + String(host) + "\n");
        DEBUG_PRINT("accion: " + accion + "\n");
        DEBUG_PRINT("pin: " + String(pin) + "\n");
        digitalWrite(1, HIGH);
        delay(1000);
      }
      if (digitalRead(3) == LOW) {
        char buffer[25];
        conf3.toCharArray(buffer, 25);
        char* host = strtok(buffer, " ");
        String accion = strtok(NULL, " ");
        char* pin = strtok(NULL, " ");
        peticionHTTP(host, accion, pin);
        DEBUG_PRINT("Boton 3 pulsado\n");
        DEBUG_PRINT("host: " + String(host) + "\n");
        DEBUG_PRINT("accion: " + accion + "\n");
        DEBUG_PRINT("pin: " + String(pin) + "\n");
        digitalWrite(3, HIGH);
        delay(1000);
      }
    #endif
    server.handleClient();
  } else if (tipo == "webserver") {
    server.handleClient();
  } else if (tipo == "sonoffbasic") {
    if ((digitalRead(0) == LOW) || (digitalRead(14) == LOW)) {
      DEBUG_PRINT("Pulsado el pulsador\n");
      int pin = 12;
      int posicion = indice(pin);
      int estado = myValues[posicion];
      int nuevoEstado;
      if (estado == 1) {
        nuevoEstado = 0;
        digitalWrite(pin, LOW);
        myValues[posicion] = nuevoEstado;
      } else {
        nuevoEstado = 1;
        digitalWrite(pin, HIGH);
        myValues[posicion] = nuevoEstado;
      }
      // Aqui va la actualización al hub para dar a conocer el nuevo estado
      WiFiClient client;
      const int httpPort = 80;
      if (!client.connect(gateway, httpPort)) {
        DEBUG_PRINT("Fallo conexion con Domohub\n");
        return;
      }
      String urlUpdate = "/cgi-bin/updateStatus.cgi?alias=" + WiFi.localIP().toString() + "&status=" + nuevoEstado + "&pin=" + pin;
      client.print(String("GET ") + urlUpdate + " HTTP/1.1\r\n" +
               "Host: " + gateway.toString() + "\r\n" + 
               "Connection: close\r\n\r\n");
      unsigned long timeout2 = millis();
      while (client.available() == 0) {
        if (millis() - timeout2 > 5000) {
          DEBUG_PRINT(">>> Se ha alcanzado el timeout con el webserver!\n");
          client.stop();
          return;
        }
      }
      // Fin de la actualización al hub
      delay(2000);
      if ((digitalRead(0) == LOW) || (digitalRead(14) == LOW)) {
        DEBUG_PRINT("Reiniciamos\n");
        ESP.reset();
      }
    }
    server.handleClient();
  } else {
    DEBUG_PRINT(".");
    delay(1000);
  }
}
