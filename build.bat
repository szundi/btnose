call %HOMEPATH%\zephyrproject\zephyr\zephyr-env.cmd
west build -t clean
west build --board=btnose -p=auto

