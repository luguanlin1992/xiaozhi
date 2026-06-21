#ifndef I2C_DEVICE_H
#define I2C_DEVICE_H

#include <driver/i2c_master.h>

class I2cDevice {
public:
    I2cDevice(i2c_master_bus_handle_t i2c_bus, uint8_t addr, uint32_t scl_speed_hz = 400000);

protected:
    i2c_master_dev_handle_t i2c_device_;

    void WriteReg(uint8_t reg, uint8_t value);
    esp_err_t WriteRegEx(uint8_t reg, uint8_t value);
    uint8_t ReadReg(uint8_t reg);
    esp_err_t ReadRegEx(uint8_t reg, uint8_t* value);
    void ReadRegs(uint8_t reg, uint8_t* buffer, size_t length);
    esp_err_t TransmitEx(const uint8_t* data, size_t length, int timeout_ms = 100);
};

#endif // I2C_DEVICE_H
