echo off
set dir=%1
cd %dir%
if not exist share mkdir share
if exist share/maps cd share/maps & git pull
if not exist share/maps git clone https://git.code.sf.net/p/crossfire/crossfire-maps share/maps