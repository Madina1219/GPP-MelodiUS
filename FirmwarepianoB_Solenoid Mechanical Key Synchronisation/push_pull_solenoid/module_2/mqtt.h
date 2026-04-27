#pragma once

void     mqttSetup();
bool     reconnectMQTT();
void     sendSensorIndex(int idx);
void     mqttLoop();
