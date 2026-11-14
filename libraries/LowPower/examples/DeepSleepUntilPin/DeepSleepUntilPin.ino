// Deep sleep until a pin goes LOW (RP2350 only).  Connect a button between GP15 and GND; the internal pull-up keeps the pin HIGH.
// The chip reboots when the button is pressed; the wake reason and a retained counter are printed on Serial1 (GP0 TX).
#include <LowPower.h>

const pin_size_t WAKE_GPIO = 15;
uint32_t LOWPOWER_RETAINED(wakeCount);

void setup() {
  Serial1.begin(115200);
  delay(100);
  wakeCount++;
  Serial1.printf("boot: woke from deep sleep=%d, pin wake=%d, wakeCount=%lu\n", (int)LowPower.wokeFromDeepSleep(),
                 (int)(LowPower.deepSleepWakeReason() == LowPower.WAKE_PIN), (unsigned long)wakeCount);
  pinMode(WAKE_GPIO, INPUT_PULLUP);
  delay(10);
  Serial1.println("deep sleep until GP15 is LOW");
  Serial1.flush();
  int rc = LowPower.deepSleepUntilPin(WAKE_GPIO, LOW);
  Serial1.printf("deepSleepUntilPin failed: %d\n", rc);
}

void loop() {
  delay(1000);
}
