#include "test.h"

#include "m65/device.h"

static M65DeviceInfo valid_device(void)
{
    M65DeviceInfo device;
    (void)memset(&device, 0, sizeof(device));
    device.external = true;
    device.removable = true;
    device.whole = true;
    device.block_size = 512U;
    device.known_controller = true;
    return device;
}

void test_device(void)
{
    M65DeviceInfo device = valid_device();
    EXPECT_EQ_INT(m65_validate_device(&device), M65_DEVICE_ACCEPTED);
    device.external = false;
    EXPECT_EQ_INT(m65_validate_device(&device), M65_DEVICE_INTERNAL);
    device = valid_device();
    device.mounted = true;
    EXPECT_EQ_INT(m65_validate_device(&device), M65_DEVICE_MOUNTED);
    device = valid_device();
    device.removable = false;
    EXPECT_EQ_INT(m65_validate_device(&device), M65_DEVICE_NOT_REMOVABLE);
    device = valid_device();
    device.whole = false;
    EXPECT_EQ_INT(m65_validate_device(&device), M65_DEVICE_NOT_WHOLE);
    device = valid_device();
    device.block_size = 4096U;
    EXPECT_EQ_INT(m65_validate_device(&device), M65_DEVICE_WRONG_BLOCK_SIZE);
    device = valid_device();
    device.known_controller = false;
    EXPECT_EQ_INT(m65_validate_device(&device), M65_DEVICE_UNKNOWN_CONTROLLER);
}
