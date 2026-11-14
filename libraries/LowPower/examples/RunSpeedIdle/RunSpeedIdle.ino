// RunSpeedIdle: stay awake but run slowly.  At 6 MHz from the crystal the board draws about 3 mA instead of 17-25 mA at full speed
// (RP2040 and RP2350 on the Arm cores; see docs/lowpower.rst).  Sleeping is cheaper still when the CPU can wait: use this when it cannot.
// Serial1 (GP0 TX / GP1 RX) keeps its 115200 baud, the library sets it again after every speed change.
#include <LowPower.h>

void onSpeed(LowPowerClass::RunSpeed speed) {
  // Re-initialise here what the library cannot re-time itself (PIO programs, SerialPIO, Servo, I2S, a PWM frequency set with analogWriteFreq()).
  // Called after setRunSpeed() and after every sleep that restored a slow run speed.
  (void)speed;
}

void setup() {
  Serial1.begin(115200);
  LowPower.setRunSpeedCallback(onSpeed);
#ifndef PICO_CYW43_SUPPORTED
  pinMode(LED_BUILTIN, OUTPUT);  // on a Pico W / Pico 2 W the LED belongs to the WiFi chip, which has to stay off for 12 / 6 MHz
#endif
  int rc = LowPower.setRunSpeed(LowPower.RUN_6M);
  Serial1.printf("run speed 6 MHz: rc=%d (0 = ok)\n", rc);
}

void loop() {
#ifndef PICO_CYW43_SUPPORTED
  digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
#endif
  Serial1.printf("awake at 6 MHz, millis=%lu\n", (unsigned long)millis());
  Serial1.flush();
  delay(1000);  // delay() is timer based and not affected by the speed
}
