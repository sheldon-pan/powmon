#!/usr/bin/env python
# -*- coding: UTF-8 -*-

import argparse
import sys
import signal
from influxdb import InfluxDBClient
import json
import urllib
import logging
import paho.mqtt.client as mqtt
import requests.exceptions
import ConfigParser

# This holds the parsed arguments for the entire script
parserArgs = None
# This lets the message received event handler know that the DB connection is ready
dbConn = None
# This is the MQTT client object
client = None


def _sigIntHandler(signum, frame):
    '''This function handles Ctrl+C for graceful shutdown of the programme'''
    logging.info("Received Ctrl+C. Exiting.")
    stopMQTT()
    stopInfluxDB()
    exit(0)
def setupLogging():
    '''Sets up logging'''
    logging.basicConfig(level=logging.INFO, format='%(asctime)s %(message)s')    


def setupSigInt():
    '''Ctrl+c退出'''
    signal.signal(signal.SIGINT, _sigIntHandler)
    logging.debug("Installed Ctrl+C handler.")

def mqttOnConnect(client, userdata, flags , rc):
    '''This is the event handler for when one has connected to the MQTT broker. Will exit() if connect is not successful.'''
    if rc == 0:
        logging.info("Connected to MQTT broker successfully.")
        print("Connected to MQTT broker successfully ")
        startInfluxDB()
        client.subscribe(MQTT_IN_TOPIC+'/#', qos=int(Qos))
        return
    elif rc == 1:
        logging.critical("Connection to broker refused - incorrect protocol version.")
    elif rc == 2:
        logging.critical("Connection to broker refused - invalid client identifier.")
    elif rc == 3:
        logging.critical("Connection to broker refused - server unavailable.")
    elif rc == 4:
        logging.critical("Connection to broker refused - bad username or password.")
    elif rc == 5:
        logging.critical("Connection to broker refused - not authorised.")
    elif rc >=6 :
        logging.critical("Reserved code received!")
    
    client.close()
 
    exit(1)

       

def mqttOnMessage(client, userdata, message):
    '''This is the event handler for a received message from the MQTT broker.'''
    #print(message.topic+" " + ":" + str(message.payload))     
    if dbConn is not None:
        sendToDB(message.topic,message.payload)
     
    else:
        logging.warning("InfluxDB connection not yet available. Received message dropped.")

def sendToDB(topic,payload):
    '''This function will transmit the given payload to the InfluxDB server'''
    x = topic.split('/')
    y = payload.split(';')  
    node = x[1]
    device = x[2]
    timestamp=y[-1]
    timestamp=int(float(timestamp))
    json_body=[]
  
    if device[0:3]=="cpu":
        json_body = [
        {
            "measurement": "cpu",
            "time": timestamp*1000000000,
            "tags": {
                "node": node,
                "device": device, 
                },            
            "fields": {
                "cpu_temp" : int(y[0]),
                "nomfreq_MHz" : float(y[1]),
                'uclk_MHz' : float(y[2]),
                'pow_dram':float(y[3]),
                'pow_pkg':float(y[4]),
                
                }
            }
        ]
    if device[0:3]=="gpu":
        json_body = [
        {
            "measurement": "gpu",
            "time": int(timestamp*1000000000),
            "tags": {
                "node": node,
                "device": device,              
                },            
            "fields": {
                "gpu_pow" : float(y[0]),
                "gpu_utl" : float(y[1]),
                'mem_utl' : float(y[2]),
                'gpuclk_MHz': float(y[3]),
                'memclk_MHz':float(y[4]),
                
                }
            }
        ]
        
    if device[0:3]=="cor":
        json_body = [
        {
            "measurement": "core",
            "time": int(timestamp*1000000000),
            "tags": {
                "node": node,
                "device": device,
                },            
            "fields": {
                "core_temp" : int(y[0]),
                "CPI" : float(y[1]),
                "IPS" : float(y[2]),
                'Load' : float(y[3]),
                'mclk_MHz':float(y[4]),
                'rclk_MHz':float(y[5]),           
                }
            }
        ]
    try:
        dbConn.write_points(json_body)
        '''logging.warning("Wrote " + x[1] + "to InfluxDB.")'''
    except Exception as e:
        try:
            logging.critical("Couldn't write to InfluxDB: " + e.message)
        except TypeError as e2:
            logging.critical("Couldn't write to InfluxDB.")

def startInfluxDB():
    '''Influx的连接'''
    global dbConn
    try:
        dbConn = InfluxDBClient(InfluxDB_IP, int(InfluxDB_PORT), InfluxDB_USER, InfluxDB_PSWD,InfluxDB_DBNAME)
        print("Connected to InfluxDB successfully ")
        logging.info("Connected to InfluxDB.")
    except InfluxDBClientError as e:
        logging.critical("Could not connect to Influxdb. Message: " + e.content)
        stopMQTT()
        exit(1)
    except:
        logging.critical("Could not connect to InfluxDB.")
        stopMQTT()
        exit(1) 

def stopInfluxDB():
    '''This functions closes our InfluxDB connection'''
    dbConn = None
    logging.info("Disconnected from InfluxDB.") 


def startMQTT():
    '''This function starts the MQTT connection and listens for messages'''
    global client
    client = mqtt.Client()
    client.on_connect = mqttOnConnect  
    client.on_message = mqttOnMessage
    client.connect(MQTT_BROKER, int(MQTT_PORT), 60) 
    client.loop_forever()

def stopMQTT():
    '''This function stops the MQTT client service'''
    global client
    if client is not None:
        #client.loop_stop()
        client.disconnect()
        logging.info("Disconnected from MQTT broker.")
        client = None
    else:
        logging.warning("Attempting to disconnect without first connecting to MQTT broker.")

def main():
    # 从conf文件中读取服务器的相关信息，influx和mqtt的配置都在这里
    config = ConfigParser.RawConfigParser()
    config.read('process.conf')

    global InfluxDB_IP 
    global InfluxDB_PORT
    global InfluxDB_USER 
    global InfluxDB_PSWD 
    global InfluxDB_DBNAME
    global MQTT_BROKER
    global MQTT_PORT
    global MQTT_IN_TOPIC
    global Qos
    global MQTT_USER
   
    # 配置当中MQTT broker信息
    MQTT_BROKER = config.get('MQTT', 'MQTT_BROKER')
    MQTT_PORT = config.get('MQTT', 'MQTT_PORT')
    MQTT_IN_TOPIC = config.get('MQTT','MQTT_IN_TOPIC')
    Qos = config.get('MQTT','Qos')
    # InfluxDB服务器信息
    InfluxDB_IP = config.get('Influx','InfluxDB_IP')
    InfluxDB_PORT = config.get('Influx','InfluxDB_PORT')
    InfluxDB_USER = config.get('Influx','InfluxDB_USER')
    InfluxDB_PSWD = config.get('Influx','InfluxDB_PSWD')
    InfluxDB_DBNAME = config.get('Influx','InfluxDB_DBNAME')
    # 设置退出模式ctrl+c
    setupSigInt()
    setupLogging()
    # Stay here forever
    global client
   
    # 建立MQTT订阅，订阅话题 MQTT_IN_TOPIC
    startMQTT()

    stopInfluxDB()
    stopMQTT()

if __name__ == "__main__":
    main()



 
  
    
    
    
 

