idf_component_register(
  SRCS "user_app.c"
  PRIV_REQUIRES adc_bsp ui_bsp sd_card_bsp lcd_bl_pwm_bsp button_bsp qmi8658c ble_scan_bsp esp_wifi_bsp
  INCLUDE_DIRS "./")
