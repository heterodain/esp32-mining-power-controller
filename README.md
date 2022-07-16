# esp32-mining-power-controller

GPU power limit control of Hive OS and PC(RIG) power ON/OFF control by Light sensor using ESP32 and BH1750FVI and Photocoupler

- GPU power limit control:  
BH1750FVI -> ESP32 DevkitC -> Hive API -> Hive OS -> GPU
- PC(RIG) power ON/OFF control:  
BH1750FVI -> ESP32 DevkitC <-> PC(RIG) power button and USB

[![Video1](https://img.youtube.com/vi/crbL_Oq8djw/0.jpg)](https://www.youtube.com/watch?v=crbL_Oq8djw)

## web-api / web-services used
Hive API https://hiveon.com/forum/t/hive-api-v2/4490  
Holiday API https://s-proj.com/utils/holiday.html  
Ambient https://ambidata.io/
