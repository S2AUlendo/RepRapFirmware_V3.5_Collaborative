/*
 * PulsedFilamentSensor.h
 *
 *  Created on: 9 Jan 2018
 *      Author: David
 */

#ifndef SRC_FILAMENTSENSORS_PULSEDFILAMENTMONITOR_H_
#define SRC_FILAMENTSENSORS_PULSEDFILAMENTMONITOR_H_

#include "FilamentMonitor.h"

#if SUPPORT_DRIVERS

class PulsedFilamentMonitor : public FilamentMonitor
{
public:
	PulsedFilamentMonitor(unsigned int extruder, unsigned int monitorType) noexcept;

protected:
	GCodeResult Configure(const CanMessageGenericParser& parser, const StringRef& reply) noexcept override;
	void GetLiveData(FilamentMonitorDataNew& data) const noexcept override;

	FilamentSensorStatus Check(bool isPrinting, bool fromIsr, uint32_t isrMillis, float filamentConsumed) noexcept override;
	FilamentSensorStatus Clear() noexcept override;

	void Diagnostics(const StringRef& reply) noexcept override;
	bool Interrupt() noexcept override;

private:
	static constexpr float DefaultMmPerPulse = 1.0;
	static constexpr float DefaultMinMovementAllowed = 0.6;
	static constexpr float DefaultMaxMovementAllowed = 1.6;
	static constexpr float DefaultMinimumExtrusionCheckLength = 5.0;

	void Init() noexcept;
	void Reset() noexcept;
	void Poll() noexcept;
	FilamentSensorStatus CheckFilament(float amountCommanded, float amountMeasured, bool overdue) noexcept;

	bool DataReceived() const noexcept;
	bool HaveCalibrationData() const noexcept;
	float MeasuredSensitivity() const noexcept;

	// Configuration parameters
	float mmPerPulse;
	float minMovementAllowed, maxMovementAllowed;
	float minimumExtrusionCheckLength;

	// Other data
	uint32_t sensorValue;									// how many pulses received
	uint32_t lastIsrTime;									// the time we recorded an interrupt
	uint32_t lastSyncTime;									// the last time we synced a measurement
	uint32_t lastMeasurementTime;							// the last time we received a value

	float extrusionCommandedAtInterrupt;					// the amount of extrusion commanded (mm) when we received the interrupt since the last sync
	float extrusionCommandedSinceLastSync;					// the amount of extrusion commanded (mm) since the last sync
	float movementMeasuredSinceLastSync;					// the amount of movement in complete rotations of the wheel since the last sync
	bool wasPrintingAtInterrupt;
	bool haveInterruptData;

	float extrusionCommandedThisSegment;					// the amount of extrusion commanded (mm) since we last did a comparison
	float movementMeasuredThisSegment;						// the accumulated movement in complete rotations since the previous comparison

	// Values measured for calibration
	float minMovementRatio, maxMovementRatio;
	float totalExtrusionCommanded;
	float totalMovementMeasured;

	uint8_t samplesReceived;
	bool comparisonStarted;
	bool calibrationStarted;
};

#endif

#endif /* SRC_FILAMENTSENSORS_PULSEDFILAMENTMONITOR_H_ */
