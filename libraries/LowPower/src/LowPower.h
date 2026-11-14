/*
    LowPower - sleep and dormant modes for the Raspberry Pi Pico (RP2040/RP2350)

    Thin Arduino-style wrapper over the Pico SDK `pico_low_power` library.

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.
*/

#pragma once

#include <Arduino.h>
#include <time.h>

/**
    @brief Low-power modes.  All functions return 0 on success or a (negative)
    Pico SDK error code (PICO_ERROR_*, see pico/error.h) on failure.  With the FreeRTOS SMP
    operating system option every sleep and dormant call returns PICO_ERROR_NOT_PERMITTED.
*/
/**
    @brief Declare a variable that survives LowPower.deepSleepFor() / deepSleepUntilPin():  uint32_t LOWPOWER_RETAINED(counter);

    RP2350 only.  The variable is placed in the persistent data section: it is zero after every reset that is not a wake from deep sleep
    (power on, RUN pin, watchdog, upload) and keeps its value through deep sleep.  It cannot have an initializer other than zero.  On the RP2040
    (no deep sleep) it expands to an ordinary variable.
*/
#if defined(PICO_RP2350) && PICO_RP2350
#define LOWPOWER_RETAINED(name) __attribute__((section(".persistent_data." #name))) name
#else
#define LOWPOWER_RETAINED(name) name
#endif

class LowPowerClass {
public:
    /**
        @brief What ended the last sleep, see wakeReason().  Use it as LowPower.WAKE_PIN (or LowPowerClass::WAKE_PIN),
        the names are not global so they cannot clash with a sketch's own constants.
    */
    enum WakeReason {
        WAKE_NONE,  /**< no sleep yet, or the last call failed or slept nothing */
        WAKE_PIN,   /**< the wake pin condition */
        WAKE_TIMER  /**< the timeout (or the requested time) elapsed */
    };

    /** Value returned by lastSleepMs() when the sleep duration cannot be determined. */
    static constexpr uint32_t SLEEP_UNKNOWN = 0xFFFFFFFFu;

    /**
        @brief Light sleep: clocks gated, chip wakes after a timeout.  RP2040 and RP2350.

        @param [in] ms Time to sleep in milliseconds.
        @return 0 on success, SDK error code otherwise
    */
    int sleepFor(uint32_t ms);

    /**
        @brief Light sleep until a GPIO condition.  RP2040 and RP2350.

        @param [in] pin GPIO to watch
        @param [in] mode LOW, HIGH, RISING or FALLING
        @return 0 on success, SDK error code otherwise
    */
    int sleepUntilPin(pin_size_t pin, PinStatus mode);

    /**
        @brief Light sleep until a GPIO condition or a timeout, whichever comes first.  RP2040 and RP2350.

        Use wakeReason() to find out which one it was.  If the pin is already in the wake
        state when called (LOW or HIGH level), it returns at once with WAKE_PIN.  If the pin
        condition and the timeout happen together either reason may be reported, never both.
        Unlike sleepUntilPin(pin, mode) the system timer keeps running, so millis() includes the
        sleep and lastSleepMs() is the measured duration (rounded to ms).

        @param [in] pin GPIO to watch
        @param [in] mode LOW, HIGH, RISING or FALLING
        @param [in] timeoutMs Longest time to sleep in milliseconds, must not be 0
        @return 0 on success, PICO_ERROR_INVALID_ARG for a bad pin, mode or a timeout of 0,
                other SDK error code otherwise
    */
    int sleepUntilPin(pin_size_t pin, PinStatus mode, uint32_t timeoutMs);

    /**
        @brief Dormant: oscillators stopped, wake only from a GPIO.  RP2040 and RP2350.

        All GPIOs not excluded by keepPins(), not the wake pin and not currently muxed
        to a UART or I2C are parked in a low-leakage state (input and pulls disabled, output
        disconnected) while dormant.  Their previous state is saved first and put back
        after wake, unless setPinRestore(false) was called, in which case they stay
        parked and must be reconfigured with pinMode().

        @param [in] pin GPIO to wake on (set up its pull-up/down with pinMode() beforehand)
        @param [in] mode LOW, HIGH, RISING or FALLING
        @return 0 on success, SDK error code otherwise
    */
    int dormantUntilPin(pin_size_t pin, PinStatus mode);

    /**
        @brief Dormant with a timed wake.

        RP2350: uses the AON timer clocked from the low-power oscillator.
        RP2040: requires an external 32 kHz-class clock registered with the SDK's
        low_power_set_external_clock_source(); without it returns
        PICO_ERROR_PRECONDITION_NOT_MET.  Minimum duration is
        PICO_LOW_POWER_MIN_DORMANT_TIME_MS (2000 ms RP2040, 10 ms RP2350) or
        PICO_ERROR_INVALID_ARG is returned.  Same pin-parking rules as dormantUntilPin().

        @param [in] ms Time to stay dormant in milliseconds
        @return 0 on success, SDK error code otherwise
    */
    int dormantFor(uint32_t ms);

    /**
        @brief Deep sleep (RP2350 only): switched-core power-down, the SDK's "Pstate".  Returns PICO_ERROR_NOT_PERMITTED on the RP2040.

        The core, the clocks, the peripherals and all SRAM except the retained variables (LOWPOWER_RETAINED) are powered off: the board draws about
        0.2 mA (Pico 2 W) against about 3.7 mA in dormant.  The wake is a REBOOT: the call does not return on success, setup() runs again from the
        start, variables are reset (except the retained ones), peripherals, pins and USB must be set up again, millis() restarts at 0.  On a Pico 2 W a
        powered WiFi chip is switched off (WL_ON low) first, otherwise it would keep drawing about 47 mA; the connection is lost and has to be made again.  Pins are parked as in dormant.
        A wake costs about 0.6 mC and 0.12 s (reboot), so deep sleep pays against light sleep for sleeps longer than about 0.2 s.

        deepSleepFor() wakes from the AON timer (ms must be at least 10).  deepSleepUntilPin() wakes on a GPIO condition (LOW / HIGH level, or
        RISING / FALLING / CHANGE edge; use an external pull-down with RISING, see RP2350-E9); there is no timeout form (the SDK offers either).

        @return only on an error: a negative SDK error code
    */
    int deepSleepFor(uint32_t ms);
    int deepSleepUntilPin(pin_size_t pin, PinStatus mode);

    /** @brief true if this boot is the wake from a deepSleepFor() / deepSleepUntilPin() (not a power on, reset or watchdog reboot).  Always false on the RP2040. */
    bool wokeFromDeepSleep() const;

    /**
        @brief Wall-clock time that survives deep sleep (RP2350).

        setTime() starts the always-on (POWMAN) timer at `epoch` and sets the system time (time(), gettimeofday()).  The timer keeps counting in deep sleep, dormant and across
        RUN-pin resets; when it is running at startup the system time is set from it, so time() is right after every wake without a resync.  In deep sleep and dormant it
        runs from the on-chip low-power oscillator (about 32 kHz, tolerance of tens of percent unless calibrated), awake from the crystal.
    */
    void setTime(time_t epoch);
    /** @return true if the always-on timer is running (setTime() was called since the last power-up) */
    bool timeValid() const;
    /** Set the system time from the always-on timer (done at startup and after dormant automatically). */
    void syncTime();
    /** @return the always-on timer in milliseconds since its start value (the epoch given to setTime(), in ms) */
    uint64_t aonMs() const;
    /**
        @brief Trim of the always-on timer's low-power oscillator, in parts per million (RP2350).

        The oscillator runs the timer in deep sleep and dormant; its frequency is set from the chip's OTP calibration, and measured on a Pico 2 the timer was still about 5400 ppm slow
        in deep sleep (0.54 %, 20 s per hour).  setTimeTrimPpm(5400) makes it count that much faster.  Kept in POWMAN scratch register 5 (survives deep sleep, lost at power-up), applied at the next
        deep sleep entry only: awake and in light sleep the timer counts from the crystal, and in dormant the same oscillator ran about 1500 ppm fast, so dormant uses the untrimmed value.
        It is the trim of one chip at room temperature: measure it against NTP over a few deep sleeps.
    */
    void setTimeTrimPpm(int32_t ppm);
    /** @return the trim set with setTimeTrimPpm(), 0 if none */
    int32_t timeTrimPpm() const;

    /** @brief What woke the chip: WAKE_TIMER or WAKE_PIN after a deep sleep, WAKE_NONE if this boot is not a wake from deep sleep. */
    WakeReason deepSleepWakeReason() const;

    /**
        @brief GPIOs to leave untouched by the dormant low-leakage parking.

        The wake pin and any pin muxed to a UART or I2C are always left alone.

        @param [in] mask Bit n set = leave GPIO n alone
    */
    void keepPins(uint64_t mask);

    /**
        @brief Choose whether pins parked for dormant are restored on wake.

        Default true: every parked pin gets its pad (pulls, input enable, drive, slew,
        schmitt, ...) and IO (function, overrides) configuration back after wake.
        With false the pins stay in the low-leakage state.

        @param [in] restore true = restore on wake (default), false = leave parked
    */
    void setPinRestore(bool restore);

    /**
        @brief How long the last sleepFor/sleepUntilPin/dormantFor/dormantUntilPin call slept.

        Returns, for the most recent call:
          - sleepFor(ms), dormantFor(ms) that returned 0: the requested duration `ms`
            (not a measurement)
          - either form of sleepUntilPin() that returned 0: the measured duration (the system
            timer keeps running in light sleep)
          - dormantUntilPin() that returned 0: SLEEP_UNKNOWN (0xFFFFFFFF), because there is no
            reliable time reference while dormant (the system timer that millis() uses is stopped)
          - any call that returned an error (or sleepFor(0)): 0, nothing was slept
          - before the first call: 0

        millis() and micros() do NOT include time spent dormant (the timer stops there); light
        sleep (sleepFor, sleepUntilPin) keeps it running.  Add lastSleepMs() yourself after a
        dormant call if you need wall-clock time, where it is known (dormantFor).

        @return milliseconds slept, 0 if none, or SLEEP_UNKNOWN
    */
    uint32_t lastSleepMs() const;

    /**
        @brief What ended the most recent sleep or dormant call.

        sleepFor() and dormantFor() report WAKE_TIMER, sleepUntilPin(pin, mode) and
        dormantUntilPin() WAKE_PIN, sleepUntilPin(pin, mode, timeoutMs) either.  A call that
        returned an error (or sleepFor(0)) leaves WAKE_NONE; so does the time before the first call.

        @return WAKE_NONE, WAKE_PIN or WAKE_TIMER
    */
    WakeReason wakeReason() const {
        return _wakeReason;
    }

    /**
        @brief Peripherals that keep running during sleepFor() and sleepUntilPin() (light sleep; dormant ignores it).

        mask = KR_* bits OR-ed together; 0 (the default) is the normal gated light sleep.  Any non-zero mask also leaves the system clock on PLL_SYS (the
        library otherwise runs it from the 12 MHz crystal and stops the PLLs during the sleep): the kept peripherals keep their rate, and a byte
        arriving on a UART while the chip wakes is not damaged, at the cost of about 4 mA (the PLL) while asleep.  KR_UART0 keeps UART0 (Serial1) running in
        sleepFor() too, and makes the first byte of a sleepUntilPin() wake on its RX pin arrive intact.
        KR_ADC also keeps the DMA clock and PLL_USB (the ADC clock), another 5 mA.
    */
    enum KeepRunningMask { KR_PWM = 1, KR_PIO = 2, KR_UART0 = 4, KR_UART1 = 8, KR_ADC = 16, KR_DMA = 32 };
    void keepRunning(uint32_t mask);

    /**
        @brief Supply voltage (VSYS) in volts, from GP29 (VSYS / 3 through a 200 k + 100 k divider on the Pico and Pico 2).

        Averages `samples` 12-bit readings.  Untrimmed the result is about 1.4 % low on a Pico 2 (3.3 V ADC reference assumed); setVsysTrim() corrects gain and offset.
        On a Pico W / Pico 2 W GP29 is the WiFi chip's clock while the chip is powered: the call returns NAN then (use WiFi.powerOff() first).  Not thread-safe against another
        core using analogRead().  The ADC stays enabled afterwards (about 0.15 mA).
        @param [in] samples number of readings to average (default 16)
        @return volts, or NAN
    */
    float readVsys(int samples = 16);
    /** Volts = counts * (3.3 * 3 / 4096) * gain + offsetVolts.  On a Pico 2: gain 0.995, offset +0.077 V (fit against a PPK2 source, 3.2 to 5 V, within 12 mV). */
    void setVsysTrim(float gain, float offsetVolts);

private:
    uint64_t _keepMask = 0;
    bool _restore = true;
    bool _saved = false;
    uint64_t _savedKeep = 0;
    uint32_t _lastSleepMs = 0;
    WakeReason _wakeReason = WAKE_NONE;
    int sleepUntilPinImpl(pin_size_t pin, PinStatus mode, uint32_t timeoutMs, bool timed);
    uint64_t parkMask(int wakePin);
    void parkPins(uint64_t keep);
    void unparkPins();
    void enter();
    void leave();
};

extern LowPowerClass LowPower;
