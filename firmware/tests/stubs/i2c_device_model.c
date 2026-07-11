#include "i2c_device_model.h"
#include <string.h>

#define MAX_MODELS 8

static I2cDeviceModel *models[MAX_MODELS];
static int             model_count = 0;

void i2c_model_register(I2cDeviceModel *model) {
    if (model_count < MAX_MODELS)
        models[model_count++] = model;
}

void i2c_model_reset_all(void) {
    model_count = 0;
    memset(models, 0, sizeof(models));
}

I2cDeviceModel *i2c_model_find(uint16_t dev_addr) {
    for (int i = 0; i < model_count; i++)
        if (models[i]->dev_addr == dev_addr)
            return models[i];
    return NULL;
}
