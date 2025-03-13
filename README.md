```Chạy lệnh sau để tạo file:```


```shell
nano /usr/lib/pkgconfig/egl.pc
```

Paste these lines:
```text
prefix=/usr
exec_prefix=${prefix}
libdir=${exec_prefix}/lib
includedir=${prefix}/include

Name: EGL
Description: EGL Library
Version: 1.0.0
Libs: -L${libdir} -lEGL
Cflags: -I${includedir}
```
Then export the environment variable:
```shell
export PKG_CONFIG_PATH=/usr/lib/pkgconfig:$PKG_CONFIG_PATH
```
To check: 
```shell
pkg-config --modversion egl
```
In order to change the ``client-app-name``, execute this command:
```shell
sed -i 's/com.kienluu.chat-2/com.kienluu.chat/g' client.c
```

To export ``Display``:
```shell
export DISPLAY=:0
```

To run qemu with mouse:
```shell
runqemu qemuarm qemuparams="-usb -device usb-mouse"
```

* client.c
```shell
gcc -o chat_client client.c `pkg-config --cflags --libs gtk+-3.0` -lpthread
```
* server.c
```shell
gcc -o chat_server server.c -lpthread
```


```shell
source oe-init-build-env build-arm
```

```shell
runqemu qemuarm qemuparams="-usb -device usb-mouse"
```