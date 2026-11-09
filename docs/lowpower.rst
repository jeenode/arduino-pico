Low Power (Sleep and Dormant)
=============================

The ``LowPower`` library puts the RP2040 or RP2350 into a low power state
and wakes it up again from a timer or a GPIO.  It is a thin Arduino wrapper
over the Pico SDK ``pico_low_power`` library, plus the extra work the Arduino
core needs after a wake (restoring the CPU clock, restoring pin state).

.. code:: cpp

    #include <LowPower.h>

    void setup() {
        Serial1.begin(115200);
        pinMode(15, INPUT_PULLUP);
    }

    void loop() {
        Serial1.println("sleeping 5 seconds");
        Serial1.flush();
        LowPower.sleepFor(5000);          // light sleep, timer wake

        Serial1.println("dormant until GP15 goes low");
        Serial1.flush();
        LowPower.dormantUntilPin(15, FALLING);  // oscillators off, pin wake
    }

Modes
-----

**Sleep** gates most clocks.  The SDK's sleep leaves the oscillators and PLLs running, which is where
most of the sleep current goes (datasheet: "The PLLs are not affected by SLEEP mode").  On RP2040 and on the
RP2350's ARM cores the library therefore runs the system from the crystal and stops the system PLL and the ring oscillator
around ``sleepFor`` and both forms of ``sleepUntilPin``; this costs about 1.3 ms of extra wake time.
What else is stopped depends on USB, and is decided at every sleep:

* ``NO_USB`` build: the USB PLL, the USB clock and the ADC clock are stopped too, and the USB pins
  D+ / D- are pulled down for the length of the sleep (about 0.8 mA board current on a Pico; see
  Current below for why).
* USB build with a host using USB (the bus is connected and not suspended): the USB PLL and clock keep
  running, so the ``Serial`` port stays connected and the host sees no disconnect (about 3.1 mA).
* USB build with no host (nothing plugged in, device detached, suspended host): stopped as with ``NO_USB``.
  All clocks are back before the sleep call returns, so a cable plugged in during a sleep is enumerated
  normally after the wake.
* With Adafruit TinyUSB (``USE_TINYUSB``) USB is always treated as in use.  The RP2350's RISC-V cores use
  the SDK's sleep unchanged.

Light sleep keeps the system timer, and therefore ``millis()``, running; only dormant stops it (see
``millis()`` below).

**Dormant** stops the oscillators and PLLs.  It saves the most power but
can only be woken by a GPIO (or, with a suitable low-frequency clock, a timer),
and takes longer to restart.  USB is shut down while dormant, so a USB host
will see the board disconnect and reconnect.

Every function returns ``0`` on success or a negative Pico SDK error code
(``PICO_ERROR_*`` in ``pico/error.h``).

API
---

.. code:: cpp

    int sleepFor(uint32_t ms);
    int sleepUntilPin(pin_size_t pin, PinStatus mode);    // LOW, HIGH, RISING, FALLING
    int sleepUntilPin(pin_size_t pin, PinStatus mode, uint32_t timeoutMs);  // pin or timeout
    int dormantUntilPin(pin_size_t pin, PinStatus mode);  // LOW, HIGH, RISING, FALLING
    int dormantFor(uint32_t ms);
    void keepPins(uint64_t mask);
    void setPinRestore(bool restore);
    uint32_t lastSleepMs() const;
    WakeReason wakeReason() const;   // LowPower.WAKE_NONE, LowPower.WAKE_PIN, LowPower.WAKE_TIMER

``CHANGE`` is not supported as a wake condition and returns
``PICO_ERROR_INVALID_ARG``.

Support by chip
---------------

+-------------------------+---------------------------+-----------------------------------------+
| Call                    | RP2040 (Pico, Pico W)     | RP2350 (Pico 2, Pico 2 W)               |
+=========================+===========================+=========================================+
| ``sleepFor``            | yes (timer wake)          | yes (timer wake)                        |
+-------------------------+---------------------------+-----------------------------------------+
| ``sleepUntilPin``       | yes (GPIO wake)           | yes (GPIO wake)                         |
+-------------------------+---------------------------+-----------------------------------------+
| ``sleepUntilPin`` with  | yes (GPIO or timer)       | yes (GPIO or timer)                     |
| a timeout               |                           |                                         |
+-------------------------+---------------------------+-----------------------------------------+
| ``dormantUntilPin``     | yes (GPIO wake)           | yes (GPIO wake)                         |
+-------------------------+---------------------------+-----------------------------------------+
| ``dormantFor``          | only with an external     | yes, using the AON timer on the         |
|                         | clock, see below          | low-power oscillator                    |
+-------------------------+---------------------------+-----------------------------------------+
| Minimum ``dormantFor``  | 2000 ms                   | 10 ms                                   |
+-------------------------+---------------------------+-----------------------------------------+
| RP2350 "Pstate"         | n/a                       | provided by the SDK, **not** wrapped    |
|                         |                           | by this library yet                     |
+-------------------------+---------------------------+-----------------------------------------+

Shorter ``dormantFor`` requests return ``PICO_ERROR_INVALID_ARG``.

**RP2040 dormantFor:** the RP2040 has no always-on timer that survives dormant
with the oscillators stopped.  ``dormantFor`` therefore needs a 32 kHz-class
clock fed into the chip and registered with the SDK function
``low_power_set_external_clock_source()`` (GPIO 20 or 22) before the call;
without it ``dormantFor`` returns ``PICO_ERROR_PRECONDITION_NOT_MET``.
``dormantUntilPin`` needs no extra hardware.

Pins
----

Before going dormant, every GPIO is put into its lowest-leakage state
(pulls off, input off, output disconnected), except:

* the wake pin,
* any pin currently routed to a UART (so a UART TX line does not float),
* any pin currently routed to I2C (parking them leaves the I2C controller unusable after
  wake, every transfer times out until ``Wire.end()``/``begin()``; an idle-high bus
  draws no current through the pull-ups), and
* pins you list with ``keepPins(mask)`` (bit *n* set = leave GPIO *n* alone).

The state of the parked pins is saved first and **restored after wake**, so
outputs, pull-ups and peripherals muxed to pins come back as they were.  Call
``setPinRestore(false)`` if you want them to stay parked after wake.  Light
sleep never parks pins.

The wake pin must be configured by you before the call, for example
``pinMode(15, INPUT_PULLUP)`` for a button to ground.

**Waking on a UART RX pin.**  A pin that is routed to a UART can be the wake pin, for
example ``dormantUntilPin(1, FALLING)`` with ``Serial1`` on GP1, so that the first byte
sent by another device wakes the board (the start bit is the falling edge).  The byte that
causes the wake is **lost**: the UART clock is stopped while dormant and the byte is
over before the clocks are back.  Send a dummy byte first (for example ``x``), wait for
the wake (about 75 ms measured on a Pico, see the latency table) and only then send the real data; the
following bytes are received normally.
With ``sleepUntilPin`` the wake byte is **not** lost: it is received normally and is waiting in
the RX buffer when the call returns (1 byte after each of 20 wakes on a Pico).

Waking on a pin or a timeout
----------------------------

``sleepUntilPin(pin, mode, timeoutMs)`` is light sleep that ends at whichever comes first: the pin
condition or ``timeoutMs`` milliseconds, for example a radio interrupt with a 60 second heartbeat.
``LowPower.wakeReason()`` tells which one it was:

.. code:: cpp

    int rc = LowPower.sleepUntilPin(15, FALLING, 60000);
    if (rc == 0 && LowPower.wakeReason() == LowPower.WAKE_PIN) {
        // the button / radio IRQ
    } else {
        // 60 s heartbeat
    }

* ``WAKE_PIN`` and ``WAKE_TIMER`` are what ``sleepUntilPin(pin, mode)`` / ``dormantUntilPin`` and
  ``sleepFor`` / ``dormantFor`` report; ``WAKE_NONE`` before the first call and after a call that failed.
  The names belong to the library object (``LowPower.WAKE_PIN``, or ``LowPowerClass::WAKE_PIN``), so they
  cannot clash with a constant of your own with the same name.
* With ``LOW`` or ``HIGH`` and the pin already in that state the call returns at once with ``WAKE_PIN``.
* If the pin condition and the timeout happen at the same moment either reason may be reported (never both).
* ``timeoutMs`` of 0 returns ``PICO_ERROR_INVALID_ARG`` (use ``sleepUntilPin(pin, mode)`` to wait without a limit).
* ``millis()`` includes the sleep and ``lastSleepMs()`` is the measured duration, rounded to milliseconds
  (as for ``sleepUntilPin(pin, mode)``).
* Only the wake pin and the timer interrupt are enabled while asleep, as with the other light sleep calls.
* Pins you set up with ``attachInterrupt()`` keep working after every sleep and dormant call (the library
  does not touch the core's GPIO interrupt handler).  Do not use the wake pin itself for ``attachInterrupt()``
  as well.
  Dormant with a timeout is not available on RP2040 without an external clock, see above.

Time: millis() and lastSleepMs()
--------------------------------

``millis()`` and ``micros()`` count with the system timer.  The timer **stops in dormant**: time spent in
``dormantUntilPin`` or ``dormantFor`` does **not** appear in ``millis()``.  Light sleep (``sleepFor`` and
both forms of ``sleepUntilPin``) keeps the timer running, so ``millis()`` includes it.  (Measured on a Pico:
``millis()`` advanced about 6-10 ms across ``dormantUntilPin`` calls that lasted 1-5 seconds.)  If you
need real elapsed time across dormant, add ``lastSleepMs()`` to your own bookkeeping where it is known.

``lastSleepMs()`` describes the most recent call:

+-----------------------------------------------+------------------------------------------+
| Last call                                     | ``lastSleepMs()``                        |
+===============================================+==========================================+
| ``sleepFor(ms)`` / ``dormantFor(ms)``, success| ``ms`` - the *requested* duration, not a |
|                                               | measurement                              |
+-----------------------------------------------+------------------------------------------+
| ``sleepUntilPin``, either form, success       | the *measured* duration in milliseconds  |
|                                               | (the timer runs)                         |
+-----------------------------------------------+------------------------------------------+
| ``dormantUntilPin``, success                  | ``LowPower.SLEEP_UNKNOWN``               |
|                                               | (``0xFFFFFFFF``): there is no time       |
|                                               | reference while dormant                  |
+-----------------------------------------------+------------------------------------------+
| any call that returned an error, or           | ``0``                                    |
| ``sleepFor(0)``                               |                                          |
+-----------------------------------------------+------------------------------------------+
| no call yet                                   | ``0``                                    |
+-----------------------------------------------+------------------------------------------+

Wake latency
------------

Measured on a Raspberry Pi Pico (RP2040) at 133 MHz, wake byte arriving on a UART RX
pin, time from the start of the wake byte to the first output of the sketch:

+------------------------------+-----------------------+---------------------------------------------+
| Call                         | Wake latency          | Notes                                       |
+==============================+=======================+=============================================+
| ``dormantUntilPin``          | about 73 ms           | the crystal oscillator has to restart; this |
|                              | (72-76 ms)            | is inherent, not caused by USB or by the    |
|                              |                       | library (about 0.5 ms of the time is code)  |
+------------------------------+-----------------------+---------------------------------------------+
| ``sleepUntilPin``            | about 4-6 ms          | with the SDK sleep (oscillators running);   |
|                              |                       | about 1.3 ms more on RP2040 (see Modes)     |
+------------------------------+-----------------------+---------------------------------------------+
| ``sleepFor``                 | timer wake            | returns at the requested time; about 1.3 ms |
|                              |                       | later on RP2040                             |
+------------------------------+-----------------------+---------------------------------------------+
| ``sleepUntilPin`` with a     | pin: about 4-6 ms     | the timeout is accurate to about 1 ms       |
| timeout                      | timer: at the timeout | (measured 0-1 ms late)                      |
+------------------------------+-----------------------+---------------------------------------------+

Use ``sleepUntilPin`` when the wake has to be fast, and dormant when the lowest
power matters more than a wake in tens of milliseconds.  The crystal start time may
differ between boards.

Current
-------

Whole-board current at VSYS of a Raspberry Pi Pico (RP2040, 133 MHz, no USB stack, nothing else
attached except the debug probe wires), measured with an INA226 on a 0.1 ohm shunt, mean of 31 to 45
results of 2.1 s each, uncalibrated (+-0.1 mA offset, 25 uA steps); the board's regulator is included,
so the chip alone uses less:

+-----------------------------------------------+--------------+
| Mode                                          | Board current|
+===============================================+==============+
| running (delay loop)                          | 19.7 mA      |
+-----------------------------------------------+--------------+
| ``sleepFor`` / ``sleepUntilPin``, SDK sleep   | 7.9 - 8.0 mA |
| (PLLs and oscillators left running)           |              |
+-----------------------------------------------+--------------+
| ``sleepFor`` / ``sleepUntilPin``, clocks      | 0.78 - 0.85  |
| stopped, USB pins pulled down (RP2040,        | mA           |
| ``NO_USB``)                                   |              |
+-----------------------------------------------+--------------+
| ``sleepFor`` / ``sleepUntilPin``, system      | 3.1 mA       |
| clocks stopped, USB clock kept (RP2040 with   |              |
| USB stack and a host using USB)               |              |
+-----------------------------------------------+--------------+
| ``sleepFor`` / ``sleepUntilPin``, USB build   | 1.1 mA       |
| with no host (cable unplugged)                |              |
+-----------------------------------------------+--------------+
| ``dormantUntilPin``                           | 0.76 - 0.79  |
|                                               | mA           |
+-----------------------------------------------+--------------+

The INA226's zero reading was measured later at -0.075 mA (-7.5 uV on the shunt): add 0.075 mA to the figures in this table.  The PPK2, used alongside
later, reads 0.3 to 0.7 mA lower on these pulsed loads (the board's buck converter); the INA226 is the one used for averages.

The 3.1 mA figure is for the clock setup used while a host is using USB; it was measured with the
USB stack running and the cable unplugged, with the USB clocks forced on.  That the connection really
stays up with a host attached was checked separately (20 ``sleepFor`` and 20 ``sleepUntilPin`` cycles, no
disconnect, a ``Serial`` line after every wake).  A USB build with no host connected stops the same clocks
as ``NO_USB`` (checked by register readout); measured with the cable unplugged it drew about 1.1 mA
(``sleepFor``, ``sleepUntilPin`` with and without a timeout all within 0.01 mA).
The USB pins matter: the USB PHY's receivers sit on the D+ / D- pins, and with nothing driving them
(no cable, or a cable to nothing) they float and draw about 0.25 mA each.  In a ``NO_USB`` build nothing
else uses the PHY, so the library switches on the PHY's own pull-downs for the length of each light sleep
(``sleepFor``, ``sleepUntilPin``) and restores the registers after the wake: 1.35 mA became 0.85 mA
(``sleepFor``) and 0.78 mA (``sleepUntilPin``; the two differ by about 0.07 mA).  The pull-downs are
host-like (a host the board is plugged into sees nothing attach).  They are not switched on if the sketch
has enabled the USB controller's D+ pull-up itself.  A USB build without a host leaves the USB pins to the
USB stack (its D+ pull-up is on, which is why it stands at 1.1 mA) and is not changed.
The RP2350 (Pico 2 W, ARM cores) is in the next subsection.

On a Pico W, and light sleep or dormant?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The same sketches on a Raspberry Pi Pico W (RP2040 plus CYW43439; ``WiFi`` never started, GPIO23 / WL_ON
reads low during sleep), supplied with 3.7 V at VSYS by a Nordic PPK2 through the INA226, 133 MHz.  Board
current, mean of 36 to 37 INA226 results of 2.1 s (14 for running; the PPK2 mean over the same state in brackets):

+-----------------------------------------------+--------------------+
| Mode                                          | Board current      |
+===============================================+====================+
| running (delay loop)                          | 26.1 mA (25.8)     |
+-----------------------------------------------+--------------------+
| ``sleepFor``, ``NO_USB``                      | 1.49 mA (0.95)     |
+-----------------------------------------------+--------------------+
| ``sleepUntilPin``, with or without timeout,   | 1.41 mA (0.90)     |
| ``NO_USB``                                    |                    |
+-----------------------------------------------+--------------------+
| ``sleepFor`` / ``sleepUntilPin``, USB build   | 1.78 mA (1.10)     |
| with no host                                  |                    |
+-----------------------------------------------+--------------------+
| ``dormantUntilPin`` (``NO_USB`` or USB build) | 1.19 mA (0.83)     |
+-----------------------------------------------+--------------------+

A switching regulator takes more current from a lower supply for the same power (running: 100 mW here
and on the Pico at 5.1 V), so these are not comparable with the Pico table above in mA, only in mW.
The two meters agree at 26 mA (1 %) but not around 1 mA.  The board's buck converter takes pulses of
about 1.2 uC (0.1 ms, roughly 600 per second) on top of a baseline of about 80 uA when the chip sleeps; the PPK2 (with its range
switching and spike filter) reads 0.3 to 0.7 mA lower than the INA226, which integrates continuously, so
the INA226 means are the ones to use for averages.  Neither meter was zero-calibrated against a known current.

Per cycle, from 100 kS/s PPK2 captures of 12 cycles each (``NO_USB``): entering light sleep and leaving it cost
about 4 uC each, with the chip active for about 0.3 ms before the call returns; ``dormantUntilPin`` costs the
same plus about 20 uC for the 73 ms crystal restart (about 0.25 mA above the sleep level), about 29 uC in
all against about 10 uC for ``sleepUntilPin``.  The dormant floor is 0.07 mA (PPK2) to 0.22 mA (INA226) lower
than the light-sleep floor, so dormant uses less average current once the sleep is longer than about
0.1 to 0.3 s (18 uC divided by the floor difference), and light sleep when it is shorter.  At sleeps of
a second or more the saving is 0.07 to 0.2 mA (5 to 15 % of the sleep current); the price is a wake of
about 73 ms instead of 4 to 6 ms.  Awake work after the wake is the same for both and is not included.

RP2350 (Pico 2 W)
~~~~~~~~~~~~~~~~~

Board current on a Pico 2 W (150 MHz, WiFi never started, 3.7 V supply, INA226 mean; USB build with no host in
brackets where it differs).  The RP2350's light sleep used to leave both PLLs and the ring oscillator running; the
clock gating described above now applies to it (the board went from 7.4 / 8.4 mA to the values below):

+-----------------------------------------------+------------------+
| Mode                                          | Board current    |
+===============================================+==================+
| running (delay loop)                          | 17.0 mA          |
+-----------------------------------------------+------------------+
| ``sleepFor``                                  | 4.3 (2.9) mA     |
+-----------------------------------------------+------------------+
| ``sleepUntilPin`` (2 arguments)               | 4.7 mA           |
+-----------------------------------------------+------------------+
| ``sleepUntilPin`` with timeout                | 2.5 (2.9) mA     |
+-----------------------------------------------+------------------+
| ``dormantUntilPin``, ``dormantFor``           | 3.6 to 3.9 mA    |
+-----------------------------------------------+------------------+
| switched-core power-down (SDK P-state)        | 0.15 mA          |
+-----------------------------------------------+------------------+

Two things inflated these figures: the first light sleep after a reset costs about 1.7 mA more than the following ones in a sketch that has started both
I2C controllers (the test fixture's loopback jumpers); any sleep clears it, a 5 ms ``sleepFor`` before the real one is enough.  And the paths differ
(a bare sketch, after one warm-up sleep: ``sleepFor`` 2.65 mA, ``sleepUntilPin`` with timeout 2.56 mA, without timeout 3.3 to 4.5 mA, varying from run to
run); the cause of that is not known.  It is not the debug port (same floors after a power cycle without any SWD access), not core 1, not input
buffers of unused pins, and the registers read in sleep are identical.  Dormant (3.9 mA) is no lower than the light sleeps: with the chip held in reset the
board draws 0.5 mA, and dormant adds 3.4 mA to that (the SDK's own table lists 3.3 to 3.7 mA for a Pico 2 as well; the on-chip regulator only enters its
low-power mode in a power-state change, there is no software switch for it).
A wake from dormant costs about 24 uC more than a wake from light sleep (the crystal restart), light sleep wakes cost
nothing measurable.  So on the RP2350 prefer light sleep (``sleepUntilPin`` with a timeout was the lowest) unless the
sleep is long enough for the SDK's switched-core power-down, which the library does not wrap: ``low_power_pstate_until_gpio_pin_state()``
and ``low_power_pstate_until_aon_timer()`` put the regulator into low-power mode and drop the board to 0.15 mA, but
the wake is a reboot (``setup()`` runs again, 128 ms from the wake byte to the first line of output) and only
data declared persistent survives.  Its wake costs about 2 mC (one boot at 16 mA), so it pays off once the sleep is
longer than roughly a second compared with the best light sleep.

RP2350-E9: a GPIO input with its internal pull-down enabled can latch high.  Measured on this chip: a pin driven
high and then released with ``INPUT_PULLDOWN`` still reads high 500 ms later.  The library's pin parking (input buffer
off, pulls off) clears it, so parked pins are not affected, and neither is the wake pin of the examples (an idle-high
line such as a UART RX).  A wake pin that relies on the internal pull-down and a RISING edge (a button to 3.3 V) can
stay high after the button is released and then not wake again: use an external pull-down, or wake on FALLING with
a pull-up.  Driving the pin low for a millisecond before enabling the pull-down also avoids it.

WiFi on a Pico W
~~~~~~~~~~~~~~~~

A station that is connected before the sleep is still connected after it, with no ``WiFi.begin()``,
after ``sleepFor``, ``sleepUntilPin`` (with and without timeout) and ``dormantUntilPin``: 20 cycles each
on a Pico W (``NO_USB``, 2 s sleeps), ``WiFi.status()`` was connected right after every wake and a ping to
the gateway and a DNS lookup worked within 2 to 19 ms.  The library keeps the CYW43 pins (GPIO23 WL_ON, 24, 25,
29) through ``dormantUntilPin`` while the chip is powered, and ``sleepFor`` holds the WiFi driver's lock
until the clocks are back.  Without that, ``sleepFor`` hung in the driver and dormant lost the chip (no ping,
no reconnect until a reset).

The WiFi chip is the cost: connected, the board draws about 48 mA in light sleep and in dormant, 45 to 46 mA
with ``WiFi.aggressiveLowPowerMode()`` and about 69 mA running (INA226, 3.7 V supply), so sleeping with the
connection up saves little.  ``WiFi.end()`` does not power the chip off (it leaves the network; the board still
draws about 19 mA).  With WiFi never started, or with WL_ON driven low
(``gpio_put(23, 0)`` after ``WiFi.end()``; ``cyw43_arch_deinit()`` crashed in the PIO shutdown on this core), the board
draws the same 1.3 to 1.6 mA as without WiFi.  After driving WL_ON low the driver's state is stale: reset the board
(or start over) to use WiFi again.

Pico 2 W and WiFi
~~~~~~~~~~~~~~~~~

The Pico 2 W (RP2350 plus CYW43) behaves as the Pico W above: with the same two library changes a connected station survives
``sleepFor``, ``sleepUntilPin`` (both forms), ``dormantUntilPin`` and ``dormantFor`` (20 cycles each, ``WiFi.status()`` connected right after
every wake, ping and DNS working within 2 to 22 ms).  Board current (INA226, 3.7 V): connected and asleep about 50 mA, 39 to 43 mA with
``WiFi.aggressiveLowPowerMode()``, running connected about 60 mA, ``WiFi.end()`` only about 21 mA (the chip stays powered), WiFi never started or
WL_ON driven low before dormant 3.6 to 3.9 mA (the RP2350 figure above).

Calling ``cyw43_arch_deinit()`` to power the chip off HardFaults in the unmodified core: ``cyw43_driver_deinit()`` clears the driver's context
pointer and the core's lwIP wrapper (``ethernet_arch_lwip_begin()``) then takes the lock through it while the lwIP workers are being removed.  Locking the
arch context directly in that function fixes it (``cyw43_arch_deinit()`` returns, WiFi can be started again; 20 cycles of connect, deinit,
``dormantFor`` 2 s, ``init_cyw43_wifi()``, connect).  That change is not part of this library.

Peripherals after wake
----------------------

Peripherals configured before the call keep working afterwards **without calling
``begin()`` again**.  Tested on one Raspberry Pi Pico (RP2040) at 133 MHz (dormant also at
125 and 200 MHz), 20 or more wakes per cell.  The RP2350 was tested the same way on a Pico 2 W (ARM cores at 150, 200
and 300 MHz; RISC-V cores 10 wakes of ``sleepFor`` and ``dormantUntilPin``): all five loopbacks, the PIO tone and
PWM, ``attachInterrupt`` and clocks as before the sleep, USB build and ``NO_USB``.

+--------------------------------+--------------+------------------+------------------+
| Peripheral                     | sleepFor     | sleepUntilPin    | dormantUntilPin  |
+================================+==============+==================+==================+
| UART (``Serial1``/``Serial2``) | OK           | OK               | OK               |
+--------------------------------+--------------+------------------+------------------+
| SPI                            | OK           | OK               | OK               |
+--------------------------------+--------------+------------------+------------------+
| I2C (``Wire``/``Wire1``)       | OK           | OK               | OK               |
+--------------------------------+--------------+------------------+------------------+
| ``SerialPIO``                  | OK           | OK               | OK               |
+--------------------------------+--------------+------------------+------------------+
| PIO program (``tone()``)       | OK           | OK               | OK               |
+--------------------------------+--------------+------------------+------------------+
| PWM (``analogWrite``)          | OK           | OK               | OK               |
+--------------------------------+--------------+------------------+------------------+
| ADC (``analogReadTemp``)       | OK           | OK               | OK               |
+--------------------------------+--------------+------------------+------------------+

* **PWM** (``analogWrite``) stops while dormant and resumes at the **same frequency
  and duty cycle** after wake, with or without ``keepPins`` for the PWM pin (measured:
  1 kHz, 50 %, within 0.1 % after each of 20 dormant cycles).  A PIO ``tone()`` likewise
  keeps its frequency at 125, 133 and 200 MHz.
* **Dormant leaves unparked** the wake pin, UART pins and I2C pins (see Pins);
  ``keepPins()`` covers any other pin whose state must hold while dormant.
* **USB** re-enumerates after dormant, see USB Serial below.
* **Sketches using both cores** (``setup1()``/``loop1()``) work: dormant waits for
  its wake event, and the second core carries on counting after wake.

USB Serial
----------

* ``sleepFor`` does not affect USB: the ``Serial`` port stays connected and no
  output is lost.
* **Dormant disconnects USB.**  The board detaches from the host when it goes
  dormant and re-attaches by itself about **0.6 seconds after the wake**.  No
  user code is needed to bring the USB ``Serial`` port back, but:

  * the serial port disappears from the host (a terminal program that keeps the port
    open loses the connection and has to reconnect);
  * output written just before going dormant, and anything printed in the first
    second or so after the wake, can be lost: ``Serial`` is not connected right after
    wake, and even after the host has reopened the port the first fraction of a
    second of output may be dropped;
  * once the port is open again, USB output and input work normally.

* Do not call ``Serial.end()`` and ``Serial.begin()`` after wake: in testing this made
  the USB port fail to come back at all.  Waiting for ``Serial`` before printing
  (``while (!Serial) delay(1);``) avoids printing while the host is away but does not
  recover the lost output.
* Log anything important after a dormant wake on a UART (``Serial1``) or keep it and
  print it later.

Known issue: USB on RP2040 B0/B1
--------------------------------

Early RP2040 chips (revision B0/B1, where the SDK function ``rp2040_chip_version()``
returns 1) run an SDK workaround for a USB enumeration erratum (RP2040-E5) every time
the USB host resets the device.  In rare cases a check inside that workaround fails and
the SDK ``panic()``\ s, which stops the sketch for good.  Dormant stops the USB clocks, so the host
re-enumerates the board after every dormant wake and the problem can occur there; it was
also seen on a plain USB disconnect/connect with no low-power code.  Measured on one
board: about 1 in 100 dormant wakes and 1 in 400 plain reconnects.  Disconnecting USB
before dormant and reconnecting after the wake does not avoid it.

Sketches that do not use USB are not affected (no occurrence in 400 dormant wakes with
the USB stack disabled): battery or field sketches can be built without USB (the
``NO_USB`` build option), and that is the recommended setup where reliability matters.
Chips of revision B2 and RP2350 do not run the workaround.

Clocks
------

The SDK wakes the chip into its default clocks (125 MHz RP2040, 150 MHz
RP2350).  After a wake the library re-applies the core's ``F_CPU`` setting (and
the matching core voltage and, on RP2350 boards with PSRAM, the PSRAM timing)
so that peripherals configured before the sleep - UART, SPI, I2C, PWM, PIO -
keep working at the same clock.  Time-critical outputs may glitch for the short
moment the clock is being re-applied.

The UART clock is gated while sleeping: call ``Serial1.flush()`` before
sleeping so pending output is not cut off, and do not expect to receive data
during sleep or dormant.

Flashing and debugging while dormant
------------------------------------

A dormant chip cannot be reached over SWD: a debug probe (Picoprobe/Debug Probe, ``upload_protocol = cmsis-dap``,
OpenOCD, picotool over SWD) fails to attach and an upload fails.  Holding the board in reset
while connecting does not help.  If your sketch spends its time dormant you need one of:

* **Reset the board through its RUN pin** (pull RUN low briefly).  This works whatever the
  state of the chip, and the board then runs from the start; the debug probe can attach in the
  short time before the sketch goes dormant again, or hold the sketch back with a startup delay.
* **Use BOOTSEL**: hold the BOOTSEL button while powering up or resetting, then copy a UF2
  file.  Also available by the 1200 baud USB touch while the sketch is running (not dormant).
* **Wake it first**, with the wake source your sketch uses (for example the wake GPIO or a
  byte on the UART RX pin), and flash or attach while it is awake.  A sketch that goes dormant
  again within a second needs the wake source repeated until the upload has started.

It is good practice to keep a startup delay or a "stay awake" pin check at the start of
``setup()`` in sketches that go dormant, so a board can always be re-flashed.

Multicore
---------

If the sketch defines ``setup1()``/``loop1()`` the library idles the other core
around the call (``rp2040.idleOtherCore()``) and resumes it on wake.  The idled
core spins in a loop, so with two cores in use the chip may not reach the lowest
power states: functionally this works (dormant and wake were tested with a busy
second core), but the current draw with two cores has not been measured.
Single-core sketches are recommended for the lowest power.

Pico W / Pico 2 W
-----------------

Shut the WiFi/Bluetooth chip down (``WiFi.end()``, ``WiFi.disconnect()`` and
stop any BT use) before sleeping or going dormant.  The CYW43 is not put to
sleep by this library, and a wake with clocks changed underneath it is not
supported.

Power numbers
-------------

Measured currents for this core: **TBD**.  The Pico SDK documents rough board-level
figures in ``pico/low_power.h`` (for example roughly 1 mA dormant on a Pico and
about 3 mA on a Pico 2 from VSYS) but they depend on the board's regulator and on
what is attached.

Examples
--------

* ``SleepForSerial1`` - sleep 5 seconds in a loop, reporting over ``Serial1``.
* ``DormantUntilPin`` - go dormant until GP15 is pulled low.
