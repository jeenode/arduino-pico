Low Power (Sleep and Dormant)
=============================

The ``LowPower`` library puts the RP2040 or RP2350 into a low power state and wakes it up again from a timer or a
GPIO.  It is a thin Arduino wrapper over the Pico SDK ``pico_low_power`` library, plus the work the Arduino core
needs after a wake (restoring the CPU clock, restoring pin state).

.. code:: cpp

    #include <LowPower.h>

    void setup() {
        Serial1.begin(115200);
        pinMode(15, INPUT_PULLUP);
    }

    void loop() {
        Serial1.println("sleeping 5 seconds");
        Serial1.flush();
        LowPower.sleepFor(5000);                // light sleep, timer wake

        Serial1.println("dormant until GP15 goes low");
        Serial1.flush();
        LowPower.dormantUntilPin(15, FALLING);  // oscillators off, pin wake
    }

Modes
-----

**Sleep** gates most clocks.  The SDK's sleep leaves the oscillators and PLLs running, which is where most of the
sleep current goes.  On RP2040 and on the RP2350's ARM cores the library therefore runs the system from the crystal
and stops the system PLL and the ring oscillator around ``sleepFor`` and both forms of ``sleepUntilPin`` (about
1.3 ms of extra wake time).  What else is stopped depends on USB, and is decided at every sleep:

* ``NO_USB`` build: the USB PLL, the USB clock and the ADC clock are stopped too, and the USB pins D+ / D- are pulled
  down for the length of the sleep (the USB PHY's receivers float otherwise and draw about 0.25 mA each).
* USB build with a host using USB (connected and not suspended): the USB PLL and clock keep running, so the
  ``Serial`` port stays connected.
* USB build with no host: stopped as with ``NO_USB``; all clocks are back before the call returns, so a cable plugged
  in during a sleep enumerates normally.
* With Adafruit TinyUSB (``USE_TINYUSB``) USB is always treated as in use.  The RP2350's RISC-V cores use the SDK's
  sleep unchanged (no clock gating there).

Light sleep keeps the system timer, and therefore ``millis()``, running.

**Dormant** stops the oscillators and PLLs.  It can only be woken by a GPIO (or, with a suitable low-frequency
clock, a timer) and restarts more slowly (the crystal has to start again, about 73 ms on an RP2040).  USB is shut
down while dormant.

**Deep sleep** (RP2350 only) powers the cores and the SRAM off, except the variables declared ``LOWPOWER_RETAINED``: the board draws 0.15 to
0.2 mA and the wake is a reboot (``setup()`` runs again), see "Deep sleep" below.

Every function returns ``0`` on success or a negative Pico SDK error code (``PICO_ERROR_*``, see ``pico/error.h``).

API
---

.. code:: cpp

    int sleepFor(uint32_t ms);
    int sleepUntilPin(pin_size_t pin, PinStatus mode);                       // LOW, HIGH, RISING, FALLING
    int sleepUntilPin(pin_size_t pin, PinStatus mode, uint32_t timeoutMs);   // pin or timeout
    int dormantUntilPin(pin_size_t pin, PinStatus mode);
    int dormantFor(uint32_t ms);
    void keepRunning(uint32_t mask);   // KR_PWM, KR_PIO, KR_UART0, KR_UART1, KR_ADC, KR_DMA: keep these running in sleepFor / sleepUntilPin
    int deepSleepFor(uint32_t ms);                           // RP2350 only: the wake is a reboot, see "Deep sleep"
    int deepSleepUntilPin(pin_size_t pin, PinStatus mode);   // RP2350 only
    bool wokeFromDeepSleep() const;
    WakeReason deepSleepWakeReason() const;
    uint32_t LOWPOWER_RETAINED(name);                        // declares a variable that survives deep sleep
    void setTime(time_t epoch);                              // RP2350: time that survives deep sleep
    bool timeValid() const;
    void setTimeTrimPpm(int32_t ppm);
    float readVsys(int samples = 16);                        // supply voltage, see "Supply voltage"
    void setVsysTrim(float gain, float offsetVolts);
    void keepPins(uint64_t mask);
    void setPinRestore(bool restore);
    uint32_t lastSleepMs() const;
    WakeReason wakeReason() const;     // LowPower.WAKE_NONE, LowPower.WAKE_PIN, LowPower.WAKE_TIMER

``CHANGE`` is not supported as a wake condition (``PICO_ERROR_INVALID_ARG``).

+-------------------------+---------------------------+-----------------------------------------+
| Call                    | RP2040 (Pico, Pico W)     | RP2350 (Pico 2, Pico 2 W)               |
+=========================+===========================+=========================================+
| ``sleepFor``,           | yes                       | yes                                     |
| ``sleepUntilPin``       |                           |                                         |
| (with or without        |                           |                                         |
| timeout)                |                           |                                         |
+-------------------------+---------------------------+-----------------------------------------+
| ``dormantUntilPin``     | yes                       | yes                                     |
+-------------------------+---------------------------+-----------------------------------------+
| ``dormantFor``          | only with an external     | yes, using the always-on timer on the   |
|                         | clock, see below          | low-power oscillator                    |
+-------------------------+---------------------------+-----------------------------------------+
| Minimum ``dormantFor``  | 2000 ms                   | 10 ms                                   |
+-------------------------+---------------------------+-----------------------------------------+
| ``deepSleepFor``,       | no (``PICO_ERROR_NOT_     | yes, the SDK's switched-core            |
| ``deepSleepUntilPin``   | PERMITTED``)              | power-down, the wake is a reboot        |
+-------------------------+---------------------------+-----------------------------------------+

Shorter ``dormantFor`` requests return ``PICO_ERROR_INVALID_ARG``.  The RP2040 has no always-on timer that survives
dormant, so ``dormantFor`` needs a 32 kHz-class clock fed into GPIO 20 or 22 and registered with
``low_power_set_external_clock_source()`` before the call; without it ``PICO_ERROR_PRECONDITION_NOT_MET`` is
returned.  ``dormantUntilPin`` needs no extra hardware.  The RP2350's switched-core power-down is
``deepSleepFor`` / ``deepSleepUntilPin`` (see "Deep sleep").

Pins
----

Before going dormant every GPIO is put into its lowest-leakage state (pulls off, input off, output disconnected),
except the wake pin, pins routed to a UART (so a TX line does not float), pins routed to I2C (parking them leaves the
controller unusable until ``Wire.end()`` / ``begin()``), and the pins you list with ``keepPins(mask)`` (bit *n* =
GPIO *n*).  The state of the parked pins is saved and **restored after the wake**; ``setPinRestore(false)`` leaves
them parked.  Light sleep never parks pins.

The wake pin must be configured by the sketch before the call (``pinMode(15, INPUT_PULLUP)`` for a button to
ground); on the RP2350 an untouched pad stays isolated from the input logic and never produces the event.

A pin routed to a UART can be the wake pin (``dormantUntilPin(1, FALLING)`` with ``Serial1`` on GP1; the start bit
is the edge).  With ``dormantUntilPin`` the byte that causes the wake is **lost** (the UART clock is stopped and the
byte is over before the clocks are back): send a dummy byte first, wait for the wake and then send the data.  With
``sleepUntilPin`` the wake byte is received normally.

Waking on a pin or a timeout
----------------------------

``sleepUntilPin(pin, mode, timeoutMs)`` ends at whichever comes first; ``LowPower.wakeReason()`` tells which:

.. code:: cpp

    int rc = LowPower.sleepUntilPin(15, FALLING, 60000);
    if (rc == 0 && LowPower.wakeReason() == LowPower.WAKE_PIN) {
        // the button / radio IRQ
    } else {
        // 60 s heartbeat
    }

* ``WAKE_PIN`` and ``WAKE_TIMER`` are also what the other calls report; ``WAKE_NONE`` before the first call and after
  a call that failed.  The names belong to the library object, so they cannot clash with constants of your own.
* With ``LOW`` or ``HIGH`` and the pin already in that state the call returns at once with ``WAKE_PIN``.  If the pin
  and the timeout coincide either reason may be reported (never both).  ``timeoutMs`` of 0 is invalid.
* Pins set up with ``attachInterrupt()`` keep working after every sleep and dormant call (the library does not
  touch the core's GPIO interrupt handler); do not use the wake pin itself for ``attachInterrupt()`` as well.

``millis()`` and ``micros()`` use the system timer, which **stops in dormant**: dormant time does not appear in them.
Light sleep keeps it running.  ``lastSleepMs()`` describes the last call: the *requested* time after a successful
``sleepFor`` / ``dormantFor``, the *measured* time after ``sleepUntilPin``, ``LowPower.SLEEP_UNKNOWN``
(``0xFFFFFFFF``) after ``dormantUntilPin`` (no time reference while dormant), and ``0`` after an error or before the
first call.

Current
-------

Whole-board current at VSYS, mean of INA226 results (0.1 ohm shunt, 25 uA steps, the board's regulator included, so
the chip alone uses less).  The INA226 zero reading was -0.075 mA: add that to the RP2040 Pico column.  The
Pico was measured at 133 MHz without USB, the Pico W at 133 MHz with a 3.7 V supply (a switching regulator draws more
current from a lower supply for the same power: compare in mW, not mA).  ``NO_USB``, nothing attached, WiFi never
started:

+--------------------------------------------+---------------+---------------+
| RP2040                                     | Pico          | Pico W        |
+============================================+===============+===============+
| running (delay loop)                       | 19.7 mA       | 26.1 mA       |
+--------------------------------------------+---------------+---------------+
| ``sleepFor``, SDK sleep (PLLs left on)     | 7.9 mA        |               |
+--------------------------------------------+---------------+---------------+
| ``sleepFor`` / ``sleepUntilPin``           | 0.8 mA        | 1.4 - 1.5 mA  |
+--------------------------------------------+---------------+---------------+
| same, USB build with no host               | 1.1 mA        | 1.8 mA        |
+--------------------------------------------+---------------+---------------+
| same, USB build with a host using USB      | 3.1 mA        |               |
+--------------------------------------------+---------------+---------------+
| ``dormantUntilPin``                        | 0.8 mA        | 1.2 mA        |
+--------------------------------------------+---------------+---------------+

Dormant uses 0.07 to 0.2 mA less than light sleep but costs about 18 uC more per wake (the crystal restart), so it
pays off for sleeps longer than roughly 0.1 to 0.3 s, at the price of a wake in about 73 ms instead of 4 to 6 ms.

+--------------------------------------------+------------------------------------+
| RP2350 (Pico 2, Pico 2 W), 150 MHz         | Board current                      |
+============================================+====================================+
| running (delay loop)                       | 15.6 mA (Pico 2), 17.0 mA (2 W)    |
+--------------------------------------------+------------------------------------+
| ``sleepFor``                               | 2.2 to 4.3 mA                      |
+--------------------------------------------+------------------------------------+
| ``sleepUntilPin``, with or without timeout | 1.6 to 4.7 mA                      |
+--------------------------------------------+------------------------------------+
| ``dormantUntilPin``, ``dormantFor``        | 3.3 to 3.9 mA                      |
+--------------------------------------------+------------------------------------+
| ``deepSleepFor``, ``deepSleepUntilPin``    | 0.15 to 0.2 mA (the wake is a      |
|                                            | reboot, see "Deep sleep")          |
+--------------------------------------------+------------------------------------+

On the RP2350 prefer light sleep: dormant is no lower (the regulator only enters its low-power mode in a power-state
change).  The light sleep floor is steady within one sleep but differs from sleep to sleep in steps of about 0.6 mA
(the cause is not known; a plain Pico 2 shows the same, so it is not the CYW43), and the first sleep after a reset
can cost about 1.7 mA more.  The RISC-V cores have no clock gating: light sleep draws about 6 to 9.5 mA.

Caveats
-------

* **RP2350-E9**: a GPIO input with its internal pull-down enabled can latch high.  The library's pin parking clears it,
  and so does a wake pin on an idle-high line.  A wake pin that relies on the internal pull-down and a RISING edge
  (a button to 3.3 V) can stay high after the button is released and then not wake again: use an external
  pull-down, or wake on FALLING with a pull-up.
* **WiFi on a Pico W / Pico 2 W.**  A station that is connected before the sleep is still connected after it (the
  library keeps the CYW43 pins through dormant and ``sleepFor`` holds the WiFi driver's lock until the clocks are
  back); the cost is the chip: about 48 mA asleep while connected.  ``WiFi.end()`` leaves the network but does not
  power the chip off (about 19 mA); with WiFi never started the board draws what a board without WiFi does.
* **USB.**  ``sleepFor`` does not affect USB.  Dormant disconnects it: the board re-attaches about 0.6 s after the
  wake, the host sees the port disappear, and output printed just before or in the first second after the wake can be
  lost.  Do not call ``Serial.end()`` / ``begin()`` after a wake.  Log anything important after a dormant wake on a
  UART.  Early RP2040 chips (B0/B1) can hit a rare panic in the SDK's USB enumeration workaround (RP2040-E5) when the
  host re-enumerates after a dormant wake (about 1 in 100 wakes on one board); builds without USB (``NO_USB``) and
  RP2040 B2 / RP2350 chips are not affected.
* **Clocks.**  The SDK wakes into its default clocks; the library re-applies the core's ``F_CPU`` setting (and the
  matching core voltage and PSRAM timing) so peripherals configured before the sleep keep working.  The UART clock is
  gated while sleeping (unless ``keepRunning()`` keeps it, see below): call ``Serial1.flush()`` before the call and do not expect to receive data during sleep.
* **Peripherals** configured before the call keep working afterwards without ``begin()``: UART, SPI, I2C,
  ``SerialPIO``, PIO programs (``tone()``), ADC, and PWM (``analogWrite`` stops while dormant and resumes at the same
  frequency and duty).
* **Bluetooth (BLE) on a Pico W / Pico 2 W.**  Advertising and an established connection survive every sleep and dormant
  call: tested on a Pico 2 W with a laptop connected, the link stayed up through ``sleepFor`` of 1, 3 and 10 s,
  ``sleepUntilPin`` and ``dormantUntilPin``, and reads and notifications worked right after each wake (the link layer runs
  in the CYW43, so even a sleep longer than the 9.6 s supervision timeout the laptop chose did not drop it).  Like WiFi it needs the driver's
  lock held around the sleep, which the library does for every call.  Deep sleep reboots the chip: the stack is gone after the wake and has to be started again in ``setup()``.
* **FreeRTOS: not supported.**  In a build with ``Tools->Operating System->FreeRTOS SMP`` every sleep, dormant and deep sleep call returns
  ``PICO_ERROR_NOT_PERMITTED`` and does nothing.  FreeRTOS owns the tick (SysTick), the second core and the WiFi driver's
  tasks, which the calls change underneath it.  Tested on a Pico 2 W: with plain tasks light sleep worked, but the FreeRTOS
  tick does not run asleep (tick counts, ``vTaskDelay()`` and timeouts do not include the sleep); with ``setup1()`` /
  ``loop1()`` defined or a connected WiFi chip light sleep hung; after a dormant wake the tick never ran again.  Use
  ``vTaskDelay()`` or the SDK's own sleep with FreeRTOS.
* **Both cores.**  With ``setup1()`` / ``loop1()`` the library idles the other core around the call and resumes it
  after the wake; the lowest currents need single-core sketches.
* **Flashing a dormant chip.**  A dormant chip cannot be reached over SWD and an upload fails.  Reset it through the
  RUN pin, use BOOTSEL, or wake it first; keep a startup delay at the start of ``setup()`` so a board can always be
  re-flashed.

Wake latency (RP2040 at 133 MHz, from the start of a UART wake byte to the first output): ``dormantUntilPin`` about
73 ms, ``sleepUntilPin`` about 4 to 6 ms, ``sleepFor`` and the timeout form return at the requested time (about 1 ms late).

Keeping peripherals running in light sleep
------------------------------------------

``sleepFor()`` and ``sleepUntilPin()`` gate the clocks of every peripheral except the timer (and UART0 and USB in the pin forms)
and run the system clock from the 12 MHz crystal with the PLLs stopped.  A PWM output, a PIO program, an ADC capture or UART1 left running
therefore stops, and a byte that arrives on UART0 while the chip wakes can be damaged because the clocks restart while it is being received.
``LowPower.keepRunning(mask)`` keeps selected peripherals running; ``keepRunning(0)``, the default, is the normal gated sleep.
Dormant ignores it (the oscillators are off).  Any non-zero mask leaves the system clock on PLL_SYS, so the kept peripherals run at their
normal rate; the price is the PLL, about 4 mA on a Pico 2 W.

* ``KR_PWM`` (all PWM slices), ``KR_PIO`` (PIO0, PIO1 and, on the RP2350, PIO2), ``KR_UART1``, ``KR_DMA``.
* ``KR_ADC`` keeps the ADC, the DMA clock and PLL_USB (the ADC's clock): sampling and DMA transfers continue.
* ``KR_UART0`` keeps UART0 (``Serial1``) running in ``sleepFor()`` as well, and makes a wake on the UART's RX pin
  (``sleepUntilPin(1, FALLING)`` with ``Serial1`` on GP1) deliver the first byte intact: 8 of 8 bytes intact at 115200 baud with
  ``KR_UART0``, the first byte lost in every run without it.

Measured on a Pico 2 W (150 MHz, 3.7 V, 12 s ``sleepFor`` per case, 1 kHz PWM or PIO square wave observed on a pin, a 6000-sample DMA
capture of the ADC temperature sensor, a 16-byte UART1 loopback at 9600 baud; the plain sleep varies by about 0.4 mA between runs):

+-------------------------------------+---------------------------------+------------------------------+
| ``keepRunning`` mask                | works                           | extra current in the sleep   |
+=====================================+=================================+==============================+
| 0 (normal gated sleep)              | -                               | 2.6 to 3.0 mA total          |
+-------------------------------------+---------------------------------+------------------------------+
| any mask, nothing running           | -                               | +3.9 mA (PLL_SYS stays on)   |
+-------------------------------------+---------------------------------+------------------------------+
| ``KR_PWM``                          | 1000.0 Hz, 50 % (8 runs)        | +0.7 mA                      |
+-------------------------------------+---------------------------------+------------------------------+
| ``KR_PIO``                          | 1000 Hz square wave             | +1.6 mA (one run +3.3 mA)    |
+-------------------------------------+---------------------------------+------------------------------+
| ``KR_UART1``                        | 16 of 16 bytes intact           | +0.15 mA                     |
+-------------------------------------+---------------------------------+------------------------------+
| ``KR_ADC``                          | 6000 of 6000 samples valid      | +5 mA (PLL_USB for clk_adc)  |
+-------------------------------------+---------------------------------+------------------------------+
| ``KR_UART0``                        | 8 of 8 wake bytes intact        | none beyond the PLL          |
+-------------------------------------+---------------------------------+------------------------------+

A kept peripheral makes the sleep a 7 to 12 mA one, so a timed wake loop (sleep, do the work, sleep) is often cheaper than a continuous
peripheral.  On the RP2350's RISC-V cores the library does not stop the PLLs at all, so the mask only keeps the peripheral clocks.  Not
measured on the RP2040.  The mask has no effect on the CYW43 handling: the driver's lock is held around every sleep either way.

Deep sleep (RP2350)
-------------------

``deepSleepFor(ms)`` and ``deepSleepUntilPin(pin, mode)`` wrap the SDK's switched-core power-down ("Pstate"): the cores, the clocks, the
peripherals and all SRAM except the retained variables are powered off, the on-chip regulator goes into its low-power mode, and the board
draws **0.2 mA** (Pico 2 W, 3.7 V) against 3.7 mA in dormant and 2.5 to 4.7 mA in light sleep.  The price is that the wake is a **reboot**:
the call does not return on success, ``setup()`` runs again, every variable is reset (except the retained ones), the peripherals, pins and
USB are set up again, ``millis()`` restarts at 0, and a WiFi connection is gone.  A wake takes about 0.12 s to the first line of
output and costs about 0.6 mC in all.  Compared with the light sleeps, deep sleep uses less once the sleep is longer than about 0.2 to 0.3 s;
at 10 s the average is 0.3 mA.

.. code:: cpp

    uint32_t LOWPOWER_RETAINED(wakeCount);   // zero after power on or reset, kept through deep sleep

    void setup() {
      wakeCount++;
      if (LowPower.wokeFromDeepSleep()) { /* LowPower.deepSleepWakeReason() is WAKE_TIMER or WAKE_PIN */ }
      // ... do the work ...
      LowPower.deepSleepFor(10000);          // does not return
    }

* ``deepSleepFor`` wakes from the always-on timer (at least 10 ms).  ``deepSleepUntilPin`` wakes on a LOW or HIGH level or a RISING or FALLING edge on one pin
  (a pin the sketch never configured is set up as an input first, a pull the sketch set is kept; on the RP2350 an untouched pad is isolated and would never wake);
  there is no pin-or-timeout form, the SDK offers one or the other.
* ``wokeFromDeepSleep()`` is true only in the boot that follows a deep sleep (not after power on, the RUN pin, the watchdog or an upload), and
  ``deepSleepWakeReason()`` says whether the timer or the pin woke the chip.
* ``LOWPOWER_RETAINED(name)`` puts a variable in a linker section (``.persistent_data``) that the start-up code leaves alone and that the SDK zeroes on
  every reset that is not a wake from deep sleep.  It must not have an initializer (other than zero).  The section is placed after ``.bss``; a sketch without a retained variable
  pays nothing (the heap starts right after ``.bss``).  Its SRAM bank stays powered during the sleep (0.05 mA more: 0.15 mA without any retained variable, 0.2 mA with);
  keep it small: it has to end inside the first 256 KB bank of SRAM (the link fails otherwise).  On the RP2040 the macro is an ordinary variable and
  the deep sleep calls return ``PICO_ERROR_NOT_PERMITTED``.
* Pins are parked as in dormant and not restored (the chip reboots); configure them again in ``setup()``.  On a Pico W / Pico 2 W the WiFi chip is switched off
  first (WL_ON low; left on, a connected chip would keep drawing about 47 mA through the sleep) with the driver's lock held, as in ``sleepFor``, so that the
  driver's worker (WiFi or Bluetooth) cannot touch the chip while it is switched off.  The connection and a Bluetooth stack are gone after the wake: start them again in ``setup()``.
* Use an external pull-down for a RISING wake pin (RP2350-E9, above).
* In a build with FreeRTOS both calls return ``PICO_ERROR_NOT_PERMITTED`` like every other sleep call.
* Plain Pico 2 (no CYW43): 100 timed wakes of 1 s and 30 pin wakes on GP15 (RISING, a pin the sketch never configured) all gave the right reason, a clean ``setup()`` and the
  retained counter 1, 2, 3 ... without a gap; a RUN reset and a power cycle give a cold boot (counter 0).  Floor 0.175 mA without and 0.225 mA with a retained variable
  (Pico 2 W: 0.15 and 0.20 mA).
* The first 10 KB of SRAM are not usable for retained data.  The OTA stub (``ota.o``) that the core links at the start of every RP2350 image is a copy-to-RAM program: at every boot, power-up and wake
  from deep sleep alike, it copies itself to SRAM and clears its bss before it starts the sketch.  On the rp2350 Arm build that is 0x20000110 to 0x200028DC (measured: a probe
  filled before the sleep changed in exactly that range after every wake, and not at all in an image linked without the stub); the stub tables give 0x20002CB4 for the RISC-V build.
  A retained variable there would read garbage after the wake (on a plain Pico 2 data at 0x200027f0 was lost, at 0x20002bf0 and higher it survived; the Pico 2 W builds put it higher
  because of the CYW43's ``.bss`` and were never affected).  So when a sketch has retained variables the linker script places ``.persistent_data`` at 0x20003000 or above; the gap between the end
  of ``.bss`` and 0x20003000 (at most about 3 KB, and only for a very small sketch) is then not heap.  If ``ota.o`` is ever rebuilt larger,
  the floor in ``lib/rp2350*/memmap_default.ld`` has to be checked.  The same range cannot hold anything that must survive a reboot, such as an ``.uninitialized_data`` variable (the section
  starts at 0x20000110).  The POWMAN scratch registers and everything above the stub survive a deep sleep.

Time across deep sleep (RP2350)
-------------------------------

``LowPower.setTime(epoch)`` starts the always-on (POWMAN) timer at that Unix time and sets the system time; the timer keeps counting through deep sleep, dormant and
light sleep, and when it is running at start-up the system time (``time()``, ``gettimeofday()``) is taken from it.  So ``time()`` is right after every wake without a
resync; ``timeValid()`` says whether the timer is running.  Set it once from NTP, a host or the user (the ``DeepSleepClock`` example uses the compile time).

* The timer is lost by a power-up and by a RUN-pin reset, not by deep sleep or the watchdog.  ``millis()`` restarts at 0 after a deep sleep, ``time()`` does not.
* Awake and in light sleep it counts from the crystal (the library keeps the timer's clock running in ``sleepFor`` and ``sleepUntilPin``; without that it stopped).  In
  deep sleep and dormant the crystal is off and it counts from the on-chip low-power oscillator, which on the chip measured ran **0.54 % slow** (10 s sleeps for 30 minutes:
  1793.0 s counted for 1802.6 s of host time; 20 s per hour, 8 minutes per day).  Calibrating the oscillator against the crystal while awake does not see it (it reads the
  same 31.2 kHz the SDK takes from the chip's OTP: the oscillator is slower in deep sleep than awake), so use NTP now and then, or measure the error of your board once and set
  ``LowPower.setTimeTrimPpm(5400)`` (positive = counts faster; kept in POWMAN scratch register 5, lost at power-up): with that trim the same 10 s deep sleeps ran within
  **+108 ppm** over 15 minutes (about 9 s per day).  The trim is per chip (another Pico 2 W ran 0.38 % slow with the same value) and applies to **deep sleep only**: awake and in light sleep the timer counts from the
  crystal (not trimmed), and in dormant the same oscillator ran about **0.1 to 0.3 % fast** with the plain OTP value (1500 and 2200 ppm on two days; 6900 ppm fast with the deep-sleep trim), so dormant always uses the
  untrimmed value.  That rate changes with temperature and there is no separate dormant trim: for timekeeping use deep or light sleep, or resync the time (NTP, an RTC) after dormant
  (1500 to 2200 ppm is 5 to 8 s per hour spent in dormant).
* ``dormantFor`` and ``dormantUntilPin`` keep the time too (the system timer stops in dormant; the library resyncs it from the always-on timer on return and runs the
  always-on timer from the low-power oscillator while dormant): 20 cycles each on the plain Pico 2 agreed with the host to better than 0.2 s.
* The deep-sleep floor is the same with the timer running (0.175 mA).  On the RP2040 ``setTime()`` does nothing (``timeValid()`` is false); it has an RTC but this library does not use it yet.

Supply voltage
--------------

``LowPower.readVsys()`` returns VSYS in volts from GP29 (VSYS / 3 through a 200 k + 100 k divider on the Pico and Pico 2, always connected, about 12 uA at 3.7 V).  It averages 16
readings.  Measured on a Pico 2 against a PPK2 source from 3.2 to 5.0 V the untrimmed value is 50 to 69 mV (1.4 %) low and the noise 3 to 7 mV; with
``LowPower.setVsysTrim(0.995, 0.077)`` the error is within 12 mV.  The reading stays proportional down to a 1.6 V supply (the 3.3 V rail holds; the board quit between 1.5 and 1.4 V and
came back cleanly with a power-on reset, the RP2350's brown-out detector never fired because it watches the 1.1 V core, not VSYS), so the end of a battery has to be decided in software.
The ADC costs 0.15 mA while enabled (it stays on after the call).  On a Pico W / Pico 2 W GP29 is shared with the WiFi chip:
``readVsys()`` returns NAN while the chip is powered (call ``WiFi.powerOff()`` first); with the chip off it sets GP29 up as an ADC pin on every call and drives the chip's select line (GPIO25) high for the
conversions, restoring it afterwards (without that the Pico W reads 0.04 V instead of VSYS).  The first conversions after the ADC is switched on read about 3 % low (a 67 k source), the call waits for that.

Examples
--------

* ``SleepForSerial1`` - sleep 5 seconds in a loop, reporting over ``Serial1``.
* ``SleepPinOrTimeout`` - light sleep until a pin or a timeout.
* ``DormantUntilPin`` - go dormant until GP15 is pulled low.
* ``KeepRunningPWM`` - keep a PWM output running during a ``sleepFor``.
* ``DeepSleepCounter`` - RP2350: deep sleep for 10 seconds in a loop, a retained wake counter.
* ``DeepSleepUntilPin`` - RP2350: deep sleep until GP15 is pulled low.
* ``DeepSleepClock`` - RP2350: wall-clock time that survives deep sleep.
