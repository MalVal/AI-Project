const uint8_t REED_PIN = 2;

volatile unsigned long lastTriggerMs = 0;
volatile unsigned long recordedLapMs = 0;
volatile bool newLapFlag = false;

void onReedTrigger() {
    unsigned long now = millis();

    if (now - lastTriggerMs > 150) {
        lastTriggerMs = now;
        recordedLapMs = now;
        newLapFlag = true;
    }
}

void setup() {
    Serial.begin(115200);

    pinMode(REED_PIN, INPUT_PULLUP);

    attachInterrupt(digitalPinToInterrupt(REED_PIN), onReedTrigger, FALLING);

    delay(100);
}

void loop() {
    if (newLapFlag) {
        newLapFlag = false;

        Serial.println(recordedLapMs);
    }
}