echo off
set dir=%1
if not exist %dir% mkdir %dir%
if not exist %dir%/var mkdir %dir%/var
if not exist %dir%/var/account mkdir %dir%/var/account
if not exist %dir%/var/players mkdir %dir%/var/players
if not exist %dir%/var/highscores mkdir %dir%/var/highscores
if not exist %dir%/var/unique-items mkdir %dir%/var/unique-items
if not exist %dir%/var/datafiles mkdir %dir%/var/datafiles
if not exist %dir%/etc mkdir %dir%/etc
if not exist %dir%/share mkdir %dir%/share
if not exist %dir%/share/i18n mkdir %dir%/share/i18n
if not exist %dir%/share/adm mkdir %dir%/share/adm
if not exist %dir%/share/help mkdir %dir%/share/help
if not exist %dir%/share/wizhelp mkdir %dir%/share/wizhelp
if not exist %dir%/lib mkdir %dir%/lib
if not exist %dir%/lib/plugins mkdir %dir%/lib/plugins
echo %cd%
xcopy ..\..\lib\config\*.* %dir%\etc\. /Y
xcopy ..\..\lib\help\*.* %dir%\share\help\. /Y
xcopy ..\..\lib\wizhelp\*.* %dir%\share\wizhelp\. /Y
xcopy ..\..\lib\adm\*.* %dir%\share\adm\. /Y
xcopy ..\..\lib\i18n\*.* %dir%\share\i18n\. /Y
xcopy ..\..\lib\def_help %dir%\share\. /Y
xcopy ..\..\lib\arch\attackmess %dir%\share\. /Y
xcopy ..\..\lib\arch\formulae %dir%\share\. /Y
xcopy ..\..\lib\arch\image_info %dir%\share\. /Y
xcopy ..\..\lib\arch\materials %dir%\share\. /Y
xcopy ..\..\lib\arch\messages %dir%\share\. /Y
xcopy ..\..\lib\arch\races %dir%\share\. /Y