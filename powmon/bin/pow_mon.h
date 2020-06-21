/* 进程化状态 */
#define USE_TIMER
#define RUN             0
#define START           1
#define STOP            2
#define STATUS          3
#define RESTART         4        

//定义了抽样函数中定期进行mqtt发送的信息    
#define PUB_METRIC(type, name, function, id, format) \
    sprintf(tmp_, "%s/%s/%d/%s", sysd->topic, type, id,name); \
    sprintf(data, format, function, sysd->tmpstr); \
    if(mosquitto_publish(mosq, NULL, tmp_, strlen(data), data, sysd->qos, false) != MOSQ_ERR_SUCCESS) { \
        fprintf(fp, "[MQTT]: Warning: cannot send message.\n");  \
    } \
    


