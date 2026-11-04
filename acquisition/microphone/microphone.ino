
const unsigned long INTERVAL_US = 125;
unsigned long nextTime = 0;

float dc = 0;
const float GAIN = 8.0f;

void setup() {
  Serial.begin(115200);

  // ADC plus rapide : horloge ADC = 500 kHz
  ADCSRA = (ADCSRA & 0xF8) | 0x05;

  // Initialise la moyenne à partir de la première mesure
  dc = analogRead(A0);
  nextTime = micros();
}

void loop() {
  while ((long)(micros() - nextTime) < 0) {
  }

  nextTime += INTERVAL_US;

  int raw = analogRead(A0);

  // Retire progressivement la composante continue
  dc += 0.001f * (raw - dc);

  // Amplifie les variations du microphone
  int sample = 128 + (int)((raw - dc) * GAIN);
  sample = constrain(sample, 0, 255);

  Serial.write((uint8_t)sample);
}
