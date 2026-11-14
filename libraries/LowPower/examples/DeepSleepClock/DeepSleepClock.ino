// Deep sleep with a clock (RP2350 only): the always-on timer keeps wall-clock time through the sleep, so time() is right after every wake without a
// resync.  Set the time once (here from the compile time; in a real sketch from NTP, a host or the user), then print time and the supply voltage on every wake.
// Output on Serial1 (GP0 TX); USB is gone while the chip is off.  The oscillator that runs the timer in deep sleep is slow by about 0.5 %
// (20 s per hour) on the chip measured; setTimeTrimPpm() corrects it once you have measured your own (or resync with NTP now and then).
#include <LowPower.h>
#include <time.h>

uint32_t LOWPOWER_RETAINED(wakeCount);  // zero after power on / reset, kept through deep sleep

static time_t buildTime() {
  static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
  char mon[4] = {};
  int d = 1, y = 2000, hh = 0, mm = 0, ss = 0;
  sscanf(__DATE__, "%3s %d %d", mon, &d, &y);
  sscanf(__TIME__, "%d:%d:%d", &hh, &mm, &ss);
  struct tm tm = {};
  tm.tm_year = y - 1900;
  tm.tm_mon = (strstr(months, mon) - months) / 3;
  tm.tm_mday = d;
  tm.tm_hour = hh;
  tm.tm_min = mm;
  tm.tm_sec = ss;
  return mktime(&tm);
}

void setup() {
  Serial1.begin(115200);
  delay(100);
  wakeCount++;
  if (!LowPower.timeValid()) {
    // first boot after power on: the always-on timer is not running yet
    LowPower.setTime(buildTime());
    // LowPower.setTimeTrimPpm(5400);  // optional: the trim measured for this chip
  }
  time_t now = time(nullptr);
  struct tm t;
  gmtime_r(&now, &t);
  Serial1.printf("wake %lu (deep sleep: %d): %04d-%02d-%02d %02d:%02d:%02d, VSYS %.2f V\n", (unsigned long)wakeCount,
                 (int)LowPower.wokeFromDeepSleep(), t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec, (double)LowPower.readVsys());
  Serial1.println("deep sleep for 10 s");
  Serial1.flush();
  int rc = LowPower.deepSleepFor(10000);
  Serial1.printf("deepSleepFor failed: %d\n", rc);  // only reached on an error (for example on an RP2040)
}

void loop() {
  delay(1000);
}
