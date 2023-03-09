// #include <Arduino.h>
// #include <ArduinoJson.h>
#include "methods.h"

const int maxClients = 5;
int clientcount = 0;

// power reset function
void powerReset() {
  // Disable interrupts
  noInterrupts();

  // Reset the ESP32 by setting the reset reason and then triggering a software reset
  //esp_reset_reason_set(ESP_RST_SW);
  esp_restart();

  // Enable interrupts again
  interrupts();
};

void webSocketEvent(const uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  StaticJsonDocument<1024> doc;
  switch (type) {
    case WStype_DISCONNECTED:
      Serial.printf("[%u] Disconnected!\n", num);
      clientcount--;
      break;
    case WStype_CONNECTED: {
      if (clientcount < maxClients) {
        clientcount++;
        IPAddress ip = webSocket.remoteIP(num);
        Serial.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);

        // send message to client
       // webSocket.sendTXT(num, "{\"frlightcnt\":\"Connected\"}");
      } else {
        webSocket.disconnect();
      }
      break;
    }
    case WStype_TEXT:
      Serial.printf("[%u] Received Text: %s\n", num, payload);
      // parse incoming JSON message
      DeserializationError error = deserializeJson(doc, payload);
      if (error) {
        Serial.println("Deserialization failed");
        Serial.println(error.c_str());
        return;
      }



      // perform action based on the incoming message
      if (doc.containsKey("command")) {
          String command = doc["command"];
          // handle "getLights" command
          if (command == "getLights") {
            // Serial.println("executing getlights");
            doc.clear();
            sendDLights(num);
          } else if(command == "levels"){
            doc.clear();
            sendLevels();
          } else if(command == "setLevel"){
            uint8_t sa=uint8_t(doc["shortAddress"]);
            uint8_t lv=uint8_t(doc["level"]);
            doc.clear();
            bdali.setLightLevel(sa,lv);
          };
      }
    }
    return;
  }

void saveLights() {
  // saves dlight instances to the spiffs
  File file = SPIFFS.open("/lights.bin", "w");
  int size = lights.size();
  file.write((uint8_t*)&size, sizeof(size));
  for (auto& instance : lights) {
    file.write((uint8_t*)&instance, sizeof(instance));
  }
  file.close();
}

void loadLights() {
  //loads dlight instances to the spiffs
  if (SPIFFS.exists("/lights.bin")) {
    File file = SPIFFS.open("/lights.bin", "r");
    int size;
    file.read((uint8_t*)&size, sizeof(size));
    lights.resize(size);
    for (auto& instance : lights) {
      file.read((uint8_t*)&instance, sizeof(instance));
    }
    file.close();
  } else {
    // code to create first instances starting by bdali.findLights()(for the shortaddresses)
    std::vector<uint8_t> shorts = bdali.findLights();
    // then query all data from those shortaddresses  and finally store it in an instance.
    for(auto sa = shorts.begin(); sa != shorts.end(); sa++ ){
        uint8_t shortAddress = *sa;
        String name = "Unknown";
        String room = "Unknown";
        uint8_t minLevel = bdali.getMinLevel(*sa);
        uint8_t maxLevel = bdali.getMaxLevel(*sa);
        uint8_t groups[16] = { 0 };
          bool* groupMembership = bdali.getGroupMembership(*sa);
            for(int i = 0; i < 16; i++) {
            groups[i] = groupMembership[i];
           }
           delete[] groupMembership;
        uint8_t sceneLevels[16] = { 0 };
          uint8_t* scenes = bdali.getSceneLevels(*sa);
            for(int i = 0; i < 16; i++) {
            sceneLevels[i] = scenes[i];
           }
           delete[] scenes;
        uint8_t failLevel = bdali.getFailLevel(*sa);
        uint8_t powerOnLevel = bdali.getPowerOnLevel(*sa);
        uint8_t physmin = bdali.getPhysMinLevel(*sa);
        uint8_t fadeTime = bdali.getFadeTime(*sa);
        uint8_t fadeRate = bdali.getFadeRate(*sa);
  
  // Create an instance of DLight with the above parameters
  DLight light(shortAddress, name, room, minLevel, maxLevel, groups, sceneLevels, failLevel, powerOnLevel, physmin, fadeTime, fadeRate);

  // Add the DLight instance to the lights vector
  lights.push_back(light);
    }
    saveLights();
  }
}
void sendLevels(){
  // creates a jsonobject of all shortaddresses from all dlight instances, queries the live light levels and sends them to the client
      // create a JsonObject and set its "command" property to "levels"
       DynamicJsonDocument doc(1024);
      JsonObject data = doc.to<JsonObject>();
      data["command"] = "levels";

      // create a JsonArray for the lights and add it to the JsonObject
      JsonArray lightsArray = data.createNestedArray("lights");

      // loop through the lights vector and add each DLight instance as a JsonObject to the lightsArray
      for (auto& light : lights) {
        // create a JsonObject and set its properties
        JsonObject lightObject = lightsArray.createNestedObject();
        lightObject["shortAddress"] = light.shortAddress;
        lightObject["level"] = bdali.getLightLevel(light.shortAddress);
  // Query the current level for the DALI light using bdali.getLightLevel()
      }
        // serialize the document to a String
      String jsonString;
      serializeJson(doc, jsonString);
webSocket.broadcastTXT(jsonString);
};


void sendDLights(uint8_t num){
    //creates a jsonobject of all dlight instances and sends it to the websocket
      // create a DynamicJsonDocument object with the capacity for the number of lights in the vector
      DynamicJsonDocument doc(1024 * lights.size());

      // create a JsonObject and set its "command" property to "lights"
      JsonObject data = doc.to<JsonObject>();
      data["command"] = "lights";

      // create a JsonArray for the lights and add it to the JsonObject
      JsonArray lightsArray = data.createNestedArray("lights");

      // loop through the lights vector and add each DLight instance as a JsonObject to the lightsArray
      for (auto& light : lights) {
        // create a JsonObject and set its properties
        JsonObject lightObject = lightsArray.createNestedObject();
        lightObject["shortAddress"] = light.shortAddress;
        lightObject["name"] = light.name;
        lightObject["room"] = light.room;
        lightObject["minLevel"] = light.minLevel;
        lightObject["maxLevel"] = light.maxLevel;

        // create a JsonArray for the groups and add them to the JsonObject
        JsonArray groupsArray = lightObject.createNestedArray("groups");
        for (int i = 0; i < 16; i++) {
          groupsArray.add(light.groups[i]);
        }

        // create a JsonArray for the sceneLevels and add them to the JsonObject
        JsonArray sceneLevelsArray = lightObject.createNestedArray("sceneLevels");
        for (int i = 0; i < 16; i++) {
          sceneLevelsArray.add(light.sceneLevels[i]);
        }

        lightObject["failLevel"] = light.failLevel;
        lightObject["powerOnLevel"] = light.powerOnLevel;
        lightObject["physmin"] = light.physmin;
        lightObject["fadeTime"] = light.fadeTime;
        lightObject["fadeRate"] = light.fadeRate;
      }

      // serialize the document to a String
      String jsonString;
      serializeJson(doc, jsonString);
      webSocket.sendTXT(num, jsonString);
}

