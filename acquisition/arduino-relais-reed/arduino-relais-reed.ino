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

    Serial.println("KY-021 INITIALISE");
    Serial.println("En attente d'un passage...");
}

void loop() {
    if (newLapFlag) {
        noInterrupts();
        unsigned long lapTime = recordedLapMs;
        newLapFlag = false;
        interrupts();

        Serial.println("----------------------------");
        Serial.println("NOUVEAU PASSAGE DETECTE !");
        Serial.print("Temps : ");
        Serial.print(lapTime / 1000.0, 3);
        Serial.println(" secondes");
        Serial.println("----------------------------");
    }
}