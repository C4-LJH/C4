#include <SPI.h>
#include <WizFi250.h>
#include <PubSubClient.h>
#include <Servo.h>
#include <DHT11.h>

const char *ssid = "iptime"; // Wifi ssid
const char *password = "12341234"; // Wifi password
const char *mqtt_server = "192.168.0.7"; // mqtt 서버

int angle = 0; // 서보모터의 각도
int windowAngle = 0; // 창문 각도
int cdsVal = 0; // 조도 센서 값 저장
int fireS = 8; // 화재 감지 센서 핀
int speaker = 9; // 스피커 핀
int servoPin = 10; // 서보모터 핀
int windowServo = 6; // 창문 서보모터 핀
int dhtPin = 7; // 온습도 센서 핀
int ledPin = 5; // LED 핀
bool getKma = LOW; // 기상청 파싱 상태
bool fireState = LOW; // 화재 발생 상태
bool autoState = LOW; // 자동화 스위치 상태
int fireVal = 0; // 화재 감지 센서 디지털 값 저장
float h, t; // 습도, 온도 값 저장
float rtemp, rhumi; // 기상청 온습도
unsigned long prevMillis = 0;
unsigned long rMillis = 0;
char msg[10];

Servo myServo;
DHT11 dht11(dhtPin);
WiFiClient wifiClient;
PubSubClient client(wifiClient);

void setup_wifi();
void callback(char* topic, byte* payload, unsigned int length);
void reconnect();

void setup() {
  Serial.begin(9600);
  pinMode(fireS, INPUT); // 화재 감지 센서
  pinMode(ledPin, OUTPUT); // LED
  setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
}

void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.init();
  WiFi.begin((char*)ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect("wifiClient")) {
      Serial.println("connected");
      // Once connected, publish an announcement...
      client.publish("k/uzo", "WizFi250 hello world");
      // ... and resubscribe
      //client.subscribe("home/fire");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void arrayToString(byte *array, unsigned int len, char *buffer) {
  for (unsigned int i = 0; i < len; i++) {
    byte nib1 = (array[i] >> 4) & 0x0F;
    byte nib2 = (array[i] >> 0) & 0x0F;
    buffer[i*2+0] = nib1  < 0xA ? '0' + nib1  : 'A' + nib1  - 0xA;
    buffer[i*2+1] = nib2  < 0xA ? '0' + nib2  : 'A' + nib2  - 0xA;
  }
  
  buffer[len*2] = '\0';
}

void getBirght() {
  cdsVal = analogRead(A0) / 10;
  snprintf (msg, 75, "%d", cdsVal);
  client.publish("c4/bright", msg);
}

void setAuto(byte *payload) {
  if ((char)payload[0] == '1') {
    autoState = HIGH;
  }
  else {
    autoState = LOW;
  }
}

void autoOpt() {
  if (autoState == HIGH) {
    if (cdsVal <= 50) { // 자동 점등
      client.publish("c4/light", "1");
    }
    else if (t > rtemp) {
      client.publish("c4/window", "1");
    }
    else if (t < rtemp) {
      client.publish("c4/window", "0");
    }
  }
}

void setWindow(byte *payload) {
  if ((char)payload[0] == '1') {
    myServo.attach(windowServo);
    for (; windowAngle >= 0; windowAngle--) {
      myServo.write(windowAngle);
      delay(15);
    }
    myServo.detach();
  }
  else {
    myServo.attach(windowServo);
    for (; windowAngle <= 90; windowAngle++) {
      myServo.write(windowAngle);
      delay(15);
    }
    myServo.detach();
  }
}

void detectFire() {
  fireVal = digitalRead(fireS);

  if (fireVal == HIGH) {
    noTone(speaker);
    if (fireState == LOW) {
      Serial.println("화재 조심");
      client.publish("c4/fire", "0"); // 화재 감지되지 않음
      if (angle == 90) {
        myServo.attach(servoPin);
        angle = 0;
        myServo.write(angle);
        delay(500);
        myServo.detach();
      }
      fireState = HIGH;
    }
  }
  else {
    playTone(300, 160);
    if (fireState == HIGH) {
      Serial.println("화재 발생!");
      client.publish("c4/fire", "1"); // 화재 상황 발생
      myServo.attach(servoPin);
      angle = 90;
      myServo.write(angle);
      delay(500);
      myServo.detach();
      fireState = LOW;
    }
    playTone(300, 160);
    delay(300);
    playTone(300, 160);
  }
}

void playTone(long duration, int freq) {
  duration *= 1000;
  int period = (1.0 / freq) * 1000000;
  long elapsed_time = 0;
  
  while (elapsed_time < duration) {
    digitalWrite(speaker, HIGH);
    delayMicroseconds(period / 2);
    digitalWrite(speaker, LOW);
    delayMicroseconds(period / 2);
    elapsed_time += period;
  }
}

void ledOnOff(byte *payload) {
  if ((char)payload[0] == '1') {
    digitalWrite(ledPin, HIGH);   // Turn the LED on (Note that LOW is the voltage level
  }
  else {
    digitalWrite(ledPin, LOW);  // Turn the LED off by making the voltage HIGH
  }
}

void getDHT() {
  dht11.read(h, t);

  client.publish("c4/humi", String(h, 1).c_str());
  client.publish("c4/temp", String(t, 1).c_str());
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  if (strcmp("c4/light", topic) == 0) { // LED 원격 제어
    ledOnOff(payload);
  }
  else if (strcmp("c4/window", topic) == 0) {
    setWindow(payload);
  }
  else if (strcmp("c4/auto", topic) == 0) {
    setAuto(payload);
  }
  else if (strcmp("c4/rtemp", topic) == 0) {
    rtemp = atof((char *)payload);
    Serial.println(rtemp);
  }
  else if (strcmp("c4/rhumi", topic) == 0) {
    rhumi = atof((char *)payload);
    Serial.println(rhumi);
  }
}

void loop() {
  unsigned long currentMillis = millis();
  if (!client.connected()) {
    reconnect();
  }
  
  client.loop();
  if (currentMillis - prevMillis > 3000) { // 이전 수행 1초 후 다시 수행
    prevMillis = currentMillis;
    detectFire();
    getBirght();
    getDHT();
    autoOpt();
    client.subscribe("c4/fire");
    client.subscribe("c4/bright");
    client.subscribe("c4/temp");
    client.subscribe("c4/humi");
    client.subscribe("c4/light");
    client.subscribe("c4/window");
    client.subscribe("c4/auto");
    client.subscribe("c4/rhumi");
    client.subscribe("c4/rtemp");
  }
}
