const int REED_PIN = 10;

int previousState = HIGH;

unsigned long lastTriggerMs = 0;
unsigned long recordedLapMs = 0;

void setup() {
    Serial.begin(115200);
    pinMode(REED_PIN, INPUT_PULLUP);
}

void loop() {
    int currentState = digitalRead(REED_PIN);

    if (previousState == HIGH && currentState == LOW) {
        unsigned long now = millis();

        if (now - lastTriggerMs > 150) {
            lastTriggerMs = now;
            recordedLapMs = now;

            Serial.println(recordedLapMs);
        }
    }

    previousState = currentState;
}