const unsigned long INTERVAL_US = 125; // 8000 Hz
unsigned long nextTime;

void setup() {
  Serial.begin(115200);
  // ADC plus rapide (prescaler 16) pour tenir 8 kHz
  ADCSRA = (ADCSRA & 0xF8) | 0x04;
  nextTime = micros();
}

void loop() {
  while ((long)(micros() - nextTime) < 0) {}
  nextTime += INTERVAL_US;
  Serial.write(analogRead(A0) >> 2); // 10 bits -> 8 bits
}