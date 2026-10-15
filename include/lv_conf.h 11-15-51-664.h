#ifndef LV_CONF_H
#define LV_CONF_H

/* Enable this configuration file */
#if 1

/* Waveshare display colour format */
#define LV_COLOR_DEPTH 16

/* Required for some RGB565 displays */
#define LV_COLOR_16_SWAP 0

/* Use LVGL's built-in memory allocator */
#define LV_MEM_CUSTOM 0

/* Give LVGL enough memory for a larger interface */
#define LV_MEM_SIZE (128U * 1024U)

/* Refresh rate */
#define LV_DISP_DEF_REFR_PERIOD 30

/* Input-device reading rate */
#define LV_INDEV_DEF_READ_PERIOD 30

/* Enable common widgets */
#define LV_USE_LABEL 1
#define LV_USE_BTN 1
#define LV_USE_SLIDER 1
#define LV_USE_SWITCH 1
#define LV_USE_BAR 1
#define LV_USE_ARC 1
#define LV_USE_CHART 1
#define LV_USE_DROPDOWN 1
#define LV_USE_TEXTAREA 1
#define LV_USE_KEYBOARD 1

/* Enable the default Montserrat font */
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

#endif /* Configuration enabled */

#endif /* LV_CONF_H */