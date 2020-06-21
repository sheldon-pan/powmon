
>$ make install
会在powmon/bin/bin文件夹下生成powmon和powmon.conf
powmon是监控程序的可执行文件
powmon.conf是配置文件
MQTT 参数:
- brokerHost: MQTT broker的IP地址
- brokerPort: MQTT broker的端口号 (1883)
- topic: 发布到MQTT的话题
采样参数:
- dT: 默认采样周期（秒）
- daemonize: true或false 是否使监控程序后台运行
- pidfiledir: pidfile存放目录
- logfiledir: logfile存放目录


1) 在服务器上运行mosquitto，建立broker::

    >$ ./lib/mosquitto-1.3.5/src/mosquitto -d 

2) 在各个客户端编辑pow_mon.conf配置mqtt的话题，以及broker的IP、端口、QOS以及默认的采样时间间隔

3) 需要加载msr驱动

   >$ sudo modprobe msr
  
4) 进入监控文件夹，以root权限运行监控程序
   
   >$ sudo ./pow_mon

5) 在服务商可以查看接收的消息。MQTT订阅客户端在 ./lib/mosquitto-1.3.5/client 文件夹，通过mosquitto_sub可以查看发送到该broker的所有消息

   >$ LD_LIBRARY_PATH=../lib/:$LD_LIBRARY_PATH ./mosquitto_sub -h 127.0.0.1 -t "org/myorg/cluster/testcluster/#" -v

6) 关闭进程 可以直接在pow_mon目录下执行附加stop的pow_mon

   >$ sudo ./pow_mon stop
   
   也可以
   >$ sudo pkill pow_mon


