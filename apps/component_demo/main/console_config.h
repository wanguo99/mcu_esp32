#pragma once
// Kconfig omits disabled bools. Supply numeric fallbacks for C expressions.
#ifndef CONFIG_DEMO_SERVO
#define CONFIG_DEMO_SERVO 0
#endif
#ifndef CONFIG_DEMO_FAN
#define CONFIG_DEMO_FAN 0
#endif
#ifndef CONFIG_DEMO_RELAY
#define CONFIG_DEMO_RELAY 0
#endif
#ifndef CONFIG_DEMO_IMU
#define CONFIG_DEMO_IMU 0
#endif
#ifndef CONFIG_DEMO_LED
#define CONFIG_DEMO_LED 0
#endif
#ifndef CONFIG_DEMO_FAN_INVERTED
#define CONFIG_DEMO_FAN_INVERTED 0
#endif
#ifndef CONFIG_DEMO_RELAY_FIXED
#define CONFIG_DEMO_RELAY_FIXED 0
#endif
#ifndef CONFIG_DEMO_RELAY_TWO_STOP
#define CONFIG_DEMO_RELAY_TWO_STOP 0
#endif
#ifndef CONFIG_DEMO_IMU_AD0_HIGH
#define CONFIG_DEMO_IMU_AD0_HIGH 0
#endif
#ifndef CONFIG_DEMO_IMU_INTERNAL_PULLUPS
#define CONFIG_DEMO_IMU_INTERNAL_PULLUPS 0
#endif
#ifndef CONFIG_DEMO_LED_RGB
#define CONFIG_DEMO_LED_RGB 0
#endif
