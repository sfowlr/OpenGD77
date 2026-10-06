
Pushd ..\..\firmware\include\user_interface\languages\src
gcc -Wall -O2 -I../ -o languages_builder languages_builder.c
languages_builder.exe
popd


tar.exe -a -c -f ..\..\firmware\GD-77\OpenGD77.zip -C ..\..\firmware\GD-77 OpenGD77.bin -C ..\..\firmware\include\user_interface\languages\src\ *.gla
tar.exe -a -c -f ..\..\firmware\GD-77\OpenGD77_NMEA.zip -C ..\..\firmware\GD-77_NMEA OpenGD77_NMEA.bin -C ..\..\firmware\include\user_interface\languages\src\ *.gla

tar.exe -a -c -f ..\..\firmware\GD-77S\OpenGD77S.zip -C ..\..\firmware\GD-77S OpenGD77S.bin -C ..\..\firmware\include\user_interface\languages\src\ *.gla

tar.exe -a -c -f ..\..\firmware\DM-1801\OpenDM1801.zip -C ..\..\firmware\DM-1801 OpenDM1801.bin -C ..\..\firmware\include\user_interface\languages\src\ *.gla
tar.exe -a -c -f ..\..\firmware\DM-1801\OpenDM1801_NMEA.zip -C ..\..\firmware\DM-1801_NMEA OpenDM1801_NMEA.bin -C ..\..\firmware\include\user_interface\languages\src\ *.gla

tar.exe -a -c -f ..\..\firmware\DM-1801A\OpenDM1801A.zip -C ..\..\firmware\DM-1801A OpenDM1801A.bin -C ..\..\firmware\include\user_interface\languages\src\ *.gla
tar.exe -a -c -f ..\..\firmware\DM-1801A\OpenDM1801A_NMEA.zip -C ..\..\firmware\DM-1801A_NMEA OpenDM1801A_NMEA.bin -C ..\..\firmware\include\user_interface\languages\src\ *.gla

tar.exe -a -c -f ..\..\firmware\RD-5R\OpenRD5R.zip -C ..\..\firmware\RD-5R\ OpenRD5R.bin -C ..\..\firmware\include\user_interface\languages\src\ *.gla

tar.exe -a -c -f ..\..\firmware\JA_GD-77\OpenGD77_Japanese.zip -C ..\..\firmware\JA_GD-77 OpenGD77_Japanese.bin
tar.exe -a -c -f ..\..\firmware\JA_GD-77\OpenGD77_NMEA_Japanese.zip -C ..\..\firmware\JA_GD-77_NMEA OpenGD77_NMEA_Japanese.bin

tar.exe -a -c -f ..\..\firmware\JA_DM-1801\OpenDM1801_Japanese.zip -C ..\..\firmware\JA_DM-1801 OpenDM1801_Japanese.bin
tar.exe -a -c -f ..\..\firmware\JA_DM-1801\OpenDM1801_NMEA_Japanese.zip -C ..\..\firmware\JA_DM-1801_NMEA OpenDM1801_NMEA_Japanese.bin

tar.exe -a -c -f ..\..\firmware\JA_DM-1801A\OpenDM1801A_Japanese.zip -C ..\..\firmware\JA_DM-1801A OpenDM1801A_Japanese.bin
tar.exe -a -c -f ..\..\firmware\JA_DM-1801A\OpenDM1801A_NMEA_Japanese.zip -C ..\..\firmware\JA_DM-1801A_NMEA OpenDM1801A_NMEA_Japanese.bin

tar.exe -a -c -f ..\..\firmware\JA_RD-5R\OpenRD5R_Japanese.zip -C ..\..\firmware\JA_RD-5R OpenRD5R_Japanese.bin

pause
