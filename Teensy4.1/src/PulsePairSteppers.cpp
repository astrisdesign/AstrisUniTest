#include "PulsePairSteppers.h"

PulsePairSteppers* PulsePairSteppers::isrInstance = nullptr;

void PulsePairSteppers::calculatePulseWait() { // pulseWait prevents runaway acceleration and motor lockout.
    bool accelerating = (abs(targetSpeed) > abs(stepSpeed)) && ((targetSpeed * stepSpeed) > 0);
    pulseWait = accelerating
    ? ((abs(stepSpeed) - 1500) * 1000) / maxSpeed   // acceleration profile
    : ((abs(stepSpeed) - 50) * 30) / maxSpeed;  // deceleration profile
}

void PulsePairSteppers::timerISR() { // pulse hardware timer interrupt service routine
    if (isrInstance) {
        if (isrInstance->pulseState) {
            digitalWriteFast(isrInstance->stepPin, LOW);            // End high pulse
            isrInstance->stepTimer.update(isrInstance->lowPulseUs); // Switch to low duration
        } else {
            digitalWriteFast(isrInstance->stepPin, HIGH); // Start high pulse
            isrInstance->stepTimer.update(isrInstance->highPulseUs);   // Switch to high duration

            if (isrInstance->stepSpeed != isrInstance->targetSpeed) { // Check if moving at target speed
                if (--isrInstance->pulseWait <= 0) { // Check if waited enough pulses, decrement
                    isrInstance->setVelocity(isrInstance->targetSpeed);
                }
            }
        }
        isrInstance->pulseState = !isrInstance->pulseState;     // Toggle pulse state
    }
}

PulsePairSteppers::PulsePairSteppers(int sp, int dp1, int dp2, int ep1, int ep2, int maxSp = 35000, float hP_Us = 3.0f) : 
    stepPin(sp), dirPin1(dp1), dirPin2(dp2),
    enablePin1(ep1), enablePin2(ep2), highPulseUs(hP_Us),
    stepSpeed(0), targetSpeed(0), maxSpeed(maxSp), maxDeltaV(800), pulseWait(0), pulseState(false) {
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

void PulsePairSteppers::setVelocity(int stepsPerSecond) {
    if (abs(stepsPerSecond) > maxSpeed) { // Clip new speed setpoint within system speed limit
        stepsPerSecond = (stepsPerSecond > 0) ? maxSpeed : -maxSpeed;
    }
    targetSpeed = stepsPerSecond;

    int deltaV = abs(targetSpeed - stepSpeed); // Clip speed change within system acceleration limit
    if (deltaV > maxDeltaV) {
        stepsPerSecond = (targetSpeed > stepSpeed) ? (stepSpeed + maxDeltaV) : (stepSpeed - maxDeltaV);
    }
    stepSpeed = stepsPerSecond; // new motor velocity setpoint
    dir = getDirection();
    calculatePulseWait(); // update the acceleration waiting period

    noInterrupts(); // prevent interrupts during setpoint and pin level changes
    if(stepsPerSecond != 0) {
        digitalWriteFast(dirPin1, dir);  // Motor 1 direction
        digitalWriteFast(dirPin2, !dir); // Motor 2 direction. TEMPORARY reverse for test setup
        float totalPeriod = 1000000.0f / abs(stepsPerSecond);
        lowPulseUs = totalPeriod - highPulseUs; // highPulseUs defined in construction

        digitalWriteFast(stepPin, HIGH);  // Ensure stepPin starts HIGH
        pulseState = true;                // Set pulseState to match HIGH
        stepTimer.begin(timerISR, highPulseUs); // run high pulse cycle timer
    } else {
        stepTimer.end();
    }
    interrupts();
}

void PulsePairSteppers::enable() {
    noInterrupts(); // prevent interrupts between driver pin signals
    digitalWriteFast(enablePin1, LOW);
    digitalWriteFast(enablePin2, LOW);
    interrupts();
}

void PulsePairSteppers::disable() {
    noInterrupts(); // prevent interrupts between driver pin signals
    digitalWriteFast(enablePin1, HIGH);
    digitalWriteFast(enablePin2, HIGH);
    interrupts();
}

// Getters and setters
int PulsePairSteppers::getStepSpeed() const { return stepSpeed; }
bool PulsePairSteppers::getDirection() const { return (stepSpeed > 0); } // true for CCW, false for CW
void PulsePairSteppers::setMaxSpeed(int maxSp) { maxSpeed = abs(maxSp); } // Ensure it's non-negative