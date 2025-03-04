echo off
set dir=%1
cd %dir%
md -force share
git clone https://git.code.sf.net/p/crossfire/crossfire-maps share/maps