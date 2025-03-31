### Video demo
```text
https://youtu.be/aMcj7Vty2NA
```

### Commands
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
gcc -o chat_client client.c AES.c `pkg-config --cflags --libs gtk+-3.0` -lpthread
```
* server.c
```shell
gcc -o chat_server server.c AES.c -lpthread
```


```shell
source oe-init-build-env build-arm
```

```shell
bitbake core-image-sato
```

```shell
runqemu qemuarm64
```



ứng dụng có lưu lịch sử chat vào 1 file chat_history.dat. ứng dụng chat ở đây là chat room, 
tức là mọi người đăng nhập vào đều nhắn vào 1 nhóm duy nhất. danh sách người dùng, thông tin đăng nhập được lưu 
vào 1 file user.dat . File này thì mã hóa mật khẩu AES. tên đăng nhập thì không mã hóa nhá. Còn lịch sử chat 
thì đều được mã hóa. ứng dụng sẽ có thêm chức năng gửi, tải file. 

