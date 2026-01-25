#!/bin/bash
cd /home/attax/Coding/pipewine
make clean
DEBUG=true make 64
sudo make install64
cp build64/pipewine64.dll ~/Games/ableton-live2/drive_c/windows/system32/
WINEPREFIX=/home/attax/Games/ableton-live2 wine regsvr32 "/home/attax/Coding/pipewine/build64/pipewine64.dll"
#WINEPREFIX=/home/attax/Games/ableton-live2 wine "/home/attax/Games/ableton-live2/drive_c/ProgramData/Ableton/Live 11 Intro/Program/Ableton Live 11 Intro.exe"
