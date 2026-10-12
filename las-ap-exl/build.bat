bash exlaunch.sh build
if %errorlevel% neq 0 exit /b %errorlevel%
bash exlaunch.sh deploy-ryu
if %errorlevel% neq 0 exit /b %errorlevel%