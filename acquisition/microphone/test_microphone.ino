int analog_input = A0;
int digital_input = 3;

const int SAMPLE_WINDOW = 50; // fenêtre de mesure en ms

void setup() {
  pinMode(analog_input, INPUT);
  pinMode(digital_input, INPUT);
  Serial.begin(9600);
  Serial.println("KY-037 Détection du bruit");
}

void loop() {
  unsigned long startMillis = millis();
  int signalMax = 0;
  int signalMin = 1023;

  // Échantillonne en continu pendant 50 ms
  while (millis() - startMillis < SAMPLE_WINDOW) {
    int sample = analogRead(analog_input);
    if (sample > signalMax) signalMax = sample;
    if (sample < signalMin) signalMin = sample;
  }

  int peakToPeak = signalMax - signalMin;          // 0 à 1023
  float volts = (peakToPeak * 5.0) / 1023.0;

  int digital_value = digitalRead(digital_input);

  Serial.print("Amplitude: ");
  Serial.print(peakToPeak);
  Serial.print("  (");
  Serial.print(volts, 2);
  Serial.print(" V)  Seuil: ");
  Serial.println(digital_value == 1 ? "atteint" : "pas encore atteint");

  delay(100);
}