@echo OFF
rem Makes the codec placeholders of both trees: the releases' own scripts, prepare_GD77.bat (firmware\, MK22)
rem and prepare_MDUV380.bat (MDUV380_firmware\, STM32), kept unchanged
set status=0
if exist firmware call prepare_GD77.bat || set status=1
if exist MDUV380_firmware call prepare_MDUV380.bat || set status=1
exit /b %status%
