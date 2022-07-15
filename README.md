# esp32-mining-power-controller

GPU power limit control of Hive OS and PC(RIG) power ON/OFF control by Light sensor using ESP32 and BH1750FVI

- GPU power limit control:  
BH1750FVI -> ESP32 DevkitC -> Hive API -> Hive OS -> GPU
- PC(RIG) power ON/OFF control:  
BH1750FVI -> ESP32 DevkitC <-> PC(RIG) power Button and USB
