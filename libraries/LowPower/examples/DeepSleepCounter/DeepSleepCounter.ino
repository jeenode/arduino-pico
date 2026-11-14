// Deep sleep (RP2350 only, about 0.2 mA on a Pico 2 W): the chip reboots on every wake, so the work is done in setup() and a counter
// declared LOWPOWER_RETAINED survives the sleep.  Output on Serial1 (GP0 TX); USB is gone while the chip is off.
#include <LowPower.h>

uint32_t LOWPOWER_RETAINED(wakeCount);  // zero after power on / reset, kept through deep sleep

void setup() {
  Serial1.begin(115200);
  delay(100);
  wakeCount++;
  Serial1.printf("boot: woke from deep sleep=%d reason=%d (1 = pin, 2 = timer), wakeCount=%lu\n",
                 (int)LowPower.wokeFromDeepSleep(), (int)LowPower.deepSleepWakeReason(), (unsigned long)wakeCount);
  // ... measure, send, log ... then sleep for 10 s; the call does not return
  Serial1.println("deep sleep for 10 s");
  Serial1.flush();
  int rc = LowPower.deepSleepFor(10000);
  Serial1.printf("deepSleepFor failed: %d\n", rc);  // only reached on an error (for example on an RP2040)
}

void loop() {
  delay(1000);
}
