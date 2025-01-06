/*
 * DIY motor control implementation.
 */

#include <Arduino.h>
#include <TeensyThreads.h>
#include <IntervalTimer.h>
#include <Teensy41_Pinout.h>

// Add the PulsePairSteppers class definition here
class PulsePairSteppers {
    private:
    const int stepPin, dirPin1, dirPin2, enablePin1, enablePin2;
    volatile float highPulseUs, lowPulseUs;
    volatile int dir, stepSpeed, maxSpeed, maxAccel, accelStepCount, velocityIncr, pulseCount;
    volatile bool pulseState;
    IntervalTimer stepTimer;

    // Add static instance pointer
    static PulsePairSteppers* isrInstance;
    
    // Modify ISR to be static without arguments
    static void timerISR() {
        if (isrInstance) {
            if (isrInstance->pulseState) {
                digitalWriteFast(isrInstance->stepPin, LOW);  // End high pulse
                isrInstance->stepTimer.update(isrInstance->lowPulseUs);   // Switch to low duration
            } else {
                digitalWriteFast(isrInstance->stepPin, HIGH); // Start high pulse
                isrInstance->stepTimer.update(isrInstance->highPulseUs);   // Switch to high duration
            }
            isrInstance->pulseState = !isrInstance->pulseState;
        }
    }

    void updateVelocity(int stepsPerSecond) { // Changes velocity setpoint instantaneously.
        if (abs(stepsPerSecond) > maxSpeed) { // Clip the speed within system operating limits
            stepsPerSecond = (stepsPerSecond > 0) ? maxSpeed : -maxSpeed;
        }
        stepSpeed = stepsPerSecond;
        dir = getDirection();
        
        noInterrupts(); // prevent interrupts during setpoint and pin level changes
        if(stepSpeed != 0) {
            digitalWriteFast(dirPin1, dir);  // Motor 1 direction
            digitalWriteFast(dirPin2, !dir); // Motor 2 direction. TEMPORARY reverse for test setup
            float totalPeriod = 1000000.0f / abs(stepSpeed);
            lowPulseUs = totalPeriod - highPulseUs; // highPulseUs defined in construction

            digitalWriteFast(stepPin, HIGH);  // Ensure stepPin starts HIGH
            pulseState = true;                // Set pulseState to match HIGH
            stepTimer.begin(timerISR, highPulseUs); // run high pulse cycle timer
        } else {
            stepTimer.end();
        }
        interrupts();
    }

public:
    PulsePairSteppers(int sp, int dp1, int dp2, int ep1, int ep2, int maxSp = 40000, int maxAcc = 100000, float hP_Us = 3.0f) : 
        stepPin(sp), dirPin1(dp1), dirPin2(dp2), enablePin1(ep1), enablePin2(ep2),
        highPulseUs(hP_Us), stepSpeed(0), maxSpeed(maxSp), maxAccel(maxAcc), pulseState(false)
        {
        pinMode(stepPin, OUTPUT);
        pinMode(dirPin1, OUTPUT);
        pinMode(dirPin2, OUTPUT);
        pinMode(enablePin1, OUTPUT);
        pinMode(enablePin2, OUTPUT);
        
        digitalWriteFast(stepPin, LOW);
        digitalWriteFast(dirPin1, LOW);
        digitalWriteFast(dirPin2, HIGH); // TEMPORARY - reversed dir from Motor 1 for testing setup
        digitalWriteFast(enablePin1, HIGH); // Start disabled
        digitalWriteFast(enablePin2, HIGH); // Start disabled
        isrInstance = this;  // Set instance pointer
    }

    void enable() {
        noInterrupts(); // prevent interrupts between driver pin signals
        digitalWriteFast(enablePin1, LOW);
        digitalWriteFast(enablePin2, LOW);
        interrupts();
    }

    void disable() {
        noInterrupts(); // prevent interrupts between driver pin signals
        digitalWriteFast(enablePin1, HIGH);
        digitalWriteFast(enablePin2, HIGH);
        interrupts();
    }

    void setVelocity(int stepsPerSecond) { // Drives to target velocity in steps/s
        noInterrupts();
        int deltaV = stepsPerSecond - stepSpeed;
        float time = deltaV / maxAccel;
        float vAvg = (stepsPerSecond + stepSpeed) / 2;
        accelStepCount = int(abs(time * vAvg)); // steps with constant velocity increment 'til target
        velocityIncr = deltaV / accelStepCount; // constant velicity increment
        pulseCount = 0;
        interrupts();

        for (; accelStepCount;) {
            updateVelocity(getStepSpeed() + velocityIncr);
            threads.yield();
        }

    }

    // Getters and setters
    int getStepSpeed() const { return stepSpeed; }
    bool getDirection() const { return (stepSpeed > 0); } // true for CCW, false for CW
    void setMaxSpeed(int maxSp) { maxSpeed = abs(maxSp); } // Ensure it's non-negative
};

// Add static member initialization outside the class
PulsePairSteppers* PulsePairSteppers::isrInstance = nullptr;

// Define PulsePairSteppers motor control object
volatile float speed = 1000; // TEMPORARY - init to 0 when done with testing. ------------------------------------ # INITIAL SPEED SETTING #
volatile float targetSpeed = -speed;
Threads::Mutex motorMutex;
PulsePairSteppers steppers(pin33, pin34, pin31, pin35, pin32);

void ControlThread() {
    int lastSpeed = 0;    // Cache for the last set speed
    int currentSpeed = 0; // Cache current target speed
    while(true) {
        digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN)); // TEMPORARY LED blinks for testing

        {
        Threads::Scope lock(motorMutex); // motorMutex prevents race accessing targetSpeed
        currentSpeed = targetSpeed;
        }

        if (currentSpeed != lastSpeed) { // Only update the velocity if it has changed
            steppers.setVelocity(currentSpeed);
            lastSpeed = currentSpeed;
        }

        threads.yield();
    }
}

void SensorThread() {
    while(true) {
        Serial.println("SensorThread");
        threads.delay(499);
    }
}

void CommsThread() {
    while(true) {
        Serial.println("CommsThread");
        
        motorMutex.lock();
        targetSpeed = 0;  // short pause
        motorMutex.unlock();
        threads.delay(200);

        motorMutex.lock();
        targetSpeed = speed; // forward
        motorMutex.unlock();
        threads.delay(2000);

        motorMutex.lock();
        targetSpeed = 0;  // short pause
        motorMutex.unlock();
        threads.delay(200);

        motorMutex.lock();
        targetSpeed = -speed; // reverse
        motorMutex.unlock();
        threads.delay(2000);
    }
}

void setup() {
    Serial.begin(115200);
    
    // Simplify pin setup - only LED needed as class handles other pins
    pinMode(LED_BUILTIN, OUTPUT);
    
    delay(200); // Keep delay for DM860T startup
    
    steppers.enable(); // Enable motors using class method
    
    threads.addThread(ControlThread);
    threads.addThread(SensorThread);
    threads.addThread(CommsThread);
}

void loop() {
    threads.yield();
}