/**
 * @file main.ino
 * @brief INA236A monitoring with overcurrent alert testing.
 *
 * Hardware:
 *   MCU: ESP32-WROOM-32
 *   Sensor: Texas Instruments INA236A
 *   Shunt fisico nominal: 25 mOhm.
 *   Entrada A: usar 25 mOhm (0.025 ohm).
 *   Entrada B: usar aprox. 31 mOhm de resistencia equivalente.
 *   Ajuste de banco para B: 31.107383 mOhm (0.031107383 ohm).
 *   Entrada seleccionada actualmente: B.
 *
 * I2C:
 *   SDA: GPIO21
 *   SCL: GPIO22
 *
 * Alert:
 *   ALERT: GPIO27
 *   External pull-up: 4.7 kOhm to 3.3 V
 *   Threshold: 500 mA
 *   Active level: LOW
 *
 * Supports four INA236A addresses.
 * GPIO27 monitors only the selected device.
 */

#include <Arduino.h>
#include <Wire.h>

// =====================================================
// HARDWARE CONFIGURATION
// =====================================================

static constexpr uint8_t I2C_SDA_PIN = 21;
static constexpr uint8_t I2C_SCL_PIN = 22;

static constexpr uint8_t ALERT_GPIO = 27;

static constexpr uint32_t I2C_CLOCK_HZ = 100000;
static constexpr uint32_t SERIAL_BAUD_RATE = 115200;

static constexpr uint32_t READ_INTERVAL_MS = 200;

static constexpr uint8_t MAX_SENSORS = 4;

// Address of the device connected to GPIO27
static constexpr uint8_t ALERT_SENSOR_ADDRESS = 0x40;

// =====================================================
// INA236A ADDRESSES
// =====================================================

static constexpr uint8_t INA236_ADDRESSES[MAX_SENSORS] = {
    0x40,
    0x41,
    0x42,
    0x43
};

// =====================================================
// INA236A REGISTERS
// =====================================================

enum INA236Register : uint8_t {

    REG_CONFIGURATION   = 0x00,
    REG_SHUNT_VOLTAGE   = 0x01,
    REG_BUS_VOLTAGE     = 0x02,
    REG_POWER           = 0x03,
    REG_CURRENT         = 0x04,
    REG_CALIBRATION     = 0x05,
    REG_MASK_ENABLE     = 0x06,
    REG_ALERT_LIMIT     = 0x07,

    REG_MANUFACTURER_ID = 0x3E,
    REG_DEVICE_ID       = 0x3F
};

// =====================================================
// DEVICE IDENTIFICATION
// =====================================================

static constexpr uint16_t MANUFACTURER_ID = 0x5449;
static constexpr uint16_t DEVICE_ID = 0xA080;

// =====================================================
// MEASUREMENT PARAMETERS
// =====================================================

// Entrada A: resistencia de 25 mOhm; configurar 0.025f.
// Entrada B: resistencia equivalente de aprox. 31 mOhm, incluyendo la ruta de pistas.
// Para B se conserva el ajuste preciso de banco: 0.031107383f (31.107383 mOhm),
// obtenido con 298 mA de referencia. Actualmente se usa la entrada B.
// Esta constante se aplica a todos los sensores y ajusta corriente y umbral de alarma.
static constexpr float SHUNT_RESISTANCE_OHM = 0.031107383f;

static constexpr float CURRENT_LSB_A = 0.0001f;

static constexpr float SHUNT_VOLTAGE_LSB_V = 0.0000025f;

static constexpr float BUS_VOLTAGE_LSB_V = 0.0016f;

// =====================================================
// ADC CONFIGURATION
// =====================================================

// ADC range: +/-81.92 mV
// Averaging: 16 samples
// Conversion time: 1100 us per channel
// Mode: continuous bus and shunt

static constexpr uint16_t ADC_CONFIGURATION = 0x4527;

// ADCRANGE=0: CAL = 0.00512 / (CURRENT_LSB * Rshunt).
// Round to nearest register count; derive the effective LSB after rounding.
static constexpr uint16_t SHUNT_CALIBRATION = static_cast<uint16_t>(
    0.00512f / (CURRENT_LSB_A * SHUNT_RESISTANCE_OHM) + 0.5f
);
static_assert(SHUNT_CALIBRATION > 0 && SHUNT_CALIBRATION <= 0x7FFF,
              "Calibration outside INA236 register range");
static constexpr float EFFECTIVE_CURRENT_LSB_A =
    0.00512f / (SHUNT_CALIBRATION * SHUNT_RESISTANCE_OHM);

// =====================================================
// ALERT CONFIGURATION
// =====================================================

// Overcurrent threshold: 500 mA

static constexpr float ALERT_CURRENT_LIMIT_A = 0.5f;

// Shunt voltage threshold:
// Threshold voltage = reference current limit * effective path resistance.

static constexpr float ALERT_SHUNT_LIMIT_V =
    ALERT_CURRENT_LIMIT_A * SHUNT_RESISTANCE_OHM;

// Alert limit register value:
// Round threshold voltage / shunt voltage LSB to nearest register count.

static constexpr uint16_t ALERT_LIMIT_VALUE =
    static_cast<uint16_t>(
        ALERT_SHUNT_LIMIT_V / SHUNT_VOLTAGE_LSB_V + 0.5f
    );

// Mask/Enable register bit definitions

static constexpr uint16_t MASK_SOL  = 0x8000;
static constexpr uint16_t MASK_AFF  = 0x0010;
static constexpr uint16_t MASK_OVF  = 0x0004;

// Shunt over-limit, active-low, transparent mode

static constexpr uint16_t ALERT_CONFIGURATION = MASK_SOL;

// =====================================================
// DATA STRUCTURES
// =====================================================

struct INA236Device {

    uint8_t address;

    bool detected;
    bool configured;
};

struct INA236Measurement {

    uint8_t address;

    float busVoltageV;
    float shuntVoltageV;

    float currentA;
    float calculatedCurrentA;

    bool alertActive;
    bool alertFlag;
    bool mathOverflow;

    bool valid;
};

// =====================================================
// GLOBAL VARIABLES
// =====================================================

INA236Device sensors[MAX_SENSORS];

uint8_t detectedSensorCount = 0;

// =====================================================
// I2C REGISTER WRITE
// =====================================================

bool writeRegister(
    uint8_t address,
    uint8_t reg,
    uint16_t value
) {

    Wire.beginTransmission(address);

    Wire.write(reg);

    Wire.write(static_cast<uint8_t>(value >> 8));
    Wire.write(static_cast<uint8_t>(value & 0xFF));

    return Wire.endTransmission() == 0;
}

// =====================================================
// I2C REGISTER READ
// =====================================================

bool readRegister(
    uint8_t address,
    uint8_t reg,
    uint16_t &value
) {

    Wire.beginTransmission(address);

    Wire.write(reg);

    if (Wire.endTransmission(false) != 0) {
        return false;
    }

    if (Wire.requestFrom(address, static_cast<uint8_t>(2)) != 2) {
        return false;
    }

    uint8_t msb = Wire.read();
    uint8_t lsb = Wire.read();

    value = (static_cast<uint16_t>(msb) << 8) | lsb;

    return true;
}

// =====================================================
// IDENTIFY DEVICE
// =====================================================

bool identifyINA236(uint8_t address) {

    uint16_t manufacturerID = 0;
    uint16_t deviceID = 0;

    if (!readRegister(
        address,
        REG_MANUFACTURER_ID,
        manufacturerID
    )) {
        return false;
    }

    if (!readRegister(
        address,
        REG_DEVICE_ID,
        deviceID
    )) {
        return false;
    }

    return manufacturerID == MANUFACTURER_ID &&
           deviceID == DEVICE_ID;
}

// =====================================================
// CONFIGURE ALERT
// =====================================================

bool configureINA236Alert(uint8_t address) {

    // Program alert threshold

    if (!writeRegister(
        address,
        REG_ALERT_LIMIT,
        ALERT_LIMIT_VALUE
    )) {
        return false;
    }

    // Enable shunt over-limit alert

    if (!writeRegister(
        address,
        REG_MASK_ENABLE,
        ALERT_CONFIGURATION
    )) {
        return false;
    }

    // Verify alert configuration

    uint16_t registerValue = 0;

    if (!readRegister(
        address,
        REG_ALERT_LIMIT,
        registerValue
    )) {
        return false;
    }

    if (registerValue != ALERT_LIMIT_VALUE) {
        return false;
    }

    if (!readRegister(
        address,
        REG_MASK_ENABLE,
        registerValue
    )) {
        return false;
    }

    // Compare only configuration bits

    if ((registerValue & 0xFC03) != ALERT_CONFIGURATION) {
        return false;
    }

    Serial.printf(
        "Alert threshold: %.3f A\n",
        ALERT_CURRENT_LIMIT_A
    );

    Serial.printf(
        "Alert limit register: 0x%04X\n",
        ALERT_LIMIT_VALUE
    );

    return true;
}

// =====================================================
// CONFIGURE DEVICE
// =====================================================

bool configureINA236(uint8_t address) {

    if (!writeRegister(
        address,
        REG_CONFIGURATION,
        ADC_CONFIGURATION
    )) {
        return false;
    }

    if (!writeRegister(
        address,
        REG_CALIBRATION,
        SHUNT_CALIBRATION
    )) {
        return false;
    }

    uint16_t registerValue = 0;

    // Verify ADC configuration

    if (!readRegister(
        address,
        REG_CONFIGURATION,
        registerValue
    )) {
        return false;
    }

    if (registerValue != ADC_CONFIGURATION) {
        return false;
    }

    // Verify calibration

    if (!readRegister(
        address,
        REG_CALIBRATION,
        registerValue
    )) {
        return false;
    }

    if (registerValue != SHUNT_CALIBRATION) {
        return false;
    }

    // Enable ALERT only for selected device

    if (address == ALERT_SENSOR_ADDRESS) {

        if (!configureINA236Alert(address)) {
            return false;
        }

    } else {

        // Disable ALERT output functions

        if (!writeRegister(
            address,
            REG_MASK_ENABLE,
            0x0000
        )) {
            return false;
        }
    }

    return true;
}

// =====================================================
// DEVICE SCANNING
// =====================================================

void scanINA236Devices() {

    detectedSensorCount = 0;

    Serial.println();
    Serial.println("INA236A DEVICE SCAN");
    Serial.println("------------------------------");

    for (uint8_t i = 0; i < MAX_SENSORS; i++) {

        uint8_t address = INA236_ADDRESSES[i];

        sensors[i].address = address;
        sensors[i].detected = false;
        sensors[i].configured = false;

        Serial.printf(
            "Address 0x%02X: ",
            address
        );

        if (!identifyINA236(address)) {

            Serial.println("NOT FOUND");
            continue;
        }

        sensors[i].detected = true;

        if (!configureINA236(address)) {

            Serial.println("CONFIGURATION ERROR");
            continue;
        }

        sensors[i].configured = true;

        detectedSensorCount++;

        Serial.println("OK");
    }

    Serial.printf(
        "Detected devices: %u\n",
        detectedSensorCount
    );
}

// =====================================================
// READ MEASUREMENT
// =====================================================

bool readINA236(
    uint8_t address,
    INA236Measurement &measurement
) {

    uint16_t rawShunt = 0;
    uint16_t rawBus = 0;
    uint16_t rawCurrent = 0;
    uint16_t maskRegister = 0;

    measurement.address = address;
    measurement.valid = false;

    if (!readRegister(
        address,
        REG_SHUNT_VOLTAGE,
        rawShunt
    )) {
        return false;
    }

    if (!readRegister(
        address,
        REG_BUS_VOLTAGE,
        rawBus
    )) {
        return false;
    }

    if (!readRegister(
        address,
        REG_CURRENT,
        rawCurrent
    )) {
        return false;
    }

    if (!readRegister(
        address,
        REG_MASK_ENABLE,
        maskRegister
    )) {
        return false;
    }

    // Convert measurements

    measurement.shuntVoltageV =
        static_cast<int16_t>(rawShunt) *
        SHUNT_VOLTAGE_LSB_V;

    measurement.busVoltageV =
        static_cast<float>(rawBus & 0x7FFF) *
        BUS_VOLTAGE_LSB_V;

    measurement.currentA =
        static_cast<int16_t>(rawCurrent) *
        EFFECTIVE_CURRENT_LSB_A;

    measurement.calculatedCurrentA =
        measurement.shuntVoltageV /
        SHUNT_RESISTANCE_OHM;

    // Read alert status from GPIO

    measurement.alertFlag = (maskRegister & MASK_AFF) != 0;
    measurement.alertActive = false;

    if (address == ALERT_SENSOR_ADDRESS) {

        measurement.alertActive =
            digitalRead(ALERT_GPIO) == LOW;
    }

    // Check mathematical overflow

    measurement.mathOverflow =
        (maskRegister & MASK_OVF) != 0;

    measurement.valid = true;

    return true;
}

// =====================================================
// PRINT MEASUREMENT
// =====================================================

void printMeasurement(
    const INA236Measurement &measurement
) {

    if (!measurement.valid) {
        return;
    }

    Serial.println();

    Serial.printf(
        "INA236A [0x%02X]\n",
        measurement.address
    );

    Serial.println("------------------------------");

    Serial.printf(
        "Bus voltage:       %.4f V\n",
        measurement.busVoltageV
    );

    Serial.printf(
        "Shunt voltage:     %.4f mV\n",
        measurement.shuntVoltageV * 1000.0f
    );

    Serial.printf(
        "Current:           %.6f A\n",
        measurement.currentA
    );

    Serial.printf(
        "Calculated current: %.6f A\n",
        measurement.calculatedCurrentA
    );

    if (measurement.address == ALERT_SENSOR_ADDRESS) {

        Serial.printf(
            "ALERT register:    %s\n",
            measurement.alertFlag ? "ACTIVE" : "NORMAL"
        );

        Serial.printf(
            "ALERT GPIO:        %s\n",
            measurement.alertActive ? "LOW" : "HIGH"
        );

        Serial.printf(
            "ALERT STATUS:      %s\n",
            measurement.alertActive ? "ACTIVE" : "NORMAL"
        );
    }

    Serial.printf(
        "Math overflow:     %s\n",
        measurement.mathOverflow ? "YES" : "NO"
    );
}

// =====================================================
// SETUP
// =====================================================

void setup() {

    Serial.begin(SERIAL_BAUD_RATE);

    delay(1000);

    Serial.println();
    Serial.println("================================");
    Serial.println("ESP32 INA236A ALERT TEST");
    Serial.println("================================");

    // Configure alert input
    // External 4.7 kOhm pull-up is recommended

    pinMode(ALERT_GPIO, INPUT);

    if (!Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN)) {

        Serial.println("ERROR: I2C initialization failed");

        while (true) {
            delay(1000);
        }
    }

    Wire.setClock(I2C_CLOCK_HZ);
    Wire.setTimeOut(50);

    scanINA236Devices();

    if (detectedSensorCount == 0) {

        Serial.println("ERROR: No INA236A detected");

        while (true) {
            delay(1000);
        }
    }

    delay(100);

    Serial.printf("Rshunt: %.6f ohm; CAL: %u; current LSB: %.3f uA\n",
                  SHUNT_RESISTANCE_OHM, SHUNT_CALIBRATION,
                  EFFECTIVE_CURRENT_LSB_A * 1000000.0f);
    Serial.printf("Positive current full scale approx %.3f A for selected path\n",
                  32767.0f * SHUNT_VOLTAGE_LSB_V / SHUNT_RESISTANCE_OHM);
    Serial.println("ALERT: positive current > 0.500 A, device 0x40, GPIO27");
    Serial.println("System initialized");
}

// =====================================================
// MAIN LOOP
// =====================================================

void loop() {

    static uint32_t lastReadTime = 0;

    if (millis() - lastReadTime < READ_INTERVAL_MS) {
        return;
    }

    lastReadTime = millis();

    for (uint8_t i = 0; i < MAX_SENSORS; i++) {

        if (!sensors[i].configured) {
            continue;
        }

        INA236Measurement measurement = {};

        if (readINA236(
            sensors[i].address,
            measurement
        )) {

            printMeasurement(measurement);

        } else {

            Serial.printf(
                "I2C ERROR at 0x%02X\n",
                sensors[i].address
            );
        }
    }
}