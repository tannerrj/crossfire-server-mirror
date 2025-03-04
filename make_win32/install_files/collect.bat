echo off
set dir=%1
cd %dir%
if not exist share mkdir share
.\crossfire.exe -data ../../lib -conf ../../lib/config -pack-assets archs share\crossfire.arc -pack-assets faces share\crossfire.face -pack-assets treasures share\crossfire.trs -pack-assets images share\crossfire.tar -pack-assets artifacts share\crossfire.artifacts