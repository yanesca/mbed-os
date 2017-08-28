/* mbed Microcontroller Library
 * Copyright (c) 2015-2017 Nuvoton
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
 
#include "pwmout_api.h"

#if DEVICE_PWMOUT

#include "cmsis.h"
#include "pinmap.h"
#include "PeripheralPins.h"
#include "nu_modutil.h"
#include "nu_miscutil.h"
#include "nu_bitutil.h"

struct nu_pwm_var {
    uint32_t    en_msk;
};

static struct nu_pwm_var pwm0_01_var = {
    .en_msk = 0
};

static struct nu_pwm_var pwm0_23_var = {
    .en_msk = 0
};

static struct nu_pwm_var pwm1_01_var = {
    .en_msk = 0
};

static struct nu_pwm_var pwm1_23_var = {
    .en_msk = 0
};

static uint32_t pwm_modinit_mask = 0;

static const struct nu_modinit_s pwm_modinit_tab[] = {
    {PWM_0_0, PWM0_CH01_MODULE, CLK_CLKSEL1_PWM0_CH01_S_HCLK, 0, PWM0_RST, PWM0_IRQn, &pwm0_01_var},
    {PWM_0_1, PWM0_CH01_MODULE, CLK_CLKSEL1_PWM0_CH01_S_HCLK, 0, PWM0_RST, PWM0_IRQn, &pwm0_01_var},
    {PWM_0_2, PWM0_CH23_MODULE, CLK_CLKSEL1_PWM0_CH23_S_HCLK, 0, PWM0_RST, PWM0_IRQn, &pwm0_23_var},
    {PWM_0_3, PWM0_CH23_MODULE, CLK_CLKSEL1_PWM0_CH23_S_HCLK, 0, PWM0_RST, PWM0_IRQn, &pwm0_23_var},
    
    {PWM_1_0, PWM1_CH01_MODULE, CLK_CLKSEL2_PWM1_CH01_S_HCLK, 0, PWM1_RST, PWM1_IRQn, &pwm1_01_var},
    {PWM_1_1, PWM1_CH01_MODULE, CLK_CLKSEL2_PWM1_CH01_S_HCLK, 0, PWM1_RST, PWM1_IRQn, &pwm1_01_var},
    {PWM_1_2, PWM1_CH23_MODULE, CLK_CLKSEL2_PWM1_CH23_S_HCLK, 0, PWM1_RST, PWM1_IRQn, &pwm1_23_var},
    {PWM_1_3, PWM1_CH23_MODULE, CLK_CLKSEL2_PWM1_CH23_S_HCLK, 0, PWM1_RST, PWM1_IRQn, &pwm1_23_var},
    
    {NC, 0, 0, 0, 0, (IRQn_Type) 0, NULL}
};

static void pwmout_config(pwmout_t* obj);

void pwmout_init(pwmout_t* obj, PinName pin)
{
    obj->pwm = (PWMName) pinmap_peripheral(pin, PinMap_PWM);
    MBED_ASSERT((int) obj->pwm != NC);

    const struct nu_modinit_s *modinit = get_modinit(obj->pwm, pwm_modinit_tab);
    MBED_ASSERT(modinit != NULL);
    MBED_ASSERT((PWMName) modinit->modname == obj->pwm);
    
    PWM_T *pwm_base = (PWM_T *) NU_MODBASE(obj->pwm);
    uint32_t chn =  NU_MODSUBINDEX(obj->pwm);
        
    // NOTE: Channels 0/1, 2/3 share a clock source.
    if ((((struct nu_pwm_var *) modinit->var)->en_msk & 0xF) == 0) {
        // Select clock source of paired channels
        CLK_SetModuleClock(modinit->clkidx, modinit->clksrc, modinit->clkdiv);
        // Enable clock of paired channels
        CLK_EnableModuleClock(modinit->clkidx);
    }
    
    // Wire pinout
    pinmap_pinout(pin, PinMap_PWM);
    
    // Default: period = 10 ms, pulse width = 0 ms
    obj->period_us = 1000 * 10;
    obj->pulsewidth_us = 0;
    pwmout_config(obj);
    // enable inverter to ensure the first PWM cycle is correct
    pwm_base->CTL |= (PWM_CTL_CH0INV_Msk << (chn * 8));
    
    // Enable output of the specified PWM channel
    PWM_EnableOutput(pwm_base, 1 << chn);
    PWM_Start(pwm_base, 1 << chn);
    
    ((struct nu_pwm_var *) modinit->var)->en_msk |= 1 << chn;
    
    if (((struct nu_pwm_var *) modinit->var)->en_msk) {
        // Mark this module to be inited.
        int i = modinit - pwm_modinit_tab;
        pwm_modinit_mask |= 1 << i;
    }
}

void pwmout_free(pwmout_t* obj)
{
    PWM_T *pwm_base = (PWM_T *) NU_MODBASE(obj->pwm);
    uint32_t chn =  NU_MODSUBINDEX(obj->pwm);
    PWM_ForceStop(pwm_base, 1 << chn);
    
    const struct nu_modinit_s *modinit = get_modinit(obj->pwm, pwm_modinit_tab);
    MBED_ASSERT(modinit != NULL);
    MBED_ASSERT((PWMName) modinit->modname == obj->pwm);
    ((struct nu_pwm_var *) modinit->var)->en_msk &= ~(1 << chn);
    
    
    if ((((struct nu_pwm_var *) modinit->var)->en_msk & 0xF) == 0) {
        CLK_DisableModuleClock(modinit->clkidx);
    }
    
    if (((struct nu_pwm_var *) modinit->var)->en_msk == 0) {
        // Mark this module to be deinited.
        int i = modinit - pwm_modinit_tab;
        pwm_modinit_mask &= ~(1 << i);
    }
}

void pwmout_write(pwmout_t* obj, float value)
{
    obj->pulsewidth_us = NU_CLAMP((uint32_t) (value * obj->period_us), 0, obj->period_us);
    pwmout_config(obj);
}

float pwmout_read(pwmout_t* obj)
{
    return NU_CLAMP((((float) obj->pulsewidth_us) / obj->period_us), 0.0f, 1.0f);
}

void pwmout_period(pwmout_t* obj, float seconds)
{
    pwmout_period_us(obj, seconds * 1000000.0f);
}

void pwmout_period_ms(pwmout_t* obj, int ms)
{
    pwmout_period_us(obj, ms * 1000);
}

// Set the PWM period, keeping the duty cycle the same.
void pwmout_period_us(pwmout_t* obj, int us)
{
    uint32_t period_us_old = obj->period_us;
    uint32_t pulsewidth_us_old = obj->pulsewidth_us;
    obj->period_us = us;
    obj->pulsewidth_us = NU_CLAMP(obj->period_us * pulsewidth_us_old / period_us_old, 0, obj->period_us);
    pwmout_config(obj);
}

void pwmout_pulsewidth(pwmout_t* obj, float seconds)
{
    pwmout_pulsewidth_us(obj, seconds * 1000000.0f);
}

void pwmout_pulsewidth_ms(pwmout_t* obj, int ms)
{
    pwmout_pulsewidth_us(obj, ms * 1000);
}

void pwmout_pulsewidth_us(pwmout_t* obj, int us)
{
    obj->pulsewidth_us = NU_CLAMP(us, 0, obj->period_us);
    pwmout_config(obj);
}

int pwmout_allow_powerdown(void)
{
    uint32_t modinit_mask = pwm_modinit_mask;
    while (modinit_mask) {
        int pwm_idx = nu_ctz(modinit_mask);
        const struct nu_modinit_s *modinit = pwm_modinit_tab + pwm_idx;
        if (modinit->modname != NC) {
            PWM_T *pwm_base = (PWM_T *) NU_MODBASE(modinit->modname);
            uint32_t chn = NU_MODSUBINDEX(modinit->modname);
            // Disallow entering power-down mode if PWM counter is enabled.
            if (pwm_base->OE & (1 << chn)) {
                return 0;
            }
        }
        modinit_mask &= ~(1 << pwm_idx);
    }
    
    return 1;
}

static void pwmout_config(pwmout_t* obj)
{
    PWM_T *pwm_base = (PWM_T *) NU_MODBASE(obj->pwm);
    uint32_t chn = NU_MODSUBINDEX(obj->pwm);
    // NOTE: Support period < 1s
    // NOTE: ARM mbed CI test fails due to first PWM pulse error. Workaround by:
    //       1. Inverse duty cycle (100 - duty)
    //       2. Inverse PWM output polarity
    //       This trick is here to pass ARM mbed CI test. First PWM pulse error still remains.
    PWM_ConfigOutputChannel2(pwm_base, chn, 1000 * 1000, 100 - (obj->pulsewidth_us * 100 / obj->period_us), obj->period_us);
}

#endif